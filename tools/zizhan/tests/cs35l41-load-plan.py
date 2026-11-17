#!/usr/bin/env python3
"""Protection memory planning controls with wholly synthetic HALO data."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "tools/zizhan/mac/cs35l41-load-plan.py"
spec = importlib.util.spec_from_file_location("amp_plan", TOOL)
plan = importlib.util.module_from_spec(spec)
spec.loader.exec_module(plan)
spec = importlib.util.spec_from_file_location("amp_test_fixture", ROOT / "tools/zizhan/tests/cs35l41-inputs.py")
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


def packed(words):
    raw = b"".join(v.to_bytes(3, "little") for v in words)
    raw += bytes(-len(raw) % 12)
    return b"".join(raw[i:i + 4][::-1] for i in range(0, len(raw), 4))


def firmware(words=None):
    if words is None:
        words = [0x40000, 0x30000, 2, 0x123, 0x100, 40, 2, 0, 0, 1,
                 0xcd, 0x200, 32, 4, 8, 16]
    data = struct.pack("<4sIHBBIIIIQI", b"WMFW", 40, 0x300, 4, 3, 64, 64, 32, 0, 0, 0)
    return data + fixture.region(0x11, 0, packed(words)) + fixture.region(0x12, 0, bytes(192)) + \
        fixture.region(0x10, 0, bytes(160))


def coefficient(kind=0x12, offset=4, size=12, alg=0xcd, version=0x20000):
    data = struct.pack("<4sI", b"WMDR", 16) + struct.pack(">II", 1, 4)
    return data + struct.pack("<HHIIII", offset, kind, alg, version, 0, size) + bytes(size)


def bundle(path):
    rows = []
    for name, role in plan.inputs.REQUIRED.items():
        data = firmware() if name.endswith(".wmfw") else coefficient() if name.endswith(".bin") else b"4,-1,0,5"
        (path / name).write_bytes(data)
        digest = plan.hashlib.sha256(data).hexdigest()
        rows.append(dict(file=name, source="/vendor/firmware/" + name, role=role,
                         bytes=len(data), sha256=digest, remote_sha256=digest))
    manifest = dict(status="COPIED_AND_HASH_MATCHED_ONLY", phone_writes=False,
                    firmware_loaded=False, codec_writes=False, audio_output=False, files=rows)
    (path / "manifest.json").write_text(json.dumps(manifest))


class Plans(unittest.TestCase):
    def setUp(self):
        self.raw = firmware()
        self.fw = plan.inputs.wmfw(self.raw)
        self.identity = plan.unpack_xm_header(self.raw, self.fw)

    def test_halo_lane_conversion_and_identity(self):
        self.assertEqual(self.identity["firmware_id"], 0x123)
        self.assertEqual(self.identity["algorithms"][1]["id"], 0xcd)
        self.assertEqual(self.identity["algorithms"][1]["ym_base"], 8)
        self.assertEqual(self.identity["algorithms"][1]["ym_size"], 16)

    def test_memory_and_metadata_exclusion(self):
        rows = plan.firmware_plan(self.fw, "f" * 64)
        self.assertEqual([r["register"] for r in rows], [0x2000000, 0x2c00000, 0x3800000])
        self.assertTrue(all(r["source_sha256"] == "f" * 64 for r in rows))
        changed = copy.deepcopy(self.fw)
        changed["regions"].append(dict(type="ALGORITHM_METADATA", bytes=3, file_offset=0, word_offset=0))
        self.assertEqual(plan.firmware_plan(changed, "f" * 64), rows)

    def test_coefficient_byte_offset_after_base_conversion(self):
        rows = plan.coefficient_plan(plan.inputs.wmdr(coefficient()), self.identity, self.fw["sizes_words"], "a" * 64)
        self.assertEqual(rows[0]["register"], 0x2c00000 + 8 * 3 + 4)
        rows = plan.coefficient_plan(plan.inputs.wmdr(coefficient(6)), self.identity, self.fw["sizes_words"], "a" * 64)
        self.assertEqual(rows[0]["register"], 0x3400000 + 8 * 4 + 4)

    def test_identity_truncation_core_count_and_duplicate(self):
        base = [0x40000, 0x30000, 2, 0x123, 0x100, 40, 2, 0, 0, 1, 0xcd, 0x200, 32, 4, 8, 16]
        for index, value in ((0, 0), (1, 0), (9, 0), (9, 33), (9, 2), (10, 0), (10, 0x123), (11, 0), (14, 63), (15, 65)):
            words = base.copy()
            words[index] = value
            raw = firmware(words)
            with self.subTest(index=index, value=value), self.assertRaises(ValueError):
                plan.unpack_xm_header(raw, plan.inputs.wmfw(raw))
        missing = copy.deepcopy(self.fw)
        missing["regions"][0]["word_offset"] = 1
        with self.assertRaises(ValueError):
            plan.unpack_xm_header(self.raw, missing)

    def test_algorithm_overlap(self):
        words = [0x40000, 0x30000, 2, 0x123, 0x100, 40, 2, 0, 0, 2,
                 0xcd, 0x200, 32, 4, 8, 16, 0xce, 0x200, 33, 4, 8, 16]
        raw = firmware(words)
        with self.assertRaises(ValueError):
            plan.unpack_xm_header(raw, plan.inputs.wmfw(raw))

    def test_unknown_algorithm_version_empty_and_program_memory(self):
        for data in (coefficient(alg=9), coefficient(version=0x20100), coefficient(kind=0x10),
                     coefficient(5, offset=16, size=4), coefficient(offset=48, size=4), coefficient(offset=1)):
            with self.subTest(data=data[:36].hex()), self.assertRaises(ValueError):
                plan.coefficient_plan(plan.inputs.wmdr(data), self.identity, self.fw["sizes_words"], "a" * 64)
        identity = copy.deepcopy(self.identity)
        identity["algorithms"][1]["ym_size"] = 0
        with self.assertRaises(ValueError):
            plan.coefficient_plan(plan.inputs.wmdr(coefficient()), identity, self.fw["sizes_words"], "a" * 64)

    def test_cross_alias_coeff_overlap(self):
        first = coefficient()
        second = coefficient(6)[16:]
        with self.assertRaises(ValueError):
            plan.coefficient_plan(plan.inputs.wmdr(first + second), self.identity, self.fw["sizes_words"], "a" * 64)
        changed = copy.deepcopy(self.fw)
        changed["regions"].append(dict(type="XM", word_offset=1, bytes=4, file_offset=0))
        with self.assertRaises(ValueError):
            plan.firmware_plan(changed, "f" * 64)

    def test_physical_limit_and_firmware_alignment(self):
        bad = copy.deepcopy(self.fw)
        bad["sizes_words"]["pm"] = 4097
        with self.assertRaises(ValueError):
            plan.firmware_plan(bad, "f" * 64)
        for field, value in (("bytes", 3), ("word_offset", 1), ("bytes", 16384)):
            bad = copy.deepcopy(self.fw)
            bad["regions"][-1][field] = value
            with self.assertRaises(ValueError):
                plan.firmware_plan(bad, "f" * 64)

    def test_bundle_cli_no_execution_and_report_retention(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            directory = path / "bundle"
            directory.mkdir()
            bundle(directory)
            result = plan.build(directory)
            self.assertEqual([r["address"] for r in result["channels"]], [0x40, 0x42])
            output = path / "plan.json"
            cmd = [sys.executable, str(TOOL), "--bundle", str(directory), "--output", str(output)]
            self.assertEqual(subprocess.run(cmd, capture_output=True, timeout=10).returncode, 0)
            report = json.loads(output.read_text())
            for key in ("phone_operations", "plan_executable", "hardware_writes", "dsp_started", "audio_output",
                        "runtime_algorithm_readback_verified", "b2_errata_applied", "otp_unpacked", "scene_parameters_applied", "calibration_run"):
                self.assertIs(report[key], False)
            self.assertTrue(report["remaining_gates"])
            original = output.read_bytes()
            self.assertNotEqual(subprocess.run(cmd, capture_output=True, timeout=10).returncode, 0)
            self.assertEqual(output.read_bytes(), original)
            self.assertNotEqual(subprocess.run(cmd[:-1] + [str(ROOT / "should-not-write.json")], capture_output=True, timeout=10).returncode, 0)


if __name__ == "__main__":
    unittest.main()
