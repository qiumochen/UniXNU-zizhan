#!/usr/bin/env python3
"""Execute production power transaction failure and race cases without hardware."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class PowerTransactionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="zizhan-power-model-")
        cls.binary = Path(cls.temp.name) / "model"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                        "-Werror", "-fsanitize=address,undefined",
                        str(ROOT / "tools/zizhan/tests/power-transaction.cpp"),
                        "-o", str(cls.binary)], check=True, timeout=30)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check(self, case):
        subprocess.run([str(self.binary), case], check=True, timeout=10)

    def test_unknown_capability_dependency_and_constraints(self):
        self.check("gates")

    def test_full_dependency_order_and_confirmed_resume(self):
        self.check("success")

    def test_reject_each_suspend_step_rollback_only_prepared(self):
        self.check("rejected")

    def test_cancel_inflight_duplicate_and_previous_generation(self):
        self.check("late-stale")

    def test_uncertain_failed_restore_and_invalid_reply_quarantine(self):
        self.check("uncertain")

    def test_deadline_keeps_inflight_ticket_and_blocks_new_cycle(self):
        self.check("deadline")

    def test_wake_epoch_or_new_constraint_aborts_entry(self):
        self.check("entry-race")


if __name__ == "__main__":
    unittest.main()
