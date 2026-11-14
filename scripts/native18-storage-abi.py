#!/usr/bin/env python3
"""Compile zizhan UFS against the matched native storage PAC parent on the host.

Derives every parent method from the stock kernel and real public source;
rejects mismatches before emitting adapters. No device I/O or boot image.
"""
import argparse
from datetime import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tomllib

REPO = Path(__file__).resolve().parents[1]
PROFILE = tomllib.loads((REPO / "configs/zizhan-ios18e-inputs.toml").read_text())
STORAGE_COMMIT = "1b96f8f04b9dff4dc0277ac43625bf694e62c5aa"


def revision(path):
    return subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("native-source", "tree", "input-root", "prelink-kit", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--storage-source", type=Path,
                        default=REPO / "refer/ipados18/IOStorageFamily-323")
    parser.add_argument("--parent-interfaces", action="store_true",
                        help="Also derive GPU/AES parent metadata from host-only interface objects")
    args = parser.parse_args()
    source, tree, out = args.native_source.resolve(), args.tree.resolve(), args.out.resolve()
    storage, kit = args.storage_source.resolve(), args.prelink_kit.resolve()
    if revision(source) != PROFILE["source"]["commit"]:
        parser.error("Wrong native reference revision")
    if revision(kit) != PROFILE["prelink"]["commit"] or revision(storage) != STORAGE_COMMIT:
        parser.error("Wrong prelink/storage reference revision")
    for ref in (source, kit, storage):
        if subprocess.check_output(["git", "-C", str(ref), "status", "--porcelain"], text=True).strip():
            parser.error("Dirty reference source: " + str(ref))
    if out.exists() or out == REPO or REPO in out.parents:
        parser.error("--out must be a new machine-local directory")
    inputs = json.loads((args.input_root / "native-inputs.json").read_text())
    if inputs["profile"] != PROFILE["ipsw"] or inputs["identity"].get("BuildNumber") != "22G100":
        parser.error("Input receipt is not the selected native restore")
    kernel = (args.input_root / inputs["kernelcache"]["path"]).resolve()
    if hashlib.sha256(kernel.read_bytes()).hexdigest() != inputs["kernelcache"]["sha256"]:
        parser.error("Kernelcache differs from the input receipt")
    framework = tree / ".cache/fakeroot/System/Library/Frameworks/Kernel.framework/Versions/A"
    if not (framework / "PrivateHeaders/IOKit/IOService.h").is_file():
        parser.error("Build the isolated native core SDK first")
    out.mkdir(parents=True)
    include = out / "include/IOKit/storage"
    include.mkdir(parents=True)
    for path in storage.glob("*.h"):
        shutil.copyfile(path, include / path.name)
    sdk = subprocess.check_output(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"], text=True).strip()
    macsdk = Path(subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip())
    shutil.copyfile(macsdk / "usr/include/TargetConditionals.h", out / "include/TargetConditionals.h")
    command = ["xcrun", "--sdk", "iphoneos", "clang++", "-target", "arm64e-apple-ios18.0",
               "-isysroot", sdk, "-mkernel", "-ffreestanding", "-fno-builtin", "-fno-exceptions",
               "-fno-rtti", "-std=gnu++17", "-fblocks", "-O2", "-DXNU_PONG_NATIVE_PAC=1",
               "-DKERNEL=1", "-DKERNEL_PRIVATE=1", "-DXNU_KERNEL_PRIVATE=1",
               "-I" + str(out / "include"), "-I" + str(framework / "PrivateHeaders"),
               "-I" + str(framework / "Headers"), "-Wno-deprecated-declarations",
               "-Wno-c99-designator", "-xc++", str(storage / "IOBlockStorageDevice.cpp"),
               "-c", "-o", str(out / "reference.o")]
    record = {"started_at": datetime.now().astimezone().isoformat(),
              "scope": "host compile/PAC ancestry only; not storage or persistence qualification",
              "device_access": False, "bootable": False, "source_revision": revision(REPO),
              "source_dirty": subprocess.check_output(["git", "-C", str(REPO), "status", "--short"], text=True).splitlines(),
              "reference_commits": {"native": revision(source), "kit": revision(kit), "storage": STORAGE_COMMIT},
              "commands": []}
    def run(cmd, name):
        record["commands"].append(cmd)
        (out / "receipt.json").write_text(json.dumps(record, indent=2) + "\n")
        with (out / (name + ".log")).open("w") as log:
            result = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(name + " failed; inspect " + str(out / (name + ".log")))
    (out / "compile-command.json").write_text(json.dumps(command, indent=2) + "\n")
    run(command, "reference-compile")
    # The upstream derive script expects <root>/kits/XnuPrelink.
    (out / "kit-root/kits").mkdir(parents=True)
    (out / "kit-root/kits/XnuPrelink").symlink_to(kit, target_is_directory=True)
    parent = out / "native-parent.json"
    run([sys.executable, str(source / "scripts/derive-pong-fileset-parent.py"),
         "--unixnu", str(out / "kit-root"), "--kernel", str(kernel),
         "--object", str(out / "reference.o"), "--out", str(parent),
         "--classname", "IOBlockStorageDevice", "--entry", "com.apple.iokit.IOStorageFamily"], "derive-parent")
    metadata = json.loads(parent.read_text())
    if not metadata.get("abi_compatible"):
        raise RuntimeError("Native parent PAC signatures differ; no adapters emitted")
    objects = []
    for name in ("NativeUfs", "PongUfsTransport"):
        obj = out / (name + ".o")
        cmd = command.copy()
        cmd[cmd.index("-xc++") + 1] = str(REPO / "drivers/NativeUfs" / (name + ".cpp"))
        cmd[cmd.index("-o") + 1] = str(obj)
        cmd += ["-I" + str(tree / ".cache/xnu-work/EXTERNAL_HEADERS"),
                "-DXNU_PONG_UFS_DATA=1", "-DXNU_PONG_EARLY_TRACE=0"]
        run(cmd, name)
        objects.append(obj)
    module_spec = importlib.util.spec_from_file_location("native_parent_bindings", source / "scripts/pong_fileset_parent_bindings.py")
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    owned = module.validate_owned(parent, objects[1], "PongUfsBlock")
    objects += module.build(parent, kernel, kit, objects, out / "bindings", dict(os.environ))
    archive = out / "libZizhanNativeUfs18e.a"
    run(["xcrun", "libtool", "-static", "-o", str(archive), *map(str, objects)], "archive")
    parent_interfaces = {}
    if args.parent_interfaces:
        parents = out / "parent-references"
        parents.mkdir()
        interfaces = [
            ("IOAcceleratorES", "com.apple.iokit.IOGPUFamily", "PongAdreno/PongIOAcceleratorABI.h",
             "OSDefineMetaClassAndAbstractStructors(IOAcceleratorES, IOService)\n"),
            ("IOAESAccelerator", "com.apple.iokit.IOCryptoAcceleratorFamily", "SoftwareAES/OriginalAESABI.h",
             "OSDefineMetaClassAndStructors(IOCryptoAccelerator, IOService)\n"
             "OSDefineMetaClassAndStructors(IOAESAccelerator, IOCryptoAccelerator)\n"),
        ]
        for classname, entry, header, definitions in interfaces:
            reference = parents / (classname + "-reference.cpp")
            reference.write_text("// Host-only interface metadata; never linked into a device.\n"
                                 '#include "' + str(source / "drivers" / header) + '"\n' + definitions)
            cmd = command.copy()
            cmd[cmd.index("-xc++") + 1] = str(reference)
            obj = reference.with_suffix(".o")
            cmd[cmd.index("-o") + 1] = str(obj)
            run(cmd, classname + "-reference")
            output = parents / (classname + ".json")
            run([sys.executable, str(source / "scripts/derive-pong-fileset-parent.py"),
                 "--unixnu", str(out / "kit-root"), "--kernel", str(kernel),
                 "--object", str(obj), "--out", str(output),
                 "--classname", classname, "--entry", entry], classname + "-derive")
            result = json.loads(output.read_text())
            if not result.get("abi_compatible"):
                raise RuntimeError("Native interface PAC mismatch: " + classname)
            parent_interfaces[classname] = {"slots": result["method_slots"], "abi_compatible": True,
                                            "scope": "host interface metadata, not driver qualification"}
    record.update(finished_at=datetime.now().astimezone().isoformat(), result="HOST_VERIFIED",
                  owned_subclass=owned, parent_slots=metadata["method_slots"],
                  parent_interfaces=parent_interfaces,
                  archive={"bytes": archive.stat().st_size, "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()})
    (out / "receipt.json").write_text(json.dumps(record, indent=2) + "\n")
    print("HOST zizhan UFS archive compiled with matching native PAC ancestry:", archive)


if __name__ == "__main__":
    main()
