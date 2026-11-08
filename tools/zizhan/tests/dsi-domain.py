#!/usr/bin/env python3
"""Execute production display-domain observer with synthetic MMIO replies."""
import os
import struct
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import importlib.util
import sys
import struct

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
spec = importlib.util.spec_from_file_location("adt", ROOT / "scripts/make-pong-adt-ipados18.py")
adt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adt)


class DsiDomain(unittest.TestCase):
    def test_reserved_dma_adt_lease_wire(self):
        adt_path = ROOT / "scripts/make-pong-adt-ipados18.py"
        spec = importlib.util.spec_from_file_location("pong_adt", adt_path)
        adt = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(adt)
        chosen = adt.node("chosen", [("pong-dsi-command", bytes(16))])
        tree = adt.node("device-tree", children=[chosen])
        parser_spec = importlib.util.spec_from_file_location(
            "adt_contract", ROOT / "scripts/pong_adt_contract.py")
        parser = importlib.util.module_from_spec(parser_spec)
        parser_spec.loader.exec_module(parser)
        value = parser.parse_adt(tree)["/chosen"]["pong-dsi-command"]
        self.assertEqual(struct.unpack("<QII", value), (0, 0, 0))

    def test_fixed_read_protocol_and_failure_gates(self):
        with tempfile.TemporaryDirectory(prefix="zizhan-dsi-domain-") as tmp:
            binary = str(Path(tmp) / "domain")
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined",
                            str(ROOT / "tools/zizhan/tests/dsi-domain.cpp"), "-o", binary],
                           check=True, timeout=30)
            subprocess.run([binary], check=True, timeout=10)

    def test_board_wire_shape_and_read_only_limits(self):
        d = json.loads((ROOT / "configs/zizhan-display-sm8475.json").read_text())["display_domain_observer"]
        wire = adt.encode_dsi_domain(d)
        self.assertEqual(len(wire), 40)
        self.assertEqual(struct.unpack("<Q8I", wire),
                         (0x15000000, 0x100000, 1, 3, 3, 0x2800, 0x402, 1, 0))
        for field, value in (("base", "0x15010000"), ("group", 5), ("bank", 4),
                             ("mask", "0x0"), ("register_bytes", 0x1000), ("version", 2)):
            with self.assertRaises(ValueError):
                adt.encode_dsi_domain({**d, field: value})


if __name__ == "__main__":
    unittest.main()
