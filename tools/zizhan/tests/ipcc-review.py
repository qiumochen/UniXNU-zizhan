#!/usr/bin/env python3
"""Positive and negative log controls; synthetic fixtures are not device data."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("ipcc_review", ROOT / "tools/zizhan/mac/ipcc-review.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixture():
    return "\n".join([
        "XNULOG v1 (zizhan klog: reason 1, synthetic-fixture)",
        "ZIZHAN_IPCC: PASS receiver-attached spi=261; no-DSP-start",
        "PongAdsp: shared zizhan IPCC routing; handler stats count doorbells",
        "PongAdsp: stage=13",
        "PongBattery: physical battery power source published fixture",
        *[f"ZIZHAN_IPCC_STATS: sample={i} at_ns={i*10000000000} irq={i*100} received={i*110} "
          f"delivered={i*110} unknown=0 budget=0 subscriptions=3 retries=0 adsp_callbacks={i*110} "
          f"per_id=0/{i*109}/{i}/0/0 atomic_fields=1 coherent_latch=0" for i in (1, 2)]])


class ReviewTests(unittest.TestCase):
    def test_observed_scope_stays_limited(self):
        report = module.review(fixture())
        self.assertEqual(report["status"], "PASS_SCOPED_OBSERVED_IRQ_DELIVERY")
        self.assertFalse(report["device_return_verified"])
        self.assertFalse(report["slpi_qualified"])
        self.assertFalse(report["lossless_delivery_qualified"])

    def test_missing_zero_and_static_stats_fail(self):
        for text in ("", fixture().split("ZIZHAN_IPCC_STATS:")[0],
                     fixture().replace("irq=200", "irq=0"),
                     fixture().replace("received=220", "received=110").replace("delivered=220", "delivered=110")):
            with self.subTest(text=text[-100:]):
                self.assertEqual(module.review(text)["status"], "FAIL")

    def test_fault_unknown_budget_and_malformed_fail(self):
        for text in (fixture()+"\npanic(cpu 0", fixture().replace("unknown=0", "unknown=1"),
                     fixture().replace("budget=0", "budget=1"),
                     fixture().replace("per_id=", "bad_field=")):
            self.assertEqual(module.review(text)["status"], "FAIL")

    def test_bad_time_sequence_missing_channel_fail(self):
        for text in (fixture().replace("sample=2", "sample=3"),
                     fixture().replace("at_ns=20000000000", "at_ns=15000000000"),
                     fixture().replace("per_id=0/218/2/0/0", "per_id=0/218/0/0/0")):
            self.assertEqual(module.review(text)["status"], "FAIL")

    def test_atomic_snapshot_does_not_require_cross_field_equalities(self):
        text = fixture().replace("delivered=220", "delivered=221")
        self.assertEqual(module.review(text)["status"], "PASS_SCOPED_OBSERVED_IRQ_DELIVERY")


if __name__ == "__main__":
    unittest.main()
