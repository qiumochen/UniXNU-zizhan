#!/usr/bin/env python3
"""Exercise production TSENS core and actual ADT encoding without MMIO."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from zizhan_tsens_inputs import encode
from pong_adt_contract import parse_adt


class TsensProbeTests(unittest.TestCase):
    def setUp(self):
        self.profile = json.loads((ROOT / "configs/zizhan-thermal-sm8475.json").read_text())

    def test_actual_core(self):
        with tempfile.TemporaryDirectory(prefix="zizhan-tsens-") as td:
            binary = str(Path(td) / "core")
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                            "-Werror", "-fsanitize=undefined,address",
                            str(ROOT / "tools/zizhan/tests/tsens-core.cpp"), "-o", binary],
                           check=True, timeout=30)
            subprocess.run([binary], check=True, timeout=10)

    def test_actual_adt_wire_roundtrip(self):
        spec = importlib.util.spec_from_file_location("adt", ROOT / "scripts/make-pong-adt-ipados18.py")
        adt = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(adt)
        blob = adt.node("device-tree", children=[adt.node("chosen", [("zizhan-tsens-v2", encode(self.profile))])])
        values = struct.unpack("<12Q", parse_adt(blob)["/chosen"]["zizhan-tsens-v2"])
        self.assertEqual(values, (1, 2, 0xc263000, 511, 0xc222000, 511, 0xffff,
                                 0xc265000, 511, 0xc223000, 511, 0xffff))

    def test_unreviewed_translation_rejected(self):
        for translation in (None, {}, dict(self.profile["tsens_address_translation"], physical_base="0x1000")):
            with self.subTest(translation=translation), self.assertRaises(ValueError):
                encode(dict(self.profile, tsens_address_translation=translation))

    def test_wrong_board_and_bank_count_rejected(self):
        for key, value in (("board", "pong"), ("soc", "SM8550"), ("tsens", self.profile["tsens"][:1])):
            with self.subTest(key=key), self.assertRaises(ValueError):
                encode(dict(self.profile, **{key: value}))

    def test_malformed_resources_rejected(self):
        for base, size in (("0x0", 511), ("0xc263001", 511), ("0xc263000", 0xe7),
                           ("0xc263000", 0x1001), ("0xfffffffc", 511)):
            p = copy.deepcopy(self.profile)
            p["tsens"][0]["regions"][0] = dict(bus_address=base, bytes=size)
            with self.subTest(base=base, size=size), self.assertRaises(ValueError):
                encode(p)

    def test_overlap_and_unknown_layout_rejected(self):
        p = copy.deepcopy(self.profile)
        p["tsens"][1]["regions"][0] = p["tsens"][0]["regions"][1]
        with self.assertRaises(ValueError):
            encode(p)
        p = copy.deepcopy(self.profile)
        p["tsens"][0]["declared_sensor_count"] = 17
        with self.assertRaises(ValueError):
            encode(p)


if __name__ == "__main__":
    unittest.main()
