#!/usr/bin/env python3
"""zizhan BASE-01: audit local OS/overlay/rebuild policy without mounting images.

Reports inconsistencies; never edits images, changes service policy or accesses
the phone. A local-image match is not evidence of the installed/booted OS.
"""
import argparse
import ast
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path, PurePosixPath
import plistlib
import subprocess
import sys

REPO = Path(__file__).resolve().parents[3]
CACHE = "/System/Library/xpc/launchd.plist"
DEFAULT_LABELS = ("com.apple.fairplayd.A2", "com.apple.thermalmonitord",
                  "com.apple.powerd", "com.apple.audiomxd", "com.apple.wifid",
                  "com.apple.backboardd", "com.apple.SpringBoard")
RUNTIME = {"libpong-ui.dylib": "/pong/libpong-ui18.dylib",
           "wifi-service18": "/pong/wifi-service18"}


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def file_digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(8 << 20), b""):
            value.update(block)
    return value.hexdigest()


def excluded_policy(source):
    """Read the actual rebuild policy without importing the build script."""
    for node in ast.parse(source).body:
        if isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == "EXCLUDED18" for target in node.targets):
            policy = ast.literal_eval(node.value)
            if not isinstance(policy, dict) or not all(
                    isinstance(k, str) and isinstance(v, str) for k, v in policy.items()):
                raise ValueError("Invalid EXCLUDED18 policy")
            return policy
    raise ValueError("EXCLUDED18 source policy not found")


def cache_jobs(raw):
    cache = plistlib.loads(raw)
    if cache.get("VersionNumber") != 7 or not isinstance(cache.get("LaunchDaemons"), dict):
        raise ValueError("Expected 22H374 Version 7 service cache")
    labels = {}
    for path, job in cache["LaunchDaemons"].items():
        relative = PurePosixPath(path)
        if relative.parent != PurePosixPath("/System/Library/LaunchDaemons") or ".." in relative.parts:
            raise ValueError("Unexpected service cache path")
        label = job.get("Label")
        if not isinstance(label, str) or not label:
            raise ValueError("Missing cached job label")
        entry = dict(path=path, job=job)
        if label in labels:
            # Native catalogs include variant-gated descriptors sharing a
            # label. Preserve them; do not silently overwrite a variant.
            previous = labels[label]
            if "variants" not in previous:
                previous["variants"] = [dict(path=previous["path"], job=previous["job"])]
            previous["variants"].append(entry)
        else:
            labels[label] = entry
    return labels


def canonical_job(job):
    # Native launchd receives the same environment for absent and empty
    # dictionaries. Preserve all nonempty variables and every other field.
    result = dict(job)
    if result.get("EnvironmentVariables") == {}:
        result.pop("EnvironmentVariables")
    return result


def compare_policy(excluded, jobs, planned, overlay_jobs, labels):
    checks = []
    for label in sorted(set(excluded) | set(labels)):
        cached = jobs.get(label)
        overlay = overlay_jobs.get(label)
        plan = planned.get(label)
        source_excluded = label in excluded
        image_present, overlay_present = cached is not None, overlay is not None
        expected = "excluded" if source_excluded else "not excluded by EXCLUDED18"
        issues = []
        if cached and "variants" in cached:
            issues.append("selected service has multiple descriptors; resolve native gates separately")
        if source_excluded and (image_present or overlay_present or
                                (plan is not None and plan.get("action") != "excluded")):
            issues.append("rebuild excludes service retained in candidate inputs")
        if not source_excluded and not image_present:
            issues.append("selected daily-use service absent from local image cache")
        if image_present != overlay_present:
            issues.append("overlay/image service presence differs")
        if cached and overlay and canonical_job(cached["job"]) != canonical_job(overlay):
            issues.append("overlay/image service descriptor differs")
        if plan is None:
            issues.append("missing jobs-report entry")
        elif (plan.get("action") == "excluded") == image_present:
            issues.append("jobs-report/image service presence differs")
        checks.append(dict(label=label, result="FAIL" if issues else "PASS",
                           rebuild_policy=expected, local_image_present=image_present,
                           overlay_present=overlay_present,
                           planned_action=plan.get("action") if plan else None, issues=issues))
    return checks


def load_job_report(path):
    rows = json.loads(path.read_text()).get("jobs", [])
    grouped = {}
    for row in rows:
        label = row["label"]
        if label in grouped:
            grouped[label] = dict(action="multiple-variants")
        else:
            grouped[label] = row
    return grouped


def source_identity():
    def git(*args):
        return subprocess.check_output(["git", *args], cwd=REPO, timeout=15)
    return dict(head=git("rev-parse", "HEAD").decode().strip(),
                tracked_diff_sha256=digest(git("diff", "--binary", "HEAD")),
                dirty=bool(git("status", "--porcelain")),
                audit_tool_sha256=file_digest(Path(__file__)),
                policy_module_sha256=file_digest(REPO / "userland/service_policy18.py"),
                policy_file="userland/build-ipados18.py")


def audit(prepared, layout_path, verify_image=False, service_policy_path=None):
    sys.path.insert(0, str(REPO / "tools/apfs"))
    import apfs
    layout = json.loads(layout_path.read_text())
    windows = [window for window in layout["windows"] if window["name"] == "OS"]
    if len(windows) != 1:
        raise ValueError("Layout must identify exactly one OS window")
    window = windows[0]
    image = Path(window["image"]).resolve()
    if image != (prepared / "pong-ipados18-system.cdr").resolve():
        raise ValueError("Layout OS image differs from --prepared")
    source = (REPO / "userland/build-ipados18.py").read_bytes()
    defaults = excluded_policy(source.decode())
    sys.path.insert(0, str(REPO / "userland"))
    from service_policy18 import resolve, identity
    policy = resolve(service_policy_path, defaults)
    excluded = policy["excluded_jobs"]
    container = apfs.Container(str(image))
    try:
        volume = apfs.pick_volume(container, None)

        def read(path):
            found = volume.lookup(path)
            if found is None:
                return None
            return volume.inode(found[0]).contents()

        raw = read(CACHE)
        if raw is None:
            raise ValueError("Local image has no service cache")
        jobs = cache_jobs(raw)
        overlay = cache_jobs((prepared / "overlay" / CACHE.lstrip("/")).read_bytes())
        job_report = json.loads((prepared / "jobs-report.json").read_text())
        checks = compare_policy(excluded, jobs, load_job_report(prepared / "jobs-report.json"),
                                {label: row["job"] for label, row in overlay.items()}, DEFAULT_LABELS)
        for label in sorted(set(DEFAULT_LABELS) | set(excluded)):
            for context, entry, reader in (
                    ("image", jobs.get(label), read),
                    ("overlay", overlay.get(label),
                     lambda path: (prepared / "overlay" / path.lstrip("/")).read_bytes()
                     if (prepared / "overlay" / path.lstrip("/")).is_file() else None)):
                if entry is None:
                    continue
                descriptor = reader(entry["path"])
                match = descriptor is not None and plistlib.loads(descriptor) == entry["job"]
                checks.append(dict(check=context + " plist/cache agreement", label=label,
                                   result="PASS" if match else "FAIL"))
        for filename, target in RUNTIME.items():
            content = read(target)
            local = prepared / filename
            image_hash = digest(content) if content is not None else None
            prepared_hash = file_digest(local) if local.is_file() else None
            match = image_hash is not None and prepared_hash == image_hash
            checks.append(dict(check="runtime identity", file=filename,
                               image_sha256=image_hash, prepared_sha256=prepared_hash,
                               result="PASS" if match else "FAIL"))
        actual_hash = file_digest(image) if verify_image else None
        if verify_image:
            checks.append(dict(check="layout OS sha256", result=(
                "PASS" if actual_hash == window["sha256"] else "FAIL")))
        return dict(schema_version=1, task="BASE-01",
                    status="FAIL" if any(row["result"] != "PASS" for row in checks) else "PASS",
                    observed_at=datetime.now(timezone.utc).isoformat(),
                    scope="Local image and prepared overlay only; no phone access or writes",
                    source=source_identity(), policy_source_sha256=digest(source),
                    selected_policy=policy, selected_policy_sha256=identity(policy),
                    policy_selection="explicit file" if service_policy_path else "legacy default",
                    prepared_policy_recorded="service_policy" in job_report,
                    prepared=str(prepared), layout=str(layout_path), image=str(image),
                    image_sha256=actual_hash, layout_expected_sha256=window["sha256"],
                    image_hash_verified=verify_image and actual_hash == window["sha256"],
                    cache_sha256=digest(raw),
                    image_jobs=sum(len(entry.get("variants", [entry])) for entry in jobs.values()),
                    overlay_jobs=sum(len(entry.get("variants", [entry])) for entry in overlay.values()),
                    variant_labels=sorted(label for label, entry in jobs.items() if "variants" in entry),
                    excluded_policy=excluded, checks=checks)
    finally:
        container.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepared", type=Path, required=True)
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="Private JSON report")
    parser.add_argument("--verify-image", action="store_true", help="Also hash entire local OS image")
    parser.add_argument("--service-policy", type=Path, help="Audit against an explicit rebuild policy")
    args = parser.parse_args()
    try:
        output = args.output.resolve()
        if output == REPO or REPO in output.parents:
            raise ValueError("Machine-specific reports must stay outside the public repository")
        result = audit(args.prepared.resolve(), args.layout.resolve(), args.verify_image, args.service_policy)
        output.write_text(json.dumps(result, indent=2) + "\n")
        failures = [row for row in result["checks"] if row["result"] != "PASS"]
        print(json.dumps(dict(status=result["status"], checks=len(result["checks"]),
                              failures=len(failures), image_jobs=result["image_jobs"])))
        for row in failures:
            print(row.get("label", row.get("file", row.get("check"))),
                  "; ".join(row.get("issues", [])) or row.get("check"))
        return 0 if result["status"] == "PASS" else 2
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
