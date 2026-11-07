#!/usr/bin/env python3
"""zizhan BASE-01 replay: policy drift and malformed cache must not look green."""
import importlib.util
from pathlib import Path
import plistlib
import unittest

spec = importlib.util.spec_from_file_location(
    "audit", Path(__file__).resolve().parents[1] / "mac/userland-policy-audit.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
LABEL = "com.apple.fairplayd.A2"
PATH = "/System/Library/LaunchDaemons/" + LABEL + ".plist"
JOB = {"Label": LABEL, "Program": "/usr/libexec/fairplayd.A2", "KeepAlive": True}


class PolicyAuditTests(unittest.TestCase):
    def test_policy_read_without_executing_source(self):
        source = "raise RuntimeError('must not run')\nEXCLUDED18 = {'x': 'reason'}\n"
        self.assertEqual(audit.excluded_policy(source), {"x": "reason"})
        for value in ("EXCLUDED18 = dynamic()", "EXCLUDED18 = ['x']", "CORE = []"):
            with self.subTest(source=value), self.assertRaises(ValueError):
                audit.excluded_policy(value)

    def test_retained_excluded_job_detected(self):
        cached = {LABEL: {"path": PATH, "job": JOB}}
        result = audit.compare_policy({LABEL: "reason"}, cached,
                                      {LABEL: {"action": "boot"}}, {LABEL: JOB}, [LABEL])
        self.assertEqual(result[0]["result"], "FAIL")
        self.assertIn("rebuild excludes", result[0]["issues"][0])

    def test_consistently_excluded_job_passes(self):
        rows = audit.compare_policy({LABEL: "reason"}, {},
                                    {LABEL: {"action": "excluded"}}, {}, [LABEL])
        self.assertEqual(rows[0]["result"], "PASS")

    def test_present_in_overlay_but_not_image_is_failure(self):
        rows = audit.compare_policy({}, {}, {LABEL: {"action": "boot"}}, {LABEL: JOB}, [LABEL])
        self.assertEqual(rows[0]["result"], "FAIL")
        self.assertIn("overlay/image service presence differs", rows[0]["issues"])

    def test_changed_descriptor_or_missing_report_is_failure(self):
        cached = {LABEL: {"path": PATH, "job": JOB}}
        for overlay, plan in (({LABEL: {**JOB, "KeepAlive": False}}, {LABEL: {"action": "boot"}}),
                              ({LABEL: JOB}, {})):
            with self.subTest(overlay=overlay):
                rows = audit.compare_policy({}, cached, plan, overlay, [LABEL])
                self.assertEqual(rows[0]["result"], "FAIL")

    def test_empty_environment_is_equivalent_but_nonempty_is_not(self):
        cached = {LABEL: {"path": PATH, "job": {**JOB, "EnvironmentVariables": {}}}}
        rows = audit.compare_policy({}, cached, {LABEL: {"action": "boot"}}, {LABEL: JOB}, [LABEL])
        self.assertEqual(rows[0]["result"], "PASS")
        changed = {**JOB, "EnvironmentVariables": {"DYLD_INSERT_LIBRARIES": "/adapter.dylib"}}
        rows = audit.compare_policy({}, cached, {LABEL: {"action": "boot"}}, {LABEL: changed}, [LABEL])
        self.assertEqual(rows[0]["result"], "FAIL")

    def test_native_variants_preserved_and_paths_checked(self):
        raw = lambda jobs: plistlib.dumps({"VersionNumber": 7, "LaunchDaemons": jobs})
        self.assertEqual(audit.cache_jobs(raw({PATH: JOB}))[LABEL]["job"], JOB)
        duplicate = audit.cache_jobs(raw({PATH: JOB, PATH + ".copy": JOB}))
        self.assertEqual(len(duplicate[LABEL]["variants"]), 2)
        rows = audit.compare_policy({}, duplicate, {LABEL: {"action": "boot"}}, {LABEL: JOB}, [LABEL])
        self.assertEqual(rows[0]["result"], "FAIL")
        for jobs in ({"/outside/job.plist": JOB},
                     {"/System/Library/LaunchDaemons/../job.plist": JOB}):
            with self.subTest(jobs=jobs), self.assertRaises(ValueError):
                audit.cache_jobs(raw(jobs))


if __name__ == "__main__":
    unittest.main()
