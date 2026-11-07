#!/usr/bin/env python3
"""Exercise actual 22H374 job staging under explicit and inherited policies."""
import importlib.util
import json
from pathlib import Path
import plistlib
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "userland"))
import service_policy18 as policy

spec = importlib.util.spec_from_file_location("userland18", REPO / "userland/build-ipados18.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)
LABEL = "com.apple.fairplayd.A2"
JOBPATH = "System/Library/LaunchDaemons/" + LABEL + ".plist"


class ServicePolicyTests(unittest.TestCase):
    def test_default_inheritance_and_explicit_choice(self):
        default = policy.resolve(None, build.EXCLUDED18)
        self.assertIn(LABEL, default["excluded_jobs"])
        explicit = policy.resolve(REPO / "configs/zizhan-22h374-os370-services.json", build.EXCLUDED18)
        self.assertNotIn(LABEL, explicit["excluded_jobs"])
        self.assertEqual(policy.resolve(None, build.EXCLUDED18, explicit), explicit)
        self.assertNotEqual(policy.identity(default), policy.identity(explicit))

    def test_wrong_build_unknown_keys_invalid_labels(self):
        valid = policy.resolve(None, {})
        for change in ({"build": "20G81"}, {"unknown": True},
                       {"excluded_jobs": {"../job": "reason"}},
                       {"excluded_jobs": {LABEL: ""}}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                policy.validate({**valid, **change})

    def test_image_builder_refuses_stale_policy_identity(self):
        selected = policy.resolve(REPO / "configs/zizhan-22h374-os370-services.json", build.EXCLUDED18)
        report = {"service_policy": selected, "service_policy_sha256": policy.identity(selected)}
        plan = {"service_policy": selected, "service_policy_sha256": policy.identity(selected)}
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / "jobs-report.json").write_text(json.dumps(report))
            build.verify_service_policy_plan(plan, out)
            report["service_policy_sha256"] = "0" * 64
            (out / "jobs-report.json").write_text(json.dumps(report))
            with self.assertRaisesRegex(RuntimeError, "differs from build plan"):
                build.verify_service_policy_plan(plan, out)
            report["service_policy_sha256"] = policy.identity(selected)
            (out / "jobs-report.json").write_text(json.dumps(report))
            plan["service_policy_sha256"] = "0" * 64
            with self.assertRaisesRegex(RuntimeError, "build plan service policy hash"):
                build.verify_service_policy_plan(plan, out)
            with self.assertRaisesRegex(RuntimeError, "service job report is missing"):
                build.verify_service_policy_plan(plan, out / "missing")

    def test_source_identity_is_required_to_match_before_image_edits(self):
        selected = policy.resolve(REPO / "configs/zizhan-22h374-os370-services.json", build.EXCLUDED18)
        source = {"worktree_sha256": "candidate-source"}
        plan = {"service_policy": selected, "service_policy_sha256": policy.identity(selected),
                "source_identity": source}
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / "jobs-report.json").write_text(json.dumps({
                "service_policy": selected, "service_policy_sha256": policy.identity(selected)}))
            build.verify_service_policy_plan(plan, out, current_source=source)
            with self.assertRaisesRegex(RuntimeError, "source worktree differs"):
                build.verify_service_policy_plan(plan, out, current_source={"worktree_sha256": "changed"})

    def test_prepared_images_are_checked_against_source_and_converted_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            source_os, source_data = out / "source-os", out / "source-data"
            prepared_os, prepared_data = out / "pong-ipados18-system.cdr", out / "pong-ipados18-data.cdr"
            source_os.write_bytes(b"OS source")
            source_data.write_bytes(b"sparse Data source")
            prepared_os.write_bytes(b"OS source")
            prepared_data.write_bytes(b"converted UDRW Data")
            digest = lambda path: build.file_sha(path)
            plan = dict(os_source=str(source_os), data_source=str(source_data),
                        input_images={"os_source": {"sha256": digest(source_os)},
                                      "data_source": {"sha256": digest(source_data)}},
                        prepared_images={"os_source": digest(prepared_os),
                                         "data_source": digest(prepared_data)})
            build.verify_prepared_images(plan, out)
            prepared_data.write_bytes(b"stale conversion")
            with self.assertRaisesRegex(RuntimeError, "prepared image differs"):
                build.verify_prepared_images(plan, out)
            prepared_data.write_bytes(b"converted UDRW Data")
            source_data.write_bytes(b"changed sparse source")
            with self.assertRaisesRegex(RuntimeError, "source input image changed"):
                build.verify_prepared_images(plan, out)

    def test_actual_job_builder_respects_both_policies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "source"
            cache = root / "System/Library/xpc/launchd.plist"
            cache.parent.mkdir(parents=True)
            cache.write_bytes(plistlib.dumps({"VersionNumber": 7, "LaunchDaemons": {}}))
            daemon = root / JOBPATH
            daemon.parent.mkdir(parents=True)
            daemon.write_bytes(plistlib.dumps({"Label": LABEL, "Program": "/usr/libexec/fairplayd.A2",
                                              "KeepAlive": True}))
            executable = root / "usr/libexec/fairplayd.A2"
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b"source-executable")
            for name, selected, expected in (
                    ("legacy", policy.resolve(None, build.EXCLUDED18), False),
                    ("presence-control", policy.resolve(None, {}), True)):
                out = Path(directory) / name
                out.mkdir()
                build.jobs(root, root, out / "overlay", out, service_policy=selected)
                report = json.loads((out / "jobs-report.json").read_text())
                self.assertEqual(report["service_policy_sha256"], policy.identity(selected))
                self.assertEqual(report["service_policy"], selected)
                cached = plistlib.loads((out / "overlay/System/Library/xpc/launchd.plist").read_bytes())
                self.assertEqual("/" + JOBPATH in cached["LaunchDaemons"], expected)
                self.assertEqual((out / "overlay" / JOBPATH).exists(), expected)
            self.assertEqual(executable.read_bytes(), b"source-executable")


if __name__ == "__main__":
    unittest.main()
