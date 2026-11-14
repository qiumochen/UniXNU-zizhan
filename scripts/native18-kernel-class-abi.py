#!/usr/bin/env python3
"""Derive core C++ identities from actual compiled kernel-object PAC tables.

Uses complete vtable extent, every method discriminator, and original class
name/metaclass registration. Mismatches/ambiguities remain refused. Objects
come from the real source kernel build, not replacement hardware providers.
"""
import argparse
from datetime import datetime
import hashlib
import json
import re
from pathlib import Path
import subprocess
import struct
import sys
from types import SimpleNamespace
import types
import tomllib

ROOT = Path(__file__).resolve().parents[1]
PIN = tomllib.loads((ROOT / "configs/zizhan-ios18e-inputs.toml").read_text())["source"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("native-source", "native-build", "input-root", "prelink-kit", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--all-compiled", action="store_true",
                        help="Inspect other simple named vtables present in the actual iokit/libkern objects")
    args = parser.parse_args()
    source, work, out = args.native_source.resolve(), args.native_build.resolve(), args.out.resolve()
    if subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip() != PIN["commit"]:
        parser.error("Wrong native source revision")
    built = json.loads((work / "host-core.json").read_text())
    kernel = work / "artifacts/mach.development.pong.unstripped.noctf"
    native_hash = hashlib.sha256(kernel.read_bytes()).hexdigest()
    expected = {Path(row["path"]).name: row["sha256"] for row in built["artifacts"]}
    if built.get("returncode") != 0 or expected.get(kernel.name) != native_hash:
        parser.error("Kernel differs from its completed build receipt")
    stock = json.loads((args.input_root / "native-inputs.json").read_text())
    original = (args.input_root / stock["kernelcache"]["path"]).resolve()
    stock_hash = hashlib.sha256(original.read_bytes()).hexdigest()
    if stock_hash != stock["kernelcache"]["sha256"] or stock["profile"]["build"] != "22G100":
        parser.error("Wrong stock kernel inputs")
    if out.exists() or out == ROOT or ROOT in out.parents:
        parser.error("Use a new machine-local result directory")
    out.mkdir(parents=True)
    (out / "kit-root/kits").mkdir(parents=True)
    (out / "kit-root/kits/XnuPrelink").symlink_to(args.prelink_kit.resolve(), target_is_directory=True)
    path = source / "scripts/derive-pong-fileset-parent.py"
    code = path.read_text()
    limit = "if len(result)<150 or len(result)>512:"
    if code.count(limit) != 1:
        raise RuntimeError("Review changed public-parent derivation")
    # Its original threshold was for storage/AES/GPU families. Core OSObject
    # families are smaller. All slots, exact end and registration checks stay.
    code = code.replace(limit, "if len(result)<8 or len(result)>512:")
    starts = "starts = entry.function_starts()"
    if code.count(starts) != 1:
        raise RuntimeError("Review changed class registration scan")
    # The stock function-starts stream includes twelve overflowed KLD deltas.
    # They have no backed address and cannot be class-registration evidence.
    # Keep only real executable ranges, as the core matcher already does.
    code = code.replace(starts,
                        "starts = sorted(address for address in entry.function_starts() "
                        "if any(segment['va'] <= address < segment['va'] + segment['filesize'] "
                        "for segment in entry.segments if segment['name'] in ('__TEXT_EXEC','__TEXT_BOOT_EXEC','__KLD','__LAST'))) ")
    module = types.ModuleType("native_core_class_abi")
    module.__file__ = str(path)
    exec(compile(code, str(path), "exec"), module.__dict__)
    objects = work / "tree/build/xnu/ios-size-obj/DEVELOPMENT_ARM64_PONG"
    classes = [("iokit", name, obj) for name, obj in (
        ("IOService", "IOService"), ("IORegistryEntry", "IORegistryEntry"),
        ("IOPlatformExpert", "IOPlatformExpert"), ("IODTPlatformExpert", "IOPlatformExpert"),
        ("IOCPU", "IOCPU"), ("IOMemoryDescriptor", "IOMemoryDescriptor"),
        ("IOBufferMemoryDescriptor", "IOBufferMemoryDescriptor"), ("IOMemoryMap", "IOMemoryDescriptor"))]
    classes += [("libkern", name, name) for name in
                ("OSObject", "OSMetaClass", "OSCollection", "OSArray", "OSDictionary", "OSSet",
                 "OSData", "OSNumber", "OSString", "OSSymbol", "OSBoolean")]
    if args.all_compiled:
        seen = {name for _, name, _ in classes}
        for component in ("iokit", "libkern"):
            for obj in sorted((objects / component / "DEVELOPMENT").glob("*.cpo")):
                raw = obj.read_bytes()
                cursor, symbols = 32, []
                for _ in range(struct.unpack_from("<I", raw, 16)[0]):
                    kind, size = struct.unpack_from("<II", raw, cursor)
                    if kind == 2:
                        offset, count, strings, _ = struct.unpack_from("<4I", raw, cursor + 8)
                        for index in range(count):
                            name, flags, _, _, _ = struct.unpack_from("<IBBHQ", raw, offset + index * 16)
                            if flags & 0x0e == 0x0e and not flags & 0xe0:
                                symbols.append(raw[strings + name:raw.index(0, strings + name)].decode())
                    cursor += size
                for symbol in symbols:
                    match = re.fullmatch(r"__ZTV(\d+)([A-Za-z_]\w*)", symbol)
                    if not match or int(match[1]) != len(match[2]) or match[2] in seen:
                        continue
                    seen.add(match[2])
                    classes.append((component, match[2], obj.stem))
    results = []
    for component, classname, filename in classes:
        obj = objects / component / "DEVELOPMENT" / (filename + ".cpo")
        target = out / (classname + ".json")
        try:
            module.derive(SimpleNamespace(unixnu=out / "kit-root", kernel=original,
                                           object=obj, classname=classname, entry="com.apple.kernel", out=target))
            result = json.loads(target.read_text())
            result.update(stock_sha256=stock_hash, native_sha256=native_hash,
                          compiled_object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(),
                          scope="complete real source-kernel vtable PAC ancestry; import naming only")
            target.write_text(json.dumps(result, indent=2) + "\n")
            results.append({"classname": classname, "slots": result["method_slots"],
                            "qualified": result["abi_compatible"], "path": target.name})
        except (RuntimeError, ValueError, StopIteration, FileNotFoundError, KeyError) as error:
            results.append({"classname": classname, "qualified": False, "reason": str(error)})
            print(classname, "REFUSED", str(error), flush=True)
    report = {"observed_at": datetime.now().astimezone().isoformat(), "reference": PIN,
              "stock_sha256": stock_hash, "native_sha256": native_hash, "device_access": False,
              "results": results, "qualified": sum(row["qualified"] for row in results)}
    (out / "receipt.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Qualified core class tables", report["qualified"], "of", len(results), "; no device changes")


if __name__ == "__main__":
    main()
