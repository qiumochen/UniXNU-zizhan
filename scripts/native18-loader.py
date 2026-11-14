#!/usr/bin/env python3
"""Prepare and compile a native-format loader while retaining zizhan ownership.

Transplants only the reviewed original arm64e/fileset parser and PAC ISA gate
into a copy of the maintained zizhan loader. Keeps panel pools, reset watchdog,
RAM ownership, failure log, trampoline and ADT handling. No device operations.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]
PIN = tomllib.loads((ROOT / "configs/zizhan-ios18e-inputs.toml").read_text())["source"]


def function(text, name):
    start = text.index("STATIC\nEFI_STATUS\n" + name + " ")
    brace = text.index("{", start)
    depth = 0
    for offset in range(brace, len(text)):
        if text[offset] == "{":
            depth += 1
        elif text[offset] == "}":
            depth -= 1
            if depth == 0:
                return start, offset + 1, text[start:offset + 1]
    raise RuntimeError("Unterminated loader parser")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("native-source", "out", "edk2", "package", "llvm"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    source, out = args.native_source.resolve(), args.out.resolve()
    if subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip() != PIN["commit"]:
        parser.error("Wrong original native source revision")
    loader = source / "drivers/DarwinBoot24"
    gitlink = subprocess.check_output(["git", "-C", str(source), "ls-tree", "HEAD", "drivers/DarwinBoot24"], text=True).split()[2]
    if subprocess.check_output(["git", "-C", str(loader), "rev-parse", "HEAD"], text=True).strip() != gitlink:
        parser.error("Native loader differs from its pinned gitlink")
    if subprocess.check_output(["git", "-C", str(loader), "status", "--porcelain"], text=True).strip():
        parser.error("Native loader source has changes; retain it and choose a clean reference")
    if out.exists() or out == ROOT or ROOT in out.parents:
        parser.error("--out must be a new machine-local directory outside the public repository")
    destination = out / "source"
    shutil.copytree(ROOT / "drivers/DarwinBoot24", destination,
                    ignore=shutil.ignore_patterns(".git", "__pycache__"))
    # PongAutoReset uses the device's shared, tested SPMI reset core through
    # its original relative include. Keep that sibling contract in the copy.
    sibling = out / "ArmDTPlatform"
    sibling.mkdir()
    shutil.copyfile(ROOT / "drivers/ArmDTPlatform/BootResetCore.h", sibling / "BootResetCore.h")
    relative = "DarwinBootPkg/Driver/DarwinBoot.c"
    device = destination / relative
    old = device.read_text()
    native = (loader / relative).read_text()
    begin, end, _ = function(old, "ParseKernel")
    _, _, replacement = function(native, "ParseKernel")
    changed = old[:begin] + replacement + old[end:]
    marker = "  UINT64   Entry;\n} KERNEL;"
    if changed.count(marker) != 1:
        raise RuntimeError("Review changed zizhan kernel parser contract")
    changed = changed.replace(marker, "  UINT64   Entry;\n  BOOLEAN  Arm64e;\n} KERNEL;")
    marker = "  Status = ParseKernel (Image, ImageSize, &Kernel);\n  if (EFI_ERROR (Status)) { goto Fail; }\n"
    if changed.count(marker) != 1:
        raise RuntimeError("Review changed kernel parse/PAC qualification boundary")
    pac = '''  // zizhan native target: CPU ISA is necessary; XNU still qualifies keys and every CPU.
  if (Kernel.Arm64e) {
    UINT64 Isar1 = READ_SYSREG (id_aa64isar1_el1);
    BOOLEAN AddressPac = ((Isar1 >> 4) & 0xf) != 0 || ((Isar1 >> 8) & 0xf) != 0;
    BOOLEAN GenericPac = ((Isar1 >> 24) & 0xf) != 0 || ((Isar1 >> 28) & 0xf) != 0;
    Stage = L"ARM64E PAC ISA";
    Print (L"ZIZHAN_NATIVE: arm64e PAC ISA ISAR1=%lx; XNU owns key qualification\\n", Isar1);
    if (!AddressPac || !GenericPac) { Status = EFI_UNSUPPORTED; goto Fail; }
  }
'''
    changed = changed.replace(marker, marker + pac)
    for anchor in ("PANEL_POOL_PAGES 32768", "PongAutoReset.h", "mRamAllowedOffset", "BOOTFAIL.TXT"):
        if anchor not in changed:
            raise RuntimeError("Missing retained zizhan ownership/return contract: " + anchor)
    device.write_text(changed)
    report = {"started_at": datetime.now().astimezone().isoformat(),
              "native_source": PIN["commit"], "native_loader": gitlink,
              "device_revision": subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip(),
              "scope": "host loader build only; no fileset/boot/phone qualification", "device_access": False,
              "source_before_sha256": hashlib.sha256(old.encode()).hexdigest(),
              "source_after_sha256": hashlib.sha256(changed.encode()).hexdigest(),
              "native_parser_sha256": hashlib.sha256(replacement.encode()).hexdigest(),
              "retained": ["128MiB panel pool", "additional RAM ownership", "PongAutoReset", "BOOTFAIL log", "existing ADT", "existing MMU-off trampoline"]}
    record = out / "native-loader.json"
    record.write_text(json.dumps(report, indent=2) + "\n")
    if args.prepare_only:
        print(destination)
        return
    command = [sys.executable, str(ROOT / "scripts/build-darwinboot24-macos.py"),
               "--source", str(destination), "--output-dir", str(out / "build"),
               "--export-dir", str(out / "artifacts"), "--edk2", str(args.edk2.resolve()),
               "--package", str(args.package.resolve()), "--llvm", str(args.llvm.resolve())]
    report["command"] = command
    with (out / "build.log").open("w") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
    report.update(returncode=result.returncode, finished_at=datetime.now().astimezone().isoformat())
    binary = out / "artifacts/DarwinBoot24.efi"
    if binary.exists():
        report["efi"] = {"bytes": binary.stat().st_size, "sha256": hashlib.sha256(binary.read_bytes()).hexdigest()}
    record.write_text(json.dumps(report, indent=2) + "\n")
    if result.returncode:
        raise SystemExit("Native loader build failed; inspect " + str(out / "build.log"))
    print("HOST native-format zizhan loader compiled:", binary)


if __name__ == "__main__":
    main()
