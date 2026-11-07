#!/usr/bin/env python3
"""Regress completion races without a phone or real sleeping."""
import importlib.util
from pathlib import Path
import subprocess
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location(
    "wait_android", Path(__file__).resolve().parents[1] / "mac/wait-android.py")
wait = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wait)


class Clock:
    value = 0

    def now(self):
        return self.value

    def sleep(self, seconds):
        assert seconds > 0
        self.value += seconds


class WaitAndroidTests(unittest.TestCase):
    def execute(self, handler, timeout=12):
        clock = Clock()
        calls = []

        def run(args, **kwargs):
            self.assertGreater(kwargs["timeout"], 0)
            self.assertLessEqual(kwargs["timeout"], min(6, timeout - clock.now()))
            calls.append(args[1:])
            return handler(args[1:], clock)

        result = wait.wait_android(timeout=timeout, interval=3, run=run,
                                   clock=clock.now, sleep=clock.sleep)
        return result, calls

    def test_adb_ready_precedes_framework_complete(self):
        replies = iter(["", "0", "1"])

        def handler(args, _):
            value = (next(replies) if args[-1] == "sys.boot_completed" else
                     "zizhan" if args[-1] == "ro.product.device" else "device")
            return SimpleNamespace(returncode=0, stdout=value)

        result, calls = self.execute(handler)
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["attempts"], 3)
        self.assertEqual(result["elapsed_seconds"], 6)
        self.assertEqual(len(calls), 9)

    def test_foreign_product_is_terminal_and_not_completed(self):
        result, calls = self.execute(lambda args, _: SimpleNamespace(
            returncode=0, stdout="device" if args == ["get-state"] else "pong"))
        self.assertEqual(result["reason"], "unexpected-product")
        self.assertFalse(any(call[-1] == "sys.boot_completed" for call in calls))

    def test_missing_transport_is_bounded_and_read_only(self):
        result, calls = self.execute(lambda *_: SimpleNamespace(returncode=1, stdout=""))
        self.assertEqual(result["reason"], "android-completion-timeout")
        self.assertEqual(result["elapsed_seconds"], 12)
        self.assertTrue(all(call == ["get-state"] for call in calls))

    def test_command_timeout_is_not_success(self):
        def handler(args, clock):
            clock.value += min(6, 12 - clock.now())
            raise subprocess.TimeoutExpired(args, 6)

        result, _ = self.execute(handler)
        self.assertEqual(result["status"], "FAIL")
        self.assertLessEqual(result["elapsed_seconds"], 12)

    def test_completion_after_global_deadline_is_not_success(self):
        def handler(args, clock):
            if args[-1] == "sys.boot_completed":
                clock.value = 12
            return SimpleNamespace(returncode=0, stdout=(
                "device" if args == ["get-state"] else
                "zizhan" if args[-1] == "ro.product.device" else "1"))

        result, _ = self.execute(handler)
        self.assertEqual(result["status"], "FAIL")
        self.assertEqual(result["elapsed_seconds"], 12)

    def test_invalid_time_limits_rejected_before_queries(self):
        for timeout in (0, -1, 301, float("inf"), float("nan")):
            with self.subTest(timeout=timeout), self.assertRaises(ValueError):
                wait.wait_android(timeout=timeout, run=lambda *_: self.fail("queried"))


if __name__ == "__main__":
    unittest.main()
