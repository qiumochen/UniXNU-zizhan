#!/usr/bin/env python3
"""Panel parser/encoder/executor regressions; synthetic inputs only."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import struct

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location(
    "panel", ROOT / "tools/zizhan/mac/panel-power-inventory.py")
panel = importlib.util.module_from_spec(spec)
spec.loader.exec_module(panel)


def packet(kind=0x05, payload=b"\x28\0", wait=10):
    return bytes([kind, 0, 0, 0x40, wait, len(payload) >> 8, len(payload) & 255]) + payload


class PanelProgram(unittest.TestCase):
    def domain_fixture(self):
        def c(*values):
            return struct.pack(">" + "I" * len(values), *values)
        return {
            "/soc/artificial-domain": {"compatible": b"qcom,smmu_sde_unsec\0",
                "iommus": c(7, 0x1234, 2), "qcom,iommu-dma-addr-pool": c(0x10000, 0x20000)},
            "/soc/artificial-mmu": {"compatible": b"qcom,qsmmu-v500\0", "phandle": c(7),
                "#iommu-cells": c(2), "reg": c(0x8000, 0x1000),
                "qcom,handoff-smrs": c(0x1234, 2)},
        }

    def test_display_domain_keeps_live_translation_unknown(self):
        result = panel.address_domain(self.domain_fixture())
        self.assertTrue(result["firmware_handoff_match"])
        self.assertEqual(result["sid"], "0x1234")
        for key in ("stream_group_index", "context_bank", "translation_enabled"):
            self.assertIsNone(result[key])
        self.assertEqual(result["dma_address_mode"], "UNQUALIFIED")

    def test_display_domain_rejects_missing_duplicate_or_invalid_provider(self):
        for mode in range(5):
            nodes = self.domain_fixture()
            provider = nodes["/soc/artificial-mmu"]
            if mode == 0:
                del nodes["/soc/artificial-mmu"]
            elif mode == 1:
                nodes["/soc/duplicate"] = provider.copy()
            elif mode == 2:
                provider["#iommu-cells"] = struct.pack(">I", 1)
            elif mode == 3:
                nodes["/soc/artificial-domain"]["iommus"] = struct.pack(">III", 7, 0x8000, 0)
            else:
                provider["qcom,handoff-smrs"] = b"\0"
            with self.assertRaises(ValueError):
                panel.address_domain(nodes)

    def test_absent_matching_handoff_does_not_manufacture_a_group(self):
        nodes = self.domain_fixture()
        nodes["/soc/artificial-mmu"]["qcom,handoff-smrs"] = b""
        result = panel.address_domain(nodes)
        self.assertFalse(result["firmware_handoff_match"])
        self.assertIsNone(result["stream_group_index"])

    def test_actual_cpp_encoder_and_failure_paths(self):
        with tempfile.TemporaryDirectory(prefix="zizhan-panel-") as tmp:
            binary = str(Path(tmp) / "program")
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17",
                            "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                            str(ROOT / "tools/zizhan/tests/panel-program.cpp"), "-o", binary],
                           check=True, timeout=30)
            subprocess.run([binary], check=True, timeout=10)

    def test_short_type_is_preserved_with_dummy_byte(self):
        answer = panel.decode(packet())
        self.assertEqual(answer["packets"][0]["type"], 0x05)
        self.assertEqual(answer["packets"][0]["wire_bytes"], 4)
        self.assertEqual(answer["wait_ms"], 10)

    def test_fifo_boundary(self):
        self.assertEqual(panel.decode(packet(0x39, bytes(60)))["dma_packets"], 0)
        self.assertEqual(panel.decode(packet(0x39, bytes(61)))["dma_packets"], 1)

    def test_truncation_trailing_invalid_and_unknown_commands(self):
        valid = packet()
        for length in range(len(valid)):
            with self.assertRaises(ValueError):
                panel.decode(valid[:length])
        for raw in (valid + b"\0", packet(0x05, b"\x28\x01"),
                    packet(0x15, b"\x51"), packet(0x0a), packet(0x39, bytes(253))):
            with self.assertRaises(ValueError):
                panel.decode(raw)

    def test_foreign_board_missing_panel_and_link_mode(self):
        with self.assertRaisesRegex(ValueError, "Not a zizhan"):
            panel.describe({"/": {}}, "unused")
        with self.assertRaisesRegex(ValueError, "one L18"):
            panel.describe({"/": {"model": b"zizhan\0"}}, "unused")
        nodes = {"/": {"model": b"zizhan\0"}, "/" + panel.PANEL: {}}
        with self.assertRaisesRegex(ValueError, "link mode"):
            panel.describe(nodes, "unused")


if __name__ == "__main__":
    unittest.main()
