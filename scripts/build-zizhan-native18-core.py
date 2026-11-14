#!/usr/bin/env python3
"""Build an isolated arm64e core control from the pinned Pong native sources.

This is a host compatibility check, not a zizhan boot package. It disables
storage/GPU/Wi-Fi/audio and uses generated host-only volume UUIDs. Board
adaptation, native kext closure, OS preparation and device tests remain separate.
"""
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tomllib
import uuid

REPO = Path(__file__).resolve().parents[1]
PIN = tomllib.loads((REPO / "configs/zizhan-ios18e-inputs.toml").read_text())["source"]


def git(path, *args):
    return subprocess.check_output(["git", "-C", str(path), *args], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path,
                        help="New machine-local output directory outside the device repository")
    parser.add_argument("--reference-root", type=Path, default=REPO / "refer/ipados18")
    parser.add_argument("--developer-dir", type=Path,
                        default=Path("/Applications/Xcode.app/Contents/Developer"))
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--configuration", choices=("DEVELOPMENT", "RELEASE"), default="DEVELOPMENT",
                        help="RELEASE is a separate naming reference, never a runtime qualification")
    parser.add_argument("--lto-reference", type=Path,
                        help="Build a separate RELEASE LTO naming reference, retaining names from this existing release kernel")
    parser.add_argument("--reference-granule", choices=("4k", "16k"), default="4k",
                        help="16k is a stock-comparison naming reference only; runtime/root remains 4k")
    parser.add_argument("--root-storage", type=Path,
                        help="Host-only root link: verified native18-storage-abi output directory")
    parser.add_argument("--software-aes", type=Path,
                        help="Host-only root link: matching native software AES output directory")
    parser.add_argument("--gpu", type=Path,
                        help="Add verified maintained zizhan native GPU/shared-SMMU archives and real FST2 input source")
    parser.add_argument("--wifi", type=Path,
                        help="Add verified maintained zizhan native ath11k/board/network archive")
    parser.add_argument("--power", type=Path,
                        help="Add verified maintained native ADSP/dual-battery/backlight archive, without audio")
    parser.add_argument("--volume-layout", type=Path,
                        help="Use actual independent candidate UUIDs after raw-volume readback qualification")
    parser.add_argument("--volume-receipt", type=Path,
                        help="Matching native18-local-volumes receipt.json for the actual raw OS/SystemOS/Data")
    parser.add_argument("--display-handoff", action="store_true",
                        help="Pair native framebuffer CPU fallback with the maintained real scanout owner")
    args = parser.parse_args()
    source, work = args.source.resolve(), args.work.resolve()
    root_link = args.root_storage is not None or args.software_aes is not None
    if root_link and not all((args.root_storage, args.software_aes)):
        parser.error("Root link requires both --root-storage and --software-aes")
    if args.gpu and not root_link:
        parser.error("--gpu requires the explicit root-storage/software-AES stage")
    if args.wifi and not args.gpu:
        parser.error("--wifi requires the matched GPU/shared-SMMU root stage")
    if args.power and not args.wifi:
        parser.error("--power requires the matched Wi-Fi/shared RPMh owner")
    if bool(args.volume_layout) != bool(args.volume_receipt) or (args.volume_layout and not root_link):
        parser.error("Actual candidate UUIDs require root link and paired --volume-layout/--volume-receipt")
    if args.display_handoff and not args.gpu:
        parser.error("--display-handoff requires the maintained GPU/scanout archive")
    if root_link and args.configuration != "DEVELOPMENT":
        parser.error("The native root control retains DEVELOPMENT checks")
    if args.lto_reference and (root_link or args.configuration != "RELEASE"):
        parser.error("--lto-reference is only for the separate RELEASE naming stage")
    if args.reference_granule != "4k" and (root_link or args.configuration != "RELEASE"):
        parser.error("16k page geometry is permitted only in an isolated RELEASE naming reference")
    if git(source, "rev-parse", "HEAD") != PIN["commit"]:
        parser.error("--source does not match the reviewed native commit")
    if git(source, "rev-parse", "HEAD^{tree}") != PIN["tree"]:
        parser.error("--source tree differs from the reviewed native tree")
    if git(source, "status", "--porcelain"):
        parser.error("--source has tracked or untracked changes; retain them and select a clean checkout")
    if work == REPO or REPO in work.parents or work.exists():
        parser.error("--work must be a new directory outside the public device repository")
    if not 1 <= args.jobs <= 32:
        parser.error("--jobs must be between 1 and 32")
    feeds = [x for x in tomllib.loads((source / "feeds.toml").read_text())["source"]
             if x["kind"] == "refer"]
    references = {}
    for item in feeds:
        path = (args.reference_root / item["ref"]).resolve()
        if git(path, "rev-parse", "HEAD") != item["commit"] or git(path, "status", "--porcelain"):
            parser.error("Wrong or dirty public reference: " + str(path))
        references[item["name"]] = {"path": str(path), "commit": item["commit"]}
    work.mkdir(parents=True)
    overlay = work / "source-overlay"
    overlay.mkdir()
    shutil.copytree(source / "scripts", overlay / "scripts",
                    ignore=shutil.ignore_patterns("__pycache__"))
    # Only generated scripts/config are mutable. Firmware, driver code, patches
    # and the restored upstream checkout remain untouched.
    for name in ("drivers", "kits", "package", "patches", "configs"):
        (overlay / name).symlink_to(source / name, target_is_directory=True)
    shutil.copyfile(source / "feeds.toml", overlay / "feeds.toml")
    adaptations = []
    # Keep sanitizer/fatal checks enabled. The public formatter's signed
    # negation traps for LLONG_MIN in the DEVELOPMENT kernel; form its
    # unsigned magnitude without invoking signed overflow instead.
    magnitude_patch = REPO / "patches/xnu-ios18e/0003-zizhan-printf-unsigned-magnitude.patch"
    native_builder = overlay / "scripts/build-pong-ios18e.py"
    text = native_builder.read_text()
    selection = "    for old in destination.glob('*.patch'):\n"
    if text.count(selection) != 1:
        raise RuntimeError("Review changed native printf patch selection")
    text = text.replace(selection,
                        "    selected['zzzzzzzzzzzzzzzzzzzzzzzzzzzzz-zizhan-printf-unsigned-magnitude.patch'] = Path(" + repr(str(magnitude_patch)) + ")\n" + selection)
    native_builder.write_text(text)
    adaptations.append({"change": "Compute signed printf magnitude with defined unsigned arithmetic, retaining UBSan/fatal handling and LLONG_MIN output",
                        "patch_sha256": hashlib.sha256(magnitude_patch.read_bytes()).hexdigest()})
    if args.reference_granule == "16k":
        path = overlay / "scripts/build-xnu-ipados18.sh"
        lines = path.read_text().splitlines()
        matches = [i for i, line in enumerate(lines) if line.startswith("EXTRA_CFLAGS=${XNU_EXTRA_CFLAGS:")]
        if len(matches) != 1:
            raise RuntimeError("Review changed reference compiler flag boundary")
        lines.insert(matches[0] + 1, 'EXTRA_CFLAGS="$EXTRA_CFLAGS -D__ARM_16K_PG__=1"')
        path.write_text("\n".join(lines) + "\n")
        adaptations.append({"script": "build-xnu-ipados18.sh",
                            "change": "16KiB granule only for stock RELEASE naming reference, never the device kernel"})
    if args.lto_reference:
        # The reference branch immediately replaces candidates with actual
        # external definitions. A fresh tree has no all-kpi.exp yet, and its
        # unused read otherwise prevents that independent build from starting.
        path = overlay / "scripts/build-pong-ios18e.py"
        text = path.read_text()
        old = "candidates=(tree/('build/xnu/ios-obj/'+a.configuration+'_ARM64_PONG/all-kpi.exp')).read_text().splitlines()"
        if text.count(old) != 1:
            raise RuntimeError("Review changed RELEASE naming export selection")
        path.write_text(text.replace(old, "candidates=[] if a.lto_reference else (tree/('build/xnu/ios-obj/'+a.configuration+'_ARM64_PONG/all-kpi.exp')).read_text().splitlines()"))
        adaptations.append({"script": "build-pong-ios18e.py",
                            "change": "Skip unused preexisting KPI-list read only for LTO naming; retain exports from real reference symbols"})
    if root_link:
        storage = args.root_storage.resolve()
        aes = args.software_aes.resolve()
        storage_report = json.loads((storage / "receipt.json").read_text())
        aes_report = json.loads((aes / "build-report.json").read_text())
        storage_archive = storage / "libZizhanNativeUfs18e.a"
        aes_archive = aes / "libPongCrypto18e.a"
        if (storage_report.get("result") != "HOST_VERIFIED" or
                not storage_report.get("owned_subclass", {}).get("pac_signatures_match") or
                storage_report["reference_commits"]["native"] != PIN["commit"] or
                hashlib.sha256(storage_archive.read_bytes()).hexdigest() != storage_report["archive"]["sha256"]):
            raise RuntimeError("Native storage archive is not the verified zizhan source/PAC result")
        if (aes_report.get("build"), aes_report.get("arch"), aes_report.get("hardware_keys")) != ("22G100", "arm64e", False):
            raise RuntimeError("Wrong software AES ABI or key scope")
        if not aes_report.get("owned_subclass", {}).get("pac_signatures_match"):
            raise RuntimeError("Software AES inherited PAC signatures are unverified")
        # Split only the real software provider registration from PongPower;
        # do not link or acknowledge absent Nothing audio/battery/modem code.
        (overlay / "drivers").unlink()
        (overlay / "drivers").mkdir()
        for child in (source / "drivers").iterdir():
            destination = overlay / "drivers" / child.name
            if child.name == "ArmDTPlatform":
                shutil.copytree(child, destination, ignore=shutil.ignore_patterns(".git", "__pycache__"))
            else:
                destination.symlink_to(child, target_is_directory=child.is_dir())
        platform = overlay / "drivers/ArmDTPlatform/ArmDTPlatform.cpp"
        text = platform.read_text()
        for name in ("PongBootReset.h", "BootResetCore.h"):
            shutil.copyfile(REPO / "drivers/ArmDTPlatform" / name, platform.parent / name)
        reset = platform.parent / "PongBootReset.h"
        reset_text = reset.read_text()
        sampler = ('    if (seen && !sampled && gSampleSeconds && now - seenAt >= 20 * f && pong_thread_sampler_start) {\n'
                   '      sampled = true;\n      pong_thread_sampler_start(gSampleProc, gSampleSeconds);\n    }\n')
        for expected in (sampler,
                         'extern "C" void pong_thread_sampler_start(const char *proc, unsigned seconds) __attribute__((weak));',
                         'extern "C" void (*pong_button_reset_hook)(void) __attribute__((weak));',
                         '  if (&pong_button_reset_hook) pong_button_reset_hook = buttonReset;'):
            if reset_text.count(expected) != 1:
                raise RuntimeError("Review changed optional reset consumer boundary")
            reset_text = reset_text.replace(expected, "")
        # The minimal root stage has no diagnostic sampler or physical button
        # driver. Omit those optional consumers, rather than create weak-link
        # stand-ins. Deadline/panic/klog and actual watchdog remain real.
        reset_text = reset_text.replace('&pong_button_reset_hook ? "3 s" : "(no hook in this kernel)"',
                                        '"(no button driver in native root stage)"')
        reset.write_text(reset_text)
        include = "#include <uuid/uuid.h>\n"
        configure = "bool ArmDTPlatform::configure (IOService *provider)\n{\n"
        if text.count(include) != 1 or text.count(configure) != 1:
            raise RuntimeError("Review changed device reset registration boundary")
        text = text.replace(include, include + "// zizhan: retain existing bounded return after the native panic log.\n"
                            "#define pong_panic_reset_hook pong_panic_recovery_hook\n"
                            '#include "PongBootReset.h"\n'
                            "#undef pong_panic_reset_hook\n")
        text = text.replace(configure, configure + "  pong_boot_reset::start();\n")
        declaration = 'extern "C" bool pong_cpu_start(IOService *platform);'
        call = "  armdt_platform_configured = 1;\n"
        if text.count(declaration) != 1 or text.count(call) != 1:
            raise RuntimeError("Review changed platform registration boundary")
        text = text.replace(declaration, declaration + '\nextern "C" void pong_software_aes_start_optional(void);')
        text = text.replace(call, call + "  // zizhan native root: real software-key provider, independent of foreign power hardware.\n  pong_software_aes_start_optional();\n")
        if args.wifi:
            wifi = args.wifi.resolve()
            wifi_report = json.loads((wifi / "receipt.json").read_text())
            if (wifi_report.get("build"), wifi_report.get("arch"), wifi_report.get("board"), wifi_report.get("returncode")) != ("22G100", "arm64e", "zizhan", 0):
                raise RuntimeError("Require completed actual native zizhan Wi-Fi build")
            wifi_archive = wifi / "tree/build/pong-wifi/libPongWifiNative.a"
            if not any(Path(row["path"]).name == wifi_archive.name and hashlib.sha256(wifi_archive.read_bytes()).hexdigest() == row["sha256"] for row in wifi_report["artifacts"]):
                raise RuntimeError("Actual native Wi-Fi archive differs from its receipt")
            if not wifi_report.get("object_abi") or any(row["cpu_subtype"] != "0xc0000002" for row in wifi_report["object_abi"]):
                raise RuntimeError("Every Wi-Fi object must use the kernel arm64e ABI")
            for row in wifi_report["source_files"]:
                if hashlib.sha256((REPO / row["path"]).read_bytes()).hexdigest() != row["sha256"]:
                    raise RuntimeError("Maintained Wi-Fi source changed since actual compile")
            if hashlib.sha256((REPO / "configs/zizhan-wifi-sm8475.json").read_bytes()).hexdigest() != wifi_report["profile_sha256"]:
                raise RuntimeError("Actual zizhan Wi-Fi board profile changed")
            # The native full recipe couples Wi-Fi selection to foreign
            # power builders. Register this complete owned archive directly;
            # its worker retains real UFS readiness/radio/DMA gates.
            text = text.replace(declaration, declaration + '\nextern "C" void pong_wifi_start_optional(void);')
            text = text.replace("  pong_software_aes_start_optional();\n", "  pong_software_aes_start_optional();\n  pong_wifi_start_optional();\n")
        if args.power:
            power = args.power.resolve()
            power_report = json.loads((power / "receipt.json").read_text())
            if (power_report.get("build"), power_report.get("arch"), power_report.get("board"), power_report.get("returncode"), power_report.get("audio_selected")) != ("22G100", "arm64e", "zizhan", 0, False):
                raise RuntimeError("Require complete native maintained power archive without audio selection")
            if not power_report.get("owned_subclass", {}).get("pac_signatures_match"):
                raise RuntimeError("Battery inherited native power-source PAC signatures differ")
            power_archive = power / "artifacts/libZizhanNativePower18e.a"
            if hashlib.sha256(power_archive.read_bytes()).hexdigest() != power_report["archive"]["sha256"]:
                raise RuntimeError("Native power archive changed since actual build")
            for row in power_report["source_files"]:
                if hashlib.sha256((REPO / row["path"]).read_bytes()).hexdigest() != row["sha256"]:
                    raise RuntimeError("Maintained power source changed since actual compile")
            text = text.replace(declaration, declaration + '\nextern "C" void pong_power_start_optional(void);')
            text = text.replace("  pong_wifi_start_optional();\n", "  pong_wifi_start_optional();\n  pong_power_start_optional();\n")
        platform.write_text(text)
        adaptations.append({"source": "drivers/ArmDTPlatform/ArmDTPlatform.cpp",
                            "change": "Register real software AES separately from Nothing power/audio; retain zizhan watchdog/deadline on native post-panic hook",
                            "overlay_sha256": hashlib.sha256(platform.read_bytes()).hexdigest()})
        # Reuse host-verified objects whose PAC ancestry was derived from the
        # exact selected stock kernel. This step links them, never boots them.
        ufs_builder = overlay / "scripts/build-pong-ufs-ipados18-macos.py"
        ufs_builder.write_text("import argparse, pathlib, shutil\n"
                              "p=argparse.ArgumentParser();p.add_argument('--tree',type=pathlib.Path,required=True);"
                              "p.add_argument('--skip-image-verification',action='store_true');a=p.parse_args()\n"
                              "out=a.tree/'build/native-ufs';out.mkdir(parents=True,exist_ok=True)\n"
                              "shutil.copyfile(" + repr(str(storage_archive)) + ",out/'libNativeUfs.a')\n")
        native_builder = overlay / "scripts/build-pong-ios18e.py"
        text = native_builder.read_text()
        selection = "    for old in destination.glob('*.patch'):\n"
        if text.count(selection) != 1:
            raise RuntimeError("Review changed native patch selection")
        extra_patch = REPO / "patches/xnu-ios18e/0001-zizhan-root-waiters.patch"
        text = text.replace(selection,
                            "    selected['zzzzzzzzzzzzzzzzzzzzzzzzzzzzz-zizhan-root-waiters.patch'] = Path(" + repr(str(extra_patch)) + ")\n" + selection)
        if args.gpu:
            gpu = args.gpu.resolve()
            gpu_report = json.loads((gpu / "receipt.json").read_text())
            gpu_archive = gpu / "artifacts/libPongAdrenoPlatform.a"
            smmu_archive = gpu / gpu_report["smmu_archive"]["path"]
            if gpu_report.get("returncode") != 0 or gpu_report.get("smmu_returncode") != 0:
                raise RuntimeError("GPU/shared-SMMU compilation did not complete")
            for archive, metadata in ((gpu_archive, gpu_report["archive"]), (smmu_archive, gpu_report["smmu_archive"])):
                if hashlib.sha256(archive.read_bytes()).hexdigest() != metadata["sha256"]:
                    raise RuntimeError("Native device archive changed: " + str(archive))
            for relative, expected in gpu_report["board_source_sha256"].items():
                if hashlib.sha256((REPO / relative).read_bytes()).hexdigest() != expected:
                    raise RuntimeError("GPU source changed since compilation: " + relative)
            patch = (REPO / "patches/xnu-ipados18/0002-zizhan-fts.patch").read_text()
            marker = "+++ b/bsd/dev/pong_fts.c\n@@ -0,0 +1,1099 @@\n"
            if patch.count(marker) != 1:
                raise RuntimeError("Review changed maintained FST2 source patch")
            payload = patch.split(marker, 1)[1].splitlines()
            if len(payload) != 1099 or any(not line.startswith("+") for line in payload):
                raise RuntimeError("FST2 add-file patch changed")
            touch = "\n".join(line[1:] for line in payload) + "\n"
            # Only the optional old gesture sampler has no backend in this
            # stage. Real event/GPIO/SPI/rail/coordinate code stays unchanged.
            declaration = "extern void pong_gesture_sample(void);\n"
            call = "\t\tpong_gesture_sample();     /* osfmk/kern/thread.c (0005), boot-arg gs */\n"
            if touch.count(declaration) != 1 or touch.count(call) != 1:
                raise RuntimeError("Review changed optional touch diagnostic consumer")
            touch = touch.replace(declaration, "").replace(call, "\t\t/* Native stage omits the optional old gesture sampler. */\n")
            touch_patch = work / "zizhan-native-fst2.patch"
            touch_patch.write_text(
                "--- a/bsd/conf/files\n+++ b/bsd/conf/files\n@@ -403,4 +403,5 @@\n"
                " bsd/dev/pong_input.c\t\t\tstandard\n bsd/dev/pong_goodix.c\t\t\tstandard\n+bsd/dev/pong_fts.c\t\t\tstandard\n"
                " bsd/dev/pong_buttons.c\t\t\tstandard\n bsd/kern/kern_event.c\t\t\tstandard\n"
                "--- a/bsd/kern/bsd_init.c\n+++ b/bsd/kern/bsd_init.c\n@@ -1171,5 +1171,5 @@\n"
                " #if defined(PONG)\n"
                "-\textern void pong_goodix_init(void);\n-\tpong_goodix_init();\n"
                "+\textern void pong_fts_init(void);\n+\tpong_fts_init();\n"
                " \t\textern void pong_buttons_init(void);\n \t\tpong_buttons_init();\n"
                "--- /dev/null\n+++ b/bsd/dev/pong_fts.c\n@@ -0,0 +1," + str(len(touch.splitlines())) + " @@\n" +
                "".join("+" + line + "\n" for line in touch.splitlines()))
            text = text.replace(selection,
                                "    selected['zzzzzzzzzzzzzzzzzzzzzzzzzzzzzz-zizhan-native-fst2.patch'] = Path(" + repr(str(touch_patch)) + ")\n" + selection)
            # The maintained zizhan presenter has its own older active
            # protocol/trace path. Do not import an idle notification ABI or
            # trace sink that its driver does not implement, nor add stand-ins.
            text = text.replace(selection,
                                "    selected.pop('zzzz-pong-scanout-idle.patch')\n"
                                "    selected.pop('zzzzzzz-pong-frame-links.patch')\n" + selection)
            adaptations.append({"change": "Defer new Pong idle-notification and frame-trace consumers; keep real maintained awake scanout protocol; no sleep claim"})
            if args.display_handoff:
                display_patch = REPO / "patches/xnu-ios18e/0002-zizhan-native-cpu-frame.patch"
                text = text.replace(selection,
                                    "    selected['zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz-zizhan-native-cpu-frame.patch'] = Path(" + repr(str(display_patch)) + ")\n" + selection)
                adaptations.append({"change": "Restore actual CPU panel buffer before CPU fallback; reject unconfirmed asynchronous latch without CPU writes or false presentation",
                                    "patch_sha256": hashlib.sha256(display_patch.read_bytes()).hexdigest(),
                                    "async_latch_qualified": False, "hardware_executed": False})
        boundary = "    # Generated recipe overlay: architecture changes never overwrite the\n"
        if text.count(boundary) != 1:
            raise RuntimeError("Review changed native core configuration boundary")
        enable = "    # zizhan host root link: preserve real root hooks without foreign peripherals.\n"
        for key in ("NATIVE_UFS", "OWNER_DATA_UFS", "OWNER_DATA_MOUNT", "PONG_APFS_DEBUG",
                    "OWNER_CACHE_MOUNT", "OWNER_CACHE_STARTUP", "OWNER_CRYPTEX_FILES"):
            enable += "    env['XNU_" + key + "'] = '1'\n"
        if args.gpu:
            enable += "    env['XNU_PONG_ADRENO_PLATFORM'] = '1'\n"
        if args.power:
            enable += "    env['XNU_PONG_BACKLIGHT'] = '1'\n"
        text = text.replace(boundary, enable + boundary)
        native_builder.write_text(text)
        recipe = overlay / "scripts/build-xnu-ipados18.sh"
        text = recipe.read_text()
        if args.gpu:
            # Replace only native archive generation; the complete real
            # driver implementation was compiled/qualified by the prior step.
            gpu_command = ('  python3 "$PONG_NATIVE_DRIVER_SOURCE/scripts/build-pong-adreno-platform.py" \\\n'
                           '    --guest-build 22H374 --tree "$ROOT" --output-dir "$ROOT/build/pong-adreno"')
            if text.count(gpu_command) != 1:
                raise RuntimeError("Review changed GPU archive recipe boundary")
            import shlex
            text = text.replace(gpu_command,
                                '  mkdir -p "$ROOT/build/pong-adreno"\n'
                                '  cp ' + shlex.quote(str(gpu_archive)) + ' "$ROOT/build/pong-adreno/libPongAdrenoPlatform.a"')
            smmu_builder = overlay / "scripts/build-pong-smmu4k.py"
            smmu_builder.write_text("import argparse,pathlib,shutil\n"
                                   "p=argparse.ArgumentParser();p.add_argument('--tree',type=pathlib.Path,required=True);a=p.parse_args()\n"
                                   "out=a.tree/'build/pong-smmu4k';out.mkdir(parents=True,exist_ok=True)\n"
                                   "shutil.copyfile(" + repr(str(smmu_archive)) + ",out/'libPongSmmu4K.a')\n")
            adaptations.append({"change": "Link maintained zizhan GPU/shared SMMU; select real FST2 and omit foreign Goodix startup",
                                "gpu_sha256": gpu_report["archive"]["sha256"],
                                "touch_source_sha256": hashlib.sha256(touch.encode()).hexdigest()})
        boundary = '  EXTRA_MAKE_ARGS+=("LD_KERNEL_ARCHIVES=$PLATFORM_ARCHIVES")'
        if text.count(boundary) != 1:
            raise RuntimeError("Review changed archive-link boundary")
        text = text.replace(boundary,
                            '  PLATFORM_ARCHIVES="$PLATFORM_ARCHIVES -Wl,-force_load,' + str(aes_archive) + '"\n'
                            '  PLATFORM_DEPENDENCIES="$PLATFORM_DEPENDENCIES ' + str(aes_archive) + '"\n' + boundary)
        if args.wifi:
            text = text.replace(boundary,
                                '  PLATFORM_ARCHIVES="$PLATFORM_ARCHIVES -Wl,-force_load,' + str(wifi_archive) + '"\n'
                                '  PLATFORM_DEPENDENCIES="$PLATFORM_DEPENDENCIES ' + str(wifi_archive) + '"\n' + boundary)
            adaptations.append({"change": "Link complete maintained zizhan ath11k/board/network archive and real optional-worker registration independently of foreign power builders",
                                "archive_sha256": hashlib.sha256(wifi_archive.read_bytes()).hexdigest(),
                                "credential_fixture": "not selected", "radio_executed": False})
        if args.power:
            text = text.replace(boundary,
                                '  PLATFORM_ARCHIVES="$PLATFORM_ARCHIVES -Wl,-force_load,' + str(power_archive) + '"\n'
                                '  PLATFORM_DEPENDENCIES="$PLATFORM_DEPENDENCIES ' + str(power_archive) + '"\n' + boundary)
            adaptations.append({"change": "Link maintained zizhan ADSP/dual-pack battery/backlight and actual optional registration; use existing Wi-Fi RPMh owner, omit audio-start consumer",
                                "archive_sha256": hashlib.sha256(power_archive.read_bytes()).hexdigest(),
                                "audio_selected": False, "hardware_executed": False})
        recipe.write_text(text)
        adaptations.append({"change": "Link verified zizhan UFS and software AES; retain root/cache/Data hooks; foreign power/audio/network remain zero",
                            "storage_archive_sha256": hashlib.sha256(storage_archive.read_bytes()).hexdigest(),
                            "aes_archive_sha256": hashlib.sha256(aes_archive.read_bytes()).hexdigest()})
    for name in ("build-pong-platform-macos.py", "build-pong-armcpu-macos.py"):
        path = overlay / "scripts" / name
        text = path.read_text()
        old = "if platform.system() != 'Darwin' or platform.machine() != 'x86_64':"
        new = "if platform.system() != 'Darwin' or platform.machine() not in ('x86_64', 'arm64'):"
        if text.count(old) != 1:
            raise RuntimeError("Review changed upstream host guard: " + name)
        text = text.replace(old, new).replace("This continuation uses the Intel macOS toolchain",
                                             "This core control requires a supported macOS host")
        path.write_text(text)
        adaptations.append({"script": name, "change": "Allow Apple silicon host; retain explicit arm64e cross target",
                            "upstream_sha256": hashlib.sha256((source / "scripts" / name).read_bytes()).hexdigest(),
                            "overlay_sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    actual_volume_receipt = None
    if args.volume_layout:
        actual_volume_receipt = json.loads(args.volume_receipt.read_text())
        if (actual_volume_receipt.get("status"), actual_volume_receipt.get("build"), actual_volume_receipt.get("board"), actual_volume_receipt.get("isolated_data_blank")) != ("RAW_LOCAL_VOLUMES_VERIFIED", "22G100", "zizhan", True):
            raise RuntimeError("Require actual raw local OS/SystemOS/blank Data qualification")
        if hashlib.sha256(args.volume_layout.read_bytes()).hexdigest() != actual_volume_receipt["volume_layout_sha256"]:
            raise RuntimeError("Actual candidate UUID layout changed since verification")
        supplied = json.loads(args.volume_layout.read_text())
        if (supplied.get("build"), supplied.get("board")) != ("22G100", "zizhan"):
            raise RuntimeError("Actual volume layout belongs to another target")
        if len(actual_volume_receipt.get("volumes", [])) != 3:
            raise RuntimeError("Require all three real raw candidate volumes")
        for row in actual_volume_receipt["volumes"]:
            path = Path(row["image"])
            if not path.is_file() or path.is_symlink() or path.stat().st_size != row["size"]:
                raise RuntimeError("Actual raw volume identity differs")
            digest = hashlib.sha256()
            with path.open("rb") as stream:
                for block in iter(lambda: stream.read(8 << 20), b""):
                    digest.update(block)
            key = {"OS": "os", "SystemOS": "systemos", "Data": "data"}[row["name"]]
            if digest.hexdigest() != row["sha256"] or supplied[key]["uuid"] != row["volume_uuid"]:
                raise RuntimeError("Actual raw volume hash/UUID differs from independent readback")
        layout = work / "candidate-volume-layout.json"
        shutil.copy2(args.volume_layout, layout)
    else:
        layout = work / "host-only-volume-layout.json"
        layout.write_text(json.dumps({"scope": "host core control only; never deploy",
                                     **{key: {"uuid": str(uuid.uuid4()).upper()}
                                        for key in ("os", "systemos", "data")}}, indent=2) + "\n")
    lines = ["[pong_build]", 'arch = "arm64e"', 'guest_build = "22G100"',
             "developer_dir = " + json.dumps(str(args.developer_dir.resolve())),
             "volume_layout = " + json.dumps(str(layout))]
    for name, item in references.items():
        lines += ["", "[[source]]", 'kind = "refer"', "name = " + json.dumps(name),
                  "local = " + json.dumps(item["path"])]
    (overlay / "local.toml").write_text("\n".join(lines) + "\n")
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("PONG_", "XNU_", "UNIXNU_"))}
    env["PONG_VOLUME_LAYOUT"] = str(layout)
    if args.power:
        env["PONG_SHENG_ROOT"] = power_report["shared_root"]
    command = [sys.executable, str(overlay / "scripts/build-pong-ios18e.py"),
               "--tree", str(work / "tree"), "--out", str(work / "artifacts"),
               "--phase", "core", "--configuration", args.configuration, "--chained",
               *(["--optimize-size"] if args.configuration == "DEVELOPMENT" else []),
               *(["--lto-reference", "--reference-kernel", str(args.lto_reference.resolve())] if args.lto_reference else []),
               "--jobs", str(args.jobs)]
    report = {"started_at": datetime.now().astimezone().isoformat(), "upstream": PIN,
              "device_source_revision": git(REPO, "rev-parse", "HEAD"),
              "scope": "arm64e host root link with zizhan storage/software AES" if root_link else "arm64e core host control; not a board-qualified kernel",
              "stage": "host-root-link" if root_link else "core",
              "gpu_enabled": bool(args.gpu),
              "wifi_enabled": bool(args.wifi),
              "power_enabled": bool(args.power),
              "audio_selected": False,
              "display_handoff": bool(args.display_handoff),
              "printf_unsigned_magnitude": True,
              "volume_scope": "actual independent candidate UUIDs; closure/install/device still unqualified" if actual_volume_receipt else "random host-only UUIDs; never deploy",
              "volume_receipt_sha256": hashlib.sha256(args.volume_receipt.read_bytes()).hexdigest() if args.volume_receipt else None,
              "touch_driver": "zizhan FST2" if args.gpu else "none selected for runtime qualification",
              "configuration": args.configuration,
              "lto_reference": bool(args.lto_reference),
              "reference_granule": args.reference_granule,
              "bootable": False, "device_access": False, "references": references,
              "adaptations": adaptations, "command": command}
    record = work / "host-core.json"
    record.write_text(json.dumps(report, indent=2) + "\n")
    print("Building isolated native core; log:", work / "build.log", flush=True)
    with (work / "build.log").open("w") as log:
        result = subprocess.run(command, cwd=overlay, env=env, stdout=log, stderr=subprocess.STDOUT)
    report.update(finished_at=datetime.now().astimezone().isoformat(), returncode=result.returncode)
    report["artifacts"] = [{"path": str(path.relative_to(work)), "bytes": path.stat().st_size,
                            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                           for path in sorted((work / "artifacts").glob("mach.*")) if path.is_file()]
    record.write_text(json.dumps(report, indent=2) + "\n")
    if result.returncode:
        raise SystemExit("Native core build failed; inspect " + str(work / "build.log"))
    print("HOST " + ("root link" if root_link else "core build") + " completed; no native boot candidate produced", flush=True)


if __name__ == "__main__":
    main()
