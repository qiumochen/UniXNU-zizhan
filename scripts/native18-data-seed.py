#!/usr/bin/env python3
"""Extract a matching native Data seed as local files and a bounded manifest.

Uses the original restore's inode owners/modes, never the installed Data.
Does not mount writable images, write the phone, or synthesize migration success.
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
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]
PIN = tomllib.loads((ROOT / "configs/zizhan-ios18e-inputs.toml").read_text())["source"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("native-source", "os-root", "os-image", "runtime", "out"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    source, out = args.native_source.resolve(), args.out.resolve()
    if subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip() != PIN["commit"]:
        parser.error("Wrong original native source revision")
    if subprocess.check_output(["git", "-C", str(source), "status", "--porcelain"], text=True).strip():
        parser.error("Native reference source has changes")
    if out.exists() or out == ROOT or ROOT in out.parents:
        parser.error("Use a new machine-local output directory")
    version = plistlib.loads((args.os_root / "System/Library/CoreServices/SystemVersion.plist").read_bytes())
    if (version.get("ProductVersion"), version.get("ProductBuildVersion")) != ("18.6.2", "22G100"):
        parser.error("Only a matching native 22G100 restore can seed Data")
    runtime = json.loads((args.runtime / "receipt.json").read_text())
    if (runtime.get("build"), runtime.get("arch"), runtime.get("result")) != ("22G100", "arm64e", "HOST_VERIFIED"):
        parser.error("Runtime is not the host-verified native build")
    out.mkdir(parents=True)
    local_runtime = out / "seed-runtime"
    preferences = local_runtime / "var-seed/mobile/Library/Preferences"
    preferences.mkdir(parents=True)
    for name in ("com.apple.purplebuddy.plist", "com.apple.purplebuddy.notbackedup.plist", "com.apple.springboard.plist"):
        shutil.copyfile(source / "userland/var/mobile/Library/Preferences" / name, preferences / name)
    # Keep read-only native APFS mounts/scratch under this new output, never
    # the reference checkout or installed guest. The library closes its mount.
    os.environ["PONG_APFS_WORKSPACE"] = str(out / "readonly-apfs")
    spec = importlib.util.spec_from_file_location("native_data_seed", source / "userland/prepare-ios18e.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    overlay = out / "overlay"
    # The retained helper's native APFS reader attaches the same image again.
    # macOS may reuse its existing mount while the reader assumes a new empty
    # mountpoint. Reuse the actual verified read-only/owners mount instead.
    mounted = args.os_root.resolve()
    info = plistlib.loads(subprocess.check_output(["diskutil", "info", "-plist", str(mounted)]))
    images = plistlib.loads(subprocess.check_output(["hdiutil", "info", "-plist"]))["images"]
    selected = [image for image in images if Path(image.get("image-path", "/missing")).resolve() == args.os_image.resolve()
                and any(Path(entity.get("mount-point", "/missing")).resolve() == mounted
                        for entity in image.get("system-entities", []))]
    if len(selected) != 1 or info.get("WritableVolume") is not False or info.get("WritableMedia") is not False or not info.get("GlobalPermissionsEnabled"):
        raise RuntimeError("Require the exact supplied image mounted read-only with ownership")
    sys.path.insert(0, str(source / "tools/apfs"))
    import apfs
    from native import Volume
    original_container = apfs.Container
    class ExistingReadonlyContainer:
        def __init__(self, path):
            if Path(path).resolve() != args.os_image.resolve():
                raise RuntimeError("Unexpected Data source image")
            self.volume = Volume(mounted, info["DeviceNode"], info)
        def volumes(self):
            return [self.volume]
        def close(self):
            pass  # The caller owns this existing read-only mount.
    apfs.Container = ExistingReadonlyContainer
    try:
        result = module.stage_native_data_seed(args.os_image.resolve(), mounted, local_runtime, overlay, out)
    finally:
        apfs.Container = original_container
    manifest = overlay / "pong/etc/var.skel"
    data = manifest.read_bytes()
    if b"com.apple.migration.plist" in data:
        raise RuntimeError("DataMigration owns its results; no synthetic completion seed is permitted")
    receipt = {"started_from": PIN["commit"], "finished_at": datetime.now().astimezone().isoformat(),
               "build": "22G100", "scope": "local native Data seed only; no writable mount or phone access",
               "result": result, "phone_written": False, "migration_state_synthesized": False,
               "manifest_sha256": hashlib.sha256(data).hexdigest()}
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("Native Data seed extracted:", result["entries"], "entries,", result["template_bytes"], "bytes")


if __name__ == "__main__":
    main()
