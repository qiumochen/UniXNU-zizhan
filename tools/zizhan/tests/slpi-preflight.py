#!/usr/bin/env python3
"""Exercise actual SLPI references and production MDT placement, without a phone."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("slpi_preflight", ROOT / "tools/zizhan/mac/slpi-preflight.py")
slpi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(slpi)


def cells(*values):
    return struct.pack(">" + "I" * len(values), *values)


def nodes():
    return {
        "/": {"model": b"Zizhan SM8475\0", "#address-cells": cells(2), "#size-cells": cells(2)},
        "/soc": {"#address-cells": cells(1), "#size-cells": cells(1), "ranges": cells(0, 0, 0, 0xffffffff)},
        "/reserved-memory": {},
        "/reserved-memory/slpi": {"phandle": cells(41), "reg": cells(0, 0x88000000, 0, 0x1900000), "no-map": b""},
        "/soc/ssc": {"compatible": b"qcom,msm-ssc-sensors\0", "qcom,firmware-name": b"slpi\0",
                     "qcom,rproc-handle": cells(42)},
        "/soc/slpi": {"compatible": b"qcom,waipio-slpi-pas\0", "phandle": cells(42),
                      "reg": cells(0x02400000, 0x10000), "memory-region": cells(41)},
        "/soc/slpi/glink-edge": {"transport": b"smem\0", "label": b"slpi\0", "qcom,glink-label": b"dsps\0",
                                 "qcom,remote-pid": cells(3)},
        "/soc/slpi/glink-edge/qrtr": {"qcom,glink-channels": b"IPCRTR\0", "qcom,net-id": cells(2)},
    }


def image(base=0x88000000, memory=0x1000, flags=5):
    """Synthetic ELF32: metadata header, hash segment and one DSP PT_LOAD."""
    header = bytearray(52)
    header[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<HHI", header, 16, 2, 164, 1)
    struct.pack_into("<I", header, 28, 52)
    struct.pack_into("<HHH", header, 40, 52, 32, 3)
    ph = lambda *v: struct.pack("<8I", *v)
    return (bytes(header) + ph(0, 0, 0, 0, 148, 0, 0, 4) +
            ph(0, 148, 0, 0, 16, 16, 2 << 24, 4) +
            ph(1, 164, base, base, 4, memory, flags, 4) + b"h" * 16 + b"abcd")


def encode_fdt(tree):
    """Serialize a synthetic node dictionary through standard FDT tokens."""
    names, offsets, body = bytearray(), {}, bytearray()

    def emit(path):
        name = "" if path == "/" else path.rsplit("/", 1)[1]
        body.extend(cells(1) + name.encode() + b"\0")
        body.extend(b"\0" * (-len(body) % 4))
        for key, value in tree[path].items():
            if key not in offsets:
                offsets[key] = len(names)
                names.extend(key.encode() + b"\0")
            body.extend(cells(3, len(value), offsets[key]) + value)
            body.extend(b"\0" * (-len(body) % 4))
        for child in tree:
            if child != path and (child.rsplit("/", 1)[0] or "/") == path:
                emit(child)
        body.extend(cells(2))

    emit("/")
    body.extend(cells(9))
    return (cells(0xd00dfeed, 56 + len(body) + len(names), 56, 56 + len(body),
                  40, 17, 16, 0, len(names), len(body)) + b"\0" * 16 + body + names)


class SlpiPreflightTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="zizhan-slpi-test-")
        cls.path = Path(cls.temp.name)
        cls.checker = cls.path / "checker"
        # Same code and warning policy as the public preflight, plus sanitizers.
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                        "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        str(ROOT / "tools/zizhan/mac/slpi-firmware-check.c"),
                        str(ROOT / "drivers/PongPower/QcomMdt.c"), "-o", str(cls.checker)],
                       check=True, timeout=30)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check_image(self, raw):
        firmware = self.path / "fixture.mbn"
        firmware.write_bytes(raw)
        result = subprocess.run([str(self.checker), str(firmware)], capture_output=True, text=True, timeout=10)
        return result.returncode, json.loads(result.stdout)

    def test_real_parser_places_fixed_and_relocatable_images(self):
        for raw in (image(), image(base=0, flags=5 | (1 << 27))):
            code, result = self.check_image(raw)
            self.assertEqual(code, 0)
            self.assertEqual(result["span"], 4096)
            self.assertEqual(result["metadata_bytes"], 164)
            self.assertFalse(result["authenticated"])
            self.assertFalse(result["started"])

    def test_wrong_base_oversize_and_truncated_loads_rejected(self):
        for raw in (image(base=0x9fd00000), image(memory=0x3100000), image()[:-1]):
            with self.subTest(size=len(raw)):
                code, result = self.check_image(raw)
                self.assertNotEqual(code, 0)
                self.assertEqual(result["status"], "FAIL")

    def test_missing_hash_wrong_machine_and_corrupt_headers_rejected(self):
        no_hash = bytearray(image())
        struct.pack_into("<I", no_hash, 52 + 32 + 24, 0)
        empty_hash = bytearray(image())
        struct.pack_into("<I", empty_hash, 52 + 32 + 16, 0)
        wrong_machine = bytearray(image())
        wrong_machine[18] = 183
        bad_headers = bytearray(image())
        struct.pack_into("<H", bad_headers, 42, 1)
        for raw in (b"", bytes(no_hash), bytes(empty_hash), bytes(wrong_machine), bytes(bad_headers)):
            self.assertNotEqual(self.check_image(raw)[0], 0)

    def test_saved_dt_shape_and_numeric_domain_ids(self):
        result = slpi.describe_dt(encode_fdt(nodes()))
        self.assertEqual(result["route"]["remote_pid"], 3)
        self.assertEqual(result["route"]["net_id"], 2)
        self.assertEqual(result["carveout"]["bytes"], 0x1900000)

    def test_wrong_owner_firmware_carveout_translation_refused(self):
        for path, key, value in (("/", "model", b"Pong SM8475\0"),
                                 ("/soc", "ranges", cells(0, 0, 0x1000, 0xffffffff)),
                                 ("/soc/ssc", "qcom,rproc-handle", cells(99)),
                                 ("/soc/ssc", "qcom,firmware-name", b"adsp\0"),
                                 ("/soc/slpi", "compatible", b"qcom,waipio-adsp-pas\0"),
                                 ("/reserved-memory/slpi", "reg", cells(0, 0x9fd00000, 0, 0x3100000)),
                                 ("/soc/slpi/glink-edge", "qcom,remote-pid", cells(2)),
                                 ("/soc/slpi/glink-edge/qrtr", "qcom,net-id", cells(1))):
            tree = copy.deepcopy(nodes())
            tree[path][key] = value
            with self.subTest(path=path, key=key), self.assertRaises(ValueError):
                slpi.describe_nodes(tree)

    def test_disabled_ancestor_and_duplicate_reference_refused(self):
        tree = nodes()
        tree["/soc"]["status"] = b"disabled\0"
        with self.assertRaises(ValueError):
            slpi.describe_nodes(tree)
        tree = nodes()
        tree["/soc/duplicate"] = {"phandle": cells(42)}
        with self.assertRaises(ValueError):
            slpi.describe_nodes(tree)

    def test_private_output_and_no_overwrite(self):
        with self.assertRaises(ValueError):
            slpi.private_output(ROOT / "forbidden-evidence.json")
        present = self.path / "present.json"
        present.write_text("preserved")
        with self.assertRaises(ValueError):
            slpi.private_output(present)
        self.assertEqual(present.read_text(), "preserved")

    def test_full_cli_and_retained_failure_receipt(self):
        dtb, firmware = self.path / "fixture.dtb", self.path / "slpi.mbn"
        dtb.write_bytes(encode_fdt(nodes()))
        for name, raw, expected in (("success", image(), "HOST_VERIFIED_PLACEMENT"),
                                    ("failure", image(base=0x9fd00000), "FAIL")):
            firmware.write_bytes(raw)
            output = self.path / (name + ".json")
            result = subprocess.run([os.sys.executable, str(ROOT / "tools/zizhan/mac/slpi-preflight.py"),
                                     "--dtb", str(dtb), "--firmware", str(firmware), "--output", str(output)],
                                    capture_output=True, text=True, timeout=30)
            report = json.loads(output.read_text())
            self.assertEqual(report["status"], expected, result.stdout + result.stderr)
            self.assertEqual(result.returncode == 0, name == "success")
            self.assertFalse(report["phone_operations"])
            self.assertFalse(report["transport_ready"])


if __name__ == "__main__":
    unittest.main()
