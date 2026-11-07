#!/usr/bin/env python3
"""Replay power board input inventory against a saved live FDT."""
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("power", ROOT / "tools/zizhan/mac/power-inventory.py")
power = importlib.util.module_from_spec(spec)
spec.loader.exec_module(power)


class PowerInventoryTests(unittest.TestCase):
    def setUp(self):
        self.dtb = ROOT.parent / "logs/daily-use-1106/entry-live.dtb"
        self.assertTrue(self.dtb.is_file())
        self.profile = power.describe(self.dtb.read_bytes())

    def test_identity_and_counts(self):
        self.assertEqual(self.profile["format"], "zizhan-sm8475-power-inputs-v1")
        self.assertEqual(len(self.profile["cpus"]), 8)
        self.assertEqual(len(self.profile["idle_states"]), 4)
        self.assertEqual(self.profile["psci"]["method"], "smc")
        self.assertEqual(len(self.profile["rsc"]), 2)
        self.assertEqual(len(self.profile["rtc"]), 1)
        self.assertEqual(len(self.profile["pdc"]), 1)

    def test_no_fake_execution_qualification(self):
        payload = json.dumps(self.profile)
        self.assertIn('"execution_state": "UNQUALIFIED"', payload)
        self.assertIn('"layout_state": "UNQUALIFIED"', payload)
        self.assertIn('"capability_state": "UNQUALIFIED"', payload)
        self.assertIn('"wake_state": "UNQUALIFIED"', payload)
        self.assertIn('"thermal_policy": null', payload)

    def test_cpu_domain_topology_is_explicit(self):
        self.assertEqual({row["affinity"] for row in self.profile["cpus"]},
                         {hex(i << 8) for i in range(8)})
        self.assertTrue(all(cpu["idle_states"] for cpu in self.profile["cpus"]))
        for domain in self.profile["domains"]:
            self.assertLessEqual(len(domain["parents"]), 4)

    def test_tcs_order_and_implicit_rtc_irq_are_preserved(self):
        pairs = self.profile["rsc"][0]["tcs_pairs"]
        self.assertEqual(pairs, [[2, 3], [0, 2], [1, 2], [3, 0], [4, 1]])
        irq = self.profile["rtc"][0]["interrupt"]
        self.assertIn("unresolved", irq["provider"])
        self.assertEqual(irq["scope"], "Provider-specific cells do not match inherited domain; parent/bus resolution required")


if __name__ == "__main__":
    unittest.main()
