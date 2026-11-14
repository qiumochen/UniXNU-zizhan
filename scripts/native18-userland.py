#!/usr/bin/env python3
"""Compile and verify the native 22G100 bootstrap/UI in an isolated directory.

This compiles existing native adapters only. It does not curate services,
modify OS/Data, start a phone or claim rendering/network/audio capabilities.
"""
import argparse
from datetime import datetime
import hashlib
import importlib.util
import json
import plistlib
import shutil
from pathlib import Path
import struct
import subprocess
import tomllib

ROOT = Path(__file__).resolve().parents[1]
PIN = tomllib.loads((ROOT / "configs/zizhan-ios18e-inputs.toml").read_text())["source"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-source", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--developer-dir", type=Path,
                        default=Path("/Applications/Xcode.app/Contents/Developer"))
    parser.add_argument("--os-root", type=Path,
                        help="Optional read-only matching OS mount for service/namespace staging")
    parser.add_argument("--systemos-root", type=Path,
                        help="Matching read-only SystemOS mount; required with --os-root")
    parser.add_argument("--app-root", type=Path,
                        help="Optional matching AppOS mount for service availability checks")
    parser.add_argument("--staged-app-seed", action="store_true",
                        help="Build the scoped native removable-app seed reader adaptation")
    args = parser.parse_args()
    source, out = args.native_source.resolve(), args.out.resolve()
    if bool(args.os_root) != bool(args.systemos_root):
        parser.error("Both matching OS and SystemOS mounts are required")
    if args.app_root and not args.os_root:
        parser.error("--app-root requires the matching OS/SystemOS pair")
    if args.os_root:
        for mounted in (args.os_root, args.systemos_root):
            version = plistlib.loads((mounted / "System/Library/CoreServices/SystemVersion.plist").read_bytes())
            if (version.get("ProductVersion"), version.get("ProductBuildVersion")) != ("18.6.2", "22G100"):
                parser.error("Service inputs must both be the selected 22G100 restore")
    if subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip() != PIN["commit"]:
        parser.error("Wrong native source revision")
    if subprocess.check_output(["git", "-C", str(source), "status", "--porcelain"], text=True).strip():
        parser.error("Native source checkout has changes")
    if out.exists() or out == ROOT or ROOT in out.parents:
        parser.error("Use a new output directory outside the device repository")
    spec = importlib.util.spec_from_file_location("native_build_userland", source / "userland/build-ipados18.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    out.mkdir(parents=True)
    adaptation = None
    if args.staged_app_seed:
        overlay = out / "source-overlay"
        shutil.copytree(source / "userland", overlay / "userland", ignore=shutil.ignore_patterns("__pycache__"))
        header = ROOT / "include/zizhan/Native18AppSeed.h"
        shutil.copy2(header, overlay / "userland/Native18AppSeed.h")
        bootstrap = overlay / "userland/bootstrap18.c"
        text = bootstrap.read_text()
        old = 'data_path(columns[4])&&!strncmp(columns[5],"/pong/var-seed/",15)'
        new = 'data_path(columns[4])&&(!strncmp(columns[5],"/pong/var-seed/",15)||zizhan_native18_app_seed(path,columns[5],columns[4]))'
        if text.count(old) != 1:
            parser.error("Review changed native seed parser before adapting it")
        text = text.replace('#include "bootstrap_runtime.h"', '#include "bootstrap_runtime.h"\n#include "Native18AppSeed.h"')
        bootstrap.write_text(text.replace(old, new))
        module.PROJECT = overlay
        adaptation = {"scope": "numbered native app manifest to same relative staged_system_apps path only",
                      "header_sha256": hashlib.sha256(header.read_bytes()).hexdigest(),
                      "bootstrap_sha256": hashlib.sha256(bootstrap.read_bytes()).hexdigest()}
    report = {"started_at": datetime.now().astimezone().isoformat(), "reference": PIN,
              "build": "22G100", "arch": "arm64e", "device_access": False,
              "scope": "host bootstrap/UI build; no OS installation or native device evidence", "files": []}
    if adaptation:
        report["staged_app_seed"] = adaptation
    (out / "receipt.json").write_text(json.dumps(report, indent=2) + "\n")
    module.compile_init(out, args.developer_dir.resolve(), out / "init")
    module.compile_ui(out, args.developer_dir.resolve())
    if args.os_root:
        overlay = out / "overlay"
        (overlay / "pong/etc").mkdir(parents=True)
        report["services"] = module.jobs(args.os_root.resolve(), args.systemos_root.resolve(), overlay, out,
                                          args.app_root.resolve() if args.app_root else None)
        service_record = out / "jobs-report.json"
        service_metadata = json.loads(service_record.read_text())
        service_metadata["service_cache"]["source_build"] = "22G100"
        service_record.write_text(json.dumps(service_metadata, indent=2) + "\n")
        module.adapt_launchd18(args.os_root / "sbin/launchd", overlay / "sbin/launchd.ios", out)
        task_record = out / "launchd-config-update.json"
        task_metadata = json.loads(task_record.read_text())
        task_metadata["build"] = "22G100"
        task_metadata["source_scope"] = "Matching native restore launchd; shared structural task policy, no 22H374 instruction offsets"
        task_record.write_text(json.dumps(task_metadata, indent=2) + "\n")
        links = module.cryptex_link_plan(args.os_root, args.systemos_root)
        module.install_cryptex_links(overlay, links)
        shutil.copyfile(out / "init", overlay / "sbin/launchd")
        shutil.copyfile(out / "init", overlay / "pong/init")
        shutil.copyfile(out / "libpong-ui.dylib", overlay / "pong/libpong-ui18.dylib")
        report["namespace_links"] = len(links)
        report["overlay_scope"] = "native service catalog, structural launchd task configuration, cryptex links and compiled bootstrap/UI only; no Data, Metal or foreign hardware firmware"
    for filename in ("init", "libpong-ui.dylib"):
        path = out / filename
        data = path.read_bytes()
        magic, cpu, subtype, kind = struct.unpack_from("<4I", data)
        if (magic, cpu, subtype & 0xffffff) != (0xfeedfacf, 0x100000c, 2):
            raise RuntimeError("Runtime did not compile as arm64e: " + filename)
        subprocess.run(["codesign", "--verify", "--strict", str(path)], check=True)
        signature = subprocess.run(["codesign", "-d", "--verbose=4", str(path)], capture_output=True, text=True, check=True)
        if "Page size=4096" not in signature.stderr:
            raise RuntimeError("Runtime code-signature page size differs from the native 4KiB contract")
        (out / (filename + ".signature.txt")).write_text(signature.stderr)
        report["files"].append({"name": filename, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                                "cpu_subtype": subtype, "filetype": kind, "signature_page_bytes": 4096})
    report.update(finished_at=datetime.now().astimezone().isoformat(), result="HOST_VERIFIED")
    (out / "receipt.json").write_text(json.dumps(report, indent=2) + "\n")
    print("HOST native bootstrap and UI compiled, strict 4KiB signatures verified:", out)


if __name__ == "__main__":
    main()
