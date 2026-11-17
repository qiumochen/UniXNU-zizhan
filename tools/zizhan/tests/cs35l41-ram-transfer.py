#!/usr/bin/env python3
"""Bounded RAM transport controls; all payloads/readbacks are synthetic."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "tools/zizhan/mac/cs35l41-ram-transfer.py"
spec = importlib.util.spec_from_file_location("ram_transfer", TOOL)
ram = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ram)
spec = importlib.util.spec_from_file_location("ram_fixture", Path(__file__).with_name("cs35l41-load-plan.py"))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class Transfer(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.bundle = self.path / "bundle"
        self.bundle.mkdir()
        fixture.bundle(self.bundle)
        self.report, self.chunks = ram.prepare(self.bundle)
        self.rows = [dict(address=a, register=r, data_hex=d.hex()) for a, r, d in self.chunks]

    def test_exact_file_bytes_and_register_stride(self):
        raw = (self.bundle / ram.FIRMWARE).read_bytes()
        count = 0
        for segment in self.report["segments"]:
            for offset in range(0, segment["bytes"], 4):
                address, register, data = self.chunks[count]
                self.assertEqual(address, segment["address"])
                self.assertEqual(register, segment["register"] + offset)
                start = segment["source_offset"] + offset
                self.assertEqual(data, raw[start:start + 4])
                count += 1
        self.assertEqual(count, self.report["write_transactions"])
        self.assertEqual(ram.verify(self.chunks, self.rows)["status"], "SUPPLIED_READBACK_BYTES_MATCH")
        self.assertFalse(self.report["coefficients_included"])
        self.assertFalse(self.report["device_verified"])
        self.assertFalse(self.report["plan_executable"])

    def test_corrupt_truncated_duplicate_reordered_readback(self):
        changed = copy.deepcopy(self.rows)
        changed[0]["data_hex"] = "ffffffff"
        swapped = self.rows.copy()
        swapped[0], swapped[1] = swapped[1], swapped[0]
        for rows in (changed, self.rows[:-1], self.rows + self.rows[:1], swapped,
                     [self.rows[0]] * len(self.rows)):
            with self.subTest(rows=rows[:1]), self.assertRaises(ValueError):
                ram.verify(self.chunks, rows)

    def test_wrong_chip_register_encoding_and_extra_fields(self):
        for key, value in (("address", 0x41), ("register", 0x40), ("register", True),
                           ("data_hex", "00"), ("data_hex", "zzzzzzzz"),
                           ("data_hex", "        "), ("provenance", "device")):
            rows = copy.deepcopy(self.rows)
            rows[0][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                ram.verify(self.chunks, rows)

    def test_manifest_drift_rejected(self):
        path = self.bundle / ram.FIRMWARE
        path.write_bytes(path.read_bytes() + bytes(4))
        with self.assertRaises(ValueError):
            ram.prepare(self.bundle)

    def test_changed_firmware_after_inspection_rejected(self):
        build = ram.memory.build
        def changing(bundle):
            result = build(bundle)
            path = bundle / ram.FIRMWARE
            raw = bytearray(path.read_bytes())
            raw[-1] ^= 1
            path.write_bytes(raw)
            return result
        with patch.object(ram.memory, "build", changing), self.assertRaises(ValueError):
            ram.prepare(self.bundle)

    def test_size_and_segment_bounds(self):
        for constant in ("MAX_FILE_BYTES", "MAX_RAM_BYTES_PER_CHIP", "MAX_SEGMENTS"):
            with self.subTest(constant=constant), patch.object(ram, constant, 1), self.assertRaises(ValueError):
                ram.prepare(self.bundle)

    def test_cli_supplied_bytes_are_never_device_evidence(self):
        readback = self.path / "synthetic-readback.json"
        readback.write_text(json.dumps(self.rows))
        output = self.path / "verified.json"
        command = [sys.executable, str(TOOL), "--bundle", str(self.bundle),
                   "--readback", str(readback), "--output", str(output)]
        self.assertEqual(subprocess.run(command, capture_output=True, timeout=10).returncode, 0)
        report = json.loads(output.read_text())
        self.assertFalse(report["device_verified"])
        self.assertFalse(report["readback"]["provenance_verified"])
        self.assertFalse(report["readback"]["device_verified"])
        readback.write_text(json.dumps(self.rows[:-1]))
        failed = self.path / "failed.json"
        self.assertNotEqual(subprocess.run(command[:-1] + [str(failed)], capture_output=True, timeout=10).returncode, 0)
        self.assertEqual(json.loads(failed.read_text())["status"], "FAIL")

    def test_cli_private_report_retention_and_no_claims(self):
        output = self.path / "report.json"
        command = [sys.executable, str(TOOL), "--bundle", str(self.bundle), "--output", str(output)]
        self.assertEqual(subprocess.run(command, capture_output=True, timeout=10).returncode, 0)
        report = json.loads(output.read_text())
        self.assertNotIn("readback", report)
        for key in ("phone_operations", "hardware_writes", "device_verified", "dsp_started", "audio_output"):
            self.assertIs(report[key], False)
        original = output.read_bytes()
        self.assertNotEqual(subprocess.run(command, capture_output=True, timeout=10).returncode, 0)
        self.assertEqual(original, output.read_bytes())
        self.assertNotEqual(subprocess.run(command[:-1] + [str(ROOT / "must-not-write.json")], capture_output=True, timeout=10).returncode, 0)


if __name__ == "__main__":
    unittest.main()
