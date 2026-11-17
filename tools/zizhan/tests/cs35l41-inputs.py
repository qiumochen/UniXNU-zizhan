#!/usr/bin/env python3
"""Malformed/role/hash controls for offline CS35L41 inputs; no private files."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "tools/zizhan/mac/cs35l41-input-preflight.py"
spec = importlib.util.spec_from_file_location("amp_inputs", TOOL)
amp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(amp)


def region(kind, offset, payload):
    return struct.pack("<II", kind << 24 | offset, len(payload)) + payload


def firmware():
    return (struct.pack("<4sIHBBIIIIQI", b"WMFW", 40, 0x300, 4, 3, 8, 8, 8, 0, 0, 0) +
            region(0xff, 0, b"test") + region(0x11, 0, bytes(24)) +
            region(0x12, 0, bytes(24)) + region(0x10, 0, bytes(40)))


def coeff(kind=0x12):
    return struct.pack("<4sI", b"WMDR", 16) + struct.pack(">II", 1, 4) + \
        struct.pack("<HHIIII", 4, kind, 0xcd, 0x100, 0, 4) + bytes(4)


def make_bundle(path):
    rows = []
    for name, role in amp.REQUIRED.items():
        data = firmware() if name.endswith(".wmfw") else coeff() if name.endswith(".bin") else b"4,-5,0,7"
        (path / name).write_bytes(data)
        digest = hashlib.sha256(data).hexdigest()
        rows.append(dict(file=name, source="/vendor/firmware/" + name, role=role,
                         bytes=len(data), sha256=digest, remote_sha256=digest))
    manifest = dict(status="COPIED_AND_HASH_MATCHED_ONLY", phone_writes=False,
                    firmware_loaded=False, codec_writes=False, audio_output=False, files=rows)
    (path / "manifest.json").write_text(json.dumps(manifest))
    return manifest


class Inputs(unittest.TestCase):
    def test_valid_scoped_containers(self):
        fw = amp.wmfw(firmware())
        self.assertEqual(len(fw["regions"]), 4)
        self.assertFalse(fw["hardware_address_plan"])
        self.assertFalse(amp.wmdr(coeff())["algorithm_bases_resolved"])
        self.assertTrue(amp.tuning(b"4,-5,0,7")["signed_values"])

    def test_firmware_truncation_header_type_and_range(self):
        raw = firmware()
        for bad in (raw[:20], raw[:-1], raw + b"\0", b"BAD!" + raw[4:], raw[:10] + b"\x02" + raw[11:],
                    raw[:4] + struct.pack("<I", 39) + raw[8:],
                    raw[:40] + region(0xf0, 0, bytes(4)),
                    raw[:40] + region(0x11, 8, bytes(24)),
                    raw[:40] + region(0x11, 0, bytes(23))):
            with self.subTest(length=len(bad)), self.assertRaises(ValueError):
                amp.wmfw(bad)

    def test_overlap_missing_regions_and_length(self):
        raw = firmware()
        for bad in (raw + region(0x11, 0, bytes(3)), raw[:40] + region(0xff, 0, b"text"),
                    raw[:40] + struct.pack("<II", 0x11000000, 0xffffffff)):
            with self.assertRaises(ValueError):
                amp.wmfw(bad)

    def test_coeff_bounds_and_absolute_write_refusal(self):
        raw = coeff()
        for bad in (raw[:-1], raw + b"\0", coeff(0xf000), coeff(0x13), b"BAD!" + raw[4:],
                    raw[:12] + struct.pack(">I", 2) + raw[16:],
                    raw[:4] + struct.pack("<I", 17) + raw[8:],
                    raw[:32] + struct.pack("<I", 0xffffffff) + raw[36:]):
            with self.subTest(length=len(bad)), self.assertRaises(ValueError):
                amp.wmdr(bad)

    def test_coeff_padding_and_algorithm(self):
        for payload in (struct.pack("<HHIIII", 0, 0xfe00, 0, 0, 0, 1) + b"x\x01\0\0",
                        struct.pack("<HHIIII", 0, 0x12, 0, 0, 0, 4) + bytes(4)):
            with self.assertRaises(ValueError):
                amp.wmdr(coeff()[:16] + payload)

    def test_tuning_count_syntax_and_range(self):
        for raw in (b"", b"3,1", b"2,2147483648", b"2,-2147483649", b"2,1,", b"2,0x1", b"2,1;write", b"1", b"\xff"):
            with self.subTest(raw=raw), self.assertRaises((ValueError, UnicodeError)):
                amp.tuning(raw)

    def test_bundle_role_remote_hash_and_drift(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            manifest = make_bundle(path)
            self.assertEqual(len(amp.inspect(path)["files"]), 11)
            for field, value in (("role", "speaker-protection"), ("remote_sha256", "0" * 64),
                                 ("bytes", 1), ("source", "/tmp/file")):
                revised = json.loads(json.dumps(manifest))
                index = next(i for i, r in enumerate(revised["files"]) if r["file"].endswith(".txt"))
                revised["files"][index][field] = value
                (path / "manifest.json").write_text(json.dumps(revised))
                with self.subTest(field=field), self.assertRaises(ValueError):
                    amp.inspect(path)
            (path / "manifest.json").write_text(json.dumps(manifest))
            (path / manifest["files"][0]["file"]).write_bytes(b"drift")
            with self.assertRaises(ValueError):
                amp.inspect(path)

    def test_missing_duplicate_symlink_and_bad_rows(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            manifest = make_bundle(path)
            for rows in (manifest["files"][:-1], manifest["files"][:-1] + [manifest["files"][0]], ["bad"]):
                changed = dict(manifest, files=rows)
                (path / "manifest.json").write_text(json.dumps(changed))
                with self.assertRaises(ValueError):
                    amp.inspect(path)
            (path / "manifest.json").write_text(json.dumps(manifest))
            file = path / manifest["files"][0]["file"]
            alternate = path / "alternate"
            file.rename(alternate)
            file.symlink_to(alternate)
            with self.assertRaises(ValueError):
                amp.inspect(path)

    def test_cli_scope_failures_and_report_retention(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            bundle = path / "bundle"
            bundle.mkdir()
            make_bundle(bundle)
            output = path / "report.json"
            cmd = [sys.executable, str(TOOL), "--bundle", str(bundle), "--output", str(output)]
            self.assertEqual(subprocess.run(cmd, capture_output=True, timeout=10).returncode, 0)
            report = json.loads(output.read_text())
            for key in ("phone_operations", "firmware_loaded", "dsp_started", "tuning_applied", "calibration_run",
                        "audio_output", "playback_qualified", "hardware_write_plan", "firmware_authentication_checked"):
                self.assertIs(report[key], False)
            original = output.read_bytes()
            self.assertNotEqual(subprocess.run(cmd, capture_output=True, timeout=10).returncode, 0)
            self.assertEqual(output.read_bytes(), original)
            (bundle / "manifest.json").write_text("[]")
            failed = path / "failure.json"
            self.assertNotEqual(subprocess.run(cmd[:-1] + [str(failed)], capture_output=True, timeout=10).returncode, 0)
            self.assertEqual(json.loads(failed.read_text())["status"], "FAIL")
            self.assertNotEqual(subprocess.run(cmd[:-1] + [str(ROOT / "should-not-write.json")], capture_output=True, timeout=10).returncode, 0)


if __name__ == "__main__":
    unittest.main()
