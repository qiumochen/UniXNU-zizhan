#!/usr/bin/env python3
"""Local HTTP controls for native restore range resume and CRC rejection."""
import importlib.util
import io
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import tempfile
import threading
from types import SimpleNamespace
import unittest
import zipfile

PATH = Path(__file__).resolve().parents[1] / "mac/native18-inputs.py"
SPEC = importlib.util.spec_from_file_location("native_inputs", PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class RangeControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.payload = bytes(range(256)) * (65536 + 17)
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_STORED) as archive:
            archive.writestr("native.dmg.aea", cls.payload)
        cls.archive = stream.getvalue()

    def control(self, corrupt=False, status=206):
        requests = []
        archive = self.archive
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                requested = self.headers["Range"].removeprefix("bytes=")
                first, last = map(int, requested.split("-"))
                requests.append((first, last))
                data = archive[first:last + 1]
                if corrupt:
                    data = bytes([data[0] ^ 1]) + data[1:]
                if status != 206:
                    data = b""
                self.send_response(status)
                self.send_header("Content-Range", f"bytes {first}-{last}/{len(archive)}")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
            def log_message(self, *_):
                pass
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        return f"http://127.0.0.1:{server.server_port}/restore.ipsw", requests

    def image(self, output, url):
        stream = io.BytesIO(self.archive)
        archive = zipfile.ZipFile(stream)
        self.addCleanup(archive.close)
        return SimpleNamespace(zip=archive, stream=stream, cache=output, spec=url,
                               member=lambda _: "native.dmg.aea")

    def test_resume_contiguous_prefix_and_check_crc(self):
        url, requests = self.control()
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            partial = output / "native.dmg.aea.partial"
            prefix = (8 << 20) + 37
            partial.write_bytes(self.payload[:prefix])
            image = self.image(output, url)
            result = MODULE.stored_component(image, "OS", 3)
            self.assertEqual(result.read_bytes(), self.payload)
            local_start = 30 + len("native.dmg.aea")
            self.assertEqual(len(requests), 2)
            self.assertTrue(all(first >= local_start + (8 << 20) for first, _ in requests))
            receipt = json.loads(result.with_suffix(result.suffix + ".json").read_text())
            self.assertTrue(receipt["zip_crc_verified"])
            self.assertEqual(MODULE.checked_component_digest(result, image.zip.getinfo("native.dmg.aea")), receipt["sha256"])
            with result.open("r+b") as stream:
                stream.write(b"bad")
            with self.assertRaisesRegex(RuntimeError, "differs"):
                MODULE.checked_component_digest(result, image.zip.getinfo("native.dmg.aea"))

    def test_corrupt_range_never_promotes_component(self):
        url, _ = self.control(corrupt=True)
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            with self.assertRaisesRegex(RuntimeError, "CRC mismatch"):
                MODULE.stored_component(self.image(output, url), "OS", 3)
            self.assertFalse((output / "native.dmg.aea").exists())
            self.assertTrue((output / "native.dmg.aea.partial").exists())

    def test_ignored_range_is_rejected(self):
        url, _ = self.control(status=200)
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            with self.assertRaisesRegex(RuntimeError, "exact component range"):
                MODULE.stored_component(self.image(output, url), "OS", 1)
            self.assertFalse((output / "native.dmg.aea").exists())


if __name__ == "__main__":
    unittest.main()
