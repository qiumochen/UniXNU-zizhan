#!/usr/bin/env python3
"""Compile maintained zizhan A730/scanout sources for the native PAC parent.

Uses the selected native builder with an isolated copy of the device sources,
retaining zizhan display/pool/DSI ownership and locally extracted firmware.
This is an archive only: no kernel link, publication, hardware or phone test.
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("native-source", "tree", "parent", "stock-kernel", "prelink-kit", "firmware", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    source, out = args.native_source.resolve(), args.out.resolve()
    if subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip() != PIN["commit"]:
        parser.error("Wrong native source revision")
    parent = json.loads(args.parent.read_text())
    if (parent.get("build"), parent.get("arch"), parent.get("classname"), parent.get("abi_compatible")) != ("22G100", "arm64e", "IOAcceleratorES", True):
        parser.error("A matching native PAC parent is required")
    if out.exists() or out == ROOT or ROOT in out.parents:
        parser.error("Use a new local result directory")
    for name in ("gmu_gen70000.bin", "a730_sqe.fw", "a730_zap.mdt", "a730_zap.b01", "a730_zap.b02"):
        if not (args.firmware / name).is_file():
            parser.error("Actual device firmware file missing: " + name)
    overlay = out / "source-overlay"
    overlay.mkdir(parents=True)
    shutil.copytree(source / "scripts", overlay / "scripts", ignore=shutil.ignore_patterns("__pycache__"))
    builder = overlay / "scripts/build-pong-adreno-platform.py"
    recipe = builder.read_text()
    # The maintained device recipe does not select the new native branch's
    # separate GpuContext implementation; its older live path is in the real
    # SmmuContext/Memory objects. Keep the device's source selection intact.
    selection = "'NativeAdrenoGpuContext','NativeAdrenoCpu'"
    if recipe.count(selection) != 1:
        raise RuntimeError("Review changed native GPU object selection")
    builder.write_text(recipe.replace(selection, "'NativeAdrenoCpu'"))
    smmu_builder = overlay / "scripts/build-pong-smmu4k.py"
    smmu = smmu_builder.read_text()
    missing_mmio = "'ArmSmmu4KTableMemory','ArmSmmu4KMmio'"
    if smmu.count(missing_mmio) != 1:
        raise RuntimeError("Review changed shared SMMU helper selection")
    smmu_builder.write_text(smmu.replace(missing_mmio, "'ArmSmmu4KTableMemory'"))
    (overlay / "drivers").mkdir()
    shutil.copytree(ROOT / "drivers/PongAdreno", overlay / "drivers/PongAdreno")
    (overlay / "drivers/PongPower").symlink_to(ROOT / "drivers/PongPower", target_is_directory=True)
    (overlay / "drivers/PongProbe.h").symlink_to(ROOT / "drivers/PongProbe.h")
    (overlay / "kit-root/kits").mkdir(parents=True)
    (overlay / "kit-root/kits/XnuPrelink").symlink_to(args.prelink_kit.resolve(), target_is_directory=True)
    glue = overlay / "drivers/PongAdreno/PongAdrenoNativeGlue.cpp"
    text = glue.read_text()
    marker = '    service->setProperty("PongMetal22H374Enabled",enabled);'
    if text.count(marker) != 1 or text.count("#if PONG_GUEST_22H374") != 2:
        raise RuntimeError("Review changed device GPU version-selection boundary")
    text = text.replace("#if PONG_GUEST_22H374", "#if PONG_GUEST_22H374 || PONG_GUEST_22G100")
    text = text.replace(marker, '#if PONG_GUEST_22G100\n    service->setProperty("PongMetal22G100Enabled",enabled);\n#else\n' + marker + '\n#endif')
    glue.write_text(text)
    command = [sys.executable, str(overlay / "scripts/build-pong-adreno-platform.py"),
               "--guest-build", "22G100", "--tree", str(args.tree.resolve()),
               "--output-dir", str(out / "artifacts"), "--parent-abi", str(args.parent.resolve()),
               "--native-kernel", str(args.stock_kernel.resolve()), "--unixnu", str(overlay / "kit-root"),
               "--firmware-dir", str(args.firmware.resolve())]
    report = {"started_at": datetime.now().astimezone().isoformat(), "source": PIN["commit"],
              "device_revision": subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip(),
              "scope": "host maintained zizhan GPU archive compile only; not a linked or boot-qualified driver",
              "board_source_sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                                      for path in (ROOT / "drivers/PongAdreno").rglob("*") if path.is_file()},
              "adaptation": "22G100 service property/OS selection only; preserve maintained scanout, source lifecycle and board constants",
              "phone_written": False, "command": command}
    receipt = out / "receipt.json"
    receipt.write_text(json.dumps(report, indent=2) + "\n")
    with (out / "build.log").open("w") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
    report.update(finished_at=datetime.now().astimezone().isoformat(), returncode=result.returncode)
    archive = out / "artifacts/libPongAdrenoPlatform.a"
    if archive.exists():
        report["archive"] = {"bytes": archive.stat().st_size, "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}
    if result.returncode == 0:
        smmu_tree = out / "smmu-tree"
        with (out / "smmu-build.log").open("w") as stream:
            smmu_result = subprocess.run([sys.executable, str(smmu_builder), "--tree", str(smmu_tree)],
                                          stdout=stream, stderr=subprocess.STDOUT)
        report["smmu_returncode"] = smmu_result.returncode
        smmu_archive = smmu_tree / "build/pong-smmu4k/libPongSmmu4K.a"
        if smmu_archive.exists():
            report["smmu_archive"] = {"bytes": smmu_archive.stat().st_size,
                                      "sha256": hashlib.sha256(smmu_archive.read_bytes()).hexdigest(),
                                      "path": str(smmu_archive.relative_to(out))}
    receipt.write_text(json.dumps(report, indent=2) + "\n")
    if result.returncode or report.get("smmu_returncode", 1):
        raise SystemExit("Native zizhan GPU archive failed; inspect " + str(out / "build.log"))
    print("HOST maintained zizhan native GPU archive compiled; hardware qualification remains open")


if __name__ == "__main__":
    main()
