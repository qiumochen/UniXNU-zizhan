#!/usr/bin/env python3
"""Apply host-verified native bootstrap/service/Data-seed overlays to a local copy.

Only an independent OS.sparseimage in an explicit candidate directory is
modified. No Data image, installed phone, physical storage or firmware inputs
are accepted. This remains an incomplete local candidate until closure/board
adaptation and device qualification succeed.
"""
import argparse
from datetime import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import shutil
import stat
import subprocess
import tomllib

ROOT = Path(__file__).resolve().parents[1]
PIN = tomllib.loads((ROOT / "configs/zizhan-ios18e-inputs.toml").read_text())["source"]


def sha(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(8 << 20), b""):
            result.update(block)
    return result.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("native-source", "candidate", "restore-os", "runtime", "data-seed", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    if os.geteuid():
        parser.error("Local APFS staging requires root ownership")
    source, candidate, out = args.native_source.resolve(), args.candidate.resolve(), args.out.resolve()
    image, original = candidate / "OS.sparseimage", args.restore_os.resolve()
    if subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip() != PIN["commit"]:
        parser.error("Wrong native source reference")
    if candidate == ROOT or ROOT in candidate.parents or image.is_symlink() or not image.is_file() or image.samefile(original):
        parser.error("Require an independent local OS sparseimage outside the public repository")
    if out.exists() or out == ROOT or ROOT in out.parents:
        parser.error("Use a new local operation receipt directory")
    runtime = json.loads((args.runtime / "receipt.json").read_text())
    seed = json.loads((args.data_seed / "receipt.json").read_text())
    if (runtime.get("build"), runtime.get("arch"), runtime.get("result")) != ("22G100", "arm64e", "HOST_VERIFIED"):
        parser.error("Native runtime is not host-verified")
    if seed.get("build") != "22G100" or seed.get("migration_state_synthesized") is not False:
        parser.error("Data seed is not the qualified native restore seed")
    manifest = args.data_seed / "overlay/pong/etc/var.skel"
    if sha(manifest) != seed["manifest_sha256"]:
        parser.error("Data seed manifest has changed")
    for row in runtime["files"]:
        if sha(args.runtime / row["name"]) != row["sha256"]:
            parser.error("Runtime binary differs from its verified build receipt")
    out.mkdir(parents=True)
    mount = out / "os-mount"
    mount.mkdir()
    report = {"started_at": datetime.now().astimezone().isoformat(), "source": PIN["commit"],
              "candidate": str(image), "restore_source": str(original), "phone_written": False,
              "data_image_modified": False, "bootable": False, "status": "RUNNING"}
    record = out / "receipt.json"
    record.write_text(json.dumps(report, indent=2) + "\n")
    result = plistlib.loads(subprocess.check_output(["hdiutil", "attach", "-nobrowse", "-owners", "on",
                                                   "-mountpoint", str(mount), "-plist", str(image)]))
    disk = next(row["dev-entry"] for row in result["system-entities"] if row.get("dev-entry") == "/dev/" + Path(row.get("dev-entry", "")).name and row.get("content-hint") == "GUID_partition_scheme") \
        if any(row.get("content-hint") == "GUID_partition_scheme" for row in result["system-entities"]) \
        else result["system-entities"][0]["dev-entry"]
    spec = importlib.util.spec_from_file_location("native_stage_support", source / "userland/build-ipados18.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    try:
        version = plistlib.loads((mount / "System/Library/CoreServices/SystemVersion.plist").read_bytes())
        if (version.get("ProductVersion"), version.get("ProductBuildVersion")) != ("18.6.2", "22G100"):
            raise RuntimeError("Local writable copy is not the selected native restore")
        info = plistlib.loads(subprocess.check_output(["diskutil", "info", "-plist", str(mount)]))
        if not info.get("GlobalPermissionsEnabled") or info.get("WritableVolume") is not True:
            raise RuntimeError("Candidate must be writable with native ownership")
        if (mount / "pong").exists():
            raise RuntimeError("Candidate already has a runtime; retain it and use a new copy")
        stock = mount / "pong/stock-config"
        stock.mkdir(parents=True)
        for relative in ("System/Library/LaunchDaemons", "System/Library/NanoLaunchDaemons",
                         "System/Library/LaunchAngels", "System/Library/xpc/launchd.plist",
                         "System/Library/xpc/launchd.plist.sig"):
            path = mount / relative
            if os.path.lexists(path):
                shutil.move(str(path), str(stock / path.name))
        shutil.copy2(mount / "sbin/launchd", stock / "launchd.original")
        module.merge_tree(args.runtime / "overlay", mount)
        module.merge_tree(args.data_seed / "overlay", mount)
        for base in (mount / "pong", mount / "sbin/launchd", mount / "sbin/launchd.ios",
                     mount / "System/Library/LaunchDaemons", mount / "System/Library/xpc/launchd.plist"):
            paths = [base, *base.rglob("*")] if base.is_dir() else [base]
            for path in paths:
                os.chown(path, 0, 0, follow_symlinks=False)
                if not path.is_symlink() and path.is_dir():
                    path.chmod(0o755)
        for path in (mount / "System/Library/LaunchDaemons").glob("*.plist"):
            path.chmod(0o644)
        (mount / "System/Library/xpc/launchd.plist").chmod(0o644)
        for relative in ("sbin/launchd", "sbin/launchd.ios", "pong/init", "pong/libpong-ui18.dylib"):
            path = mount / relative
            path.chmod(0o755)
            subprocess.run(["codesign", "--verify", "--strict", str(path)], check=True)
        report.update(status="LOCAL_STAGED", volume_uuid=info["VolumeUUID"], build="22G100",
                      manifest_sha256=sha(mount / "pong/etc/var.skel"),
                      binaries={relative: sha(mount / relative) for relative in
                                ("sbin/launchd", "sbin/launchd.ios", "pong/init", "pong/libpong-ui18.dylib")},
                      pending=["stock kext import closure", "board/display/input/GPU/network integration",
                               "full native catalog/services", "isolated Data install plan", "device qualification"])
    except BaseException as error:
        report.update(status="FAIL", reason=str(error))
        raise
    finally:
        subprocess.run(["hdiutil", "detach", disk], check=True)
        report["finished_at"] = datetime.now().astimezone().isoformat()
        record.write_text(json.dumps(report, indent=2) + "\n")
    print("Local native OS overlay staged and strict binary signatures verified; no phone/Data write")


if __name__ == "__main__":
    main()
