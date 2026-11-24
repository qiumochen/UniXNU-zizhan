# UniXNU-zizhan

[简体中文](README.md) | **English**

A [UniXNU](https://github.com/UniXNU/UniXNU) format 2 device repository for the Xiaomi MIX Fold 2 (zizhan, Qualcomm SM8475). It targets **iPadOS 18.7.10 (22H374)** with Darwin 24.6.0 / XNU 11417.140.69, using iPad7,11 (J171AP) as the system restore input and **iPad7,12** as the default ADT runtime identity.

This repository is derived from [UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong) for the Nothing Phone (2) and keeps its full history. Both phones use the SM8475, so the SoC layer, the PongPlatform24 component and most drivers are reused. The zizhan changes are the commits after pong `64ddf28`.

- Firmware: [uefi-zizhan](https://github.com/qiumochen/uefi-zizhan) (uefi-pong plus `Platforms/Xiaomi/zizhanPkg`)
- User capability summary (Chinese): [docs/FEATURES.zh.md](docs/FEATURES.zh.md)
- Current ledger (Chinese): [docs/progress/STATUS.md](docs/progress/STATUS.md)
- Structured porting record: [porting.toml](porting.toml)
- Development notes (Chinese): [docs/progress/](docs/progress/), dated, with each run's configuration, result and root cause

![iPadOS home screen on the inner panel](docs/evidence/2026-10-30-u3-ipados-homescreen.jpg)

## Status (as of 2026-11-24)

| Area | State |
| --- | --- |
| Boot | `fastboot boot` of UEFI -> UniXNU menu -> DarwinBoot24 -> XNU. **No partition is written**; every run returns to Android. Extra kernel arguments beyond DarwinBoot's 1024-byte ceiling can pass through `/chosen zizhan-boot-args` |
| CPU / memory | 8 cores (4xA510, 3xA710, 1xX2) as an AMP machine with zizhan frequency caps (2.02 / 2.75 / 3.00 GHz), UI threads prefer the X2; 9728 MiB managed |
| Storage / sandbox | The root is a physically contiguous 19.4 GiB file in Android `/data` holding OS, SystemOS and writable Data volumes. Probabilistic APFS root-mount deadlock (0006) and `minisandbox` extension token issue (0040, eliminating MobileNotes `SIGABRT`) are fixed |
| Display | 2160x1916 inner panel at 120 Hz, DPU zero-copy scanout (128 MiB pool with 1 copy buffer and 6 canvas slots by default), real L18 DCS brightness enabled by default with native Settings slider and readback. Unattended inner-panel off/on passes 20/20 rounds in the daily profile. Outer panel raw framebuffer output is verified by MISR; full iPadOS UI on the outer panel is not implemented |
| Touch / input | Inner ST FTS over GPIO bit-banged SPI: tapping, scrolling and native Chinese/English software keyboards work on `iPad7,12`; touch reporting pauses while the inner panel is off. Outer ST FTM5 controller status events work; touch coordinates are not wired yet |
| GPU | Adreno 730 with Turnip and metal2vulkan, 768 MiB BO pool, 256 MiB WebKit GPU VA windows (fixing Safari/Baidu texture refusals), ASTC enabled on `iPad7,12`. Per-swap compositor fence snapshotting yields 58.6 fps `open_zoom`, 54.3 fps `exit_zoom` and 95.7 fps `home_settle` ([11-24 record](docs/progress/2026-11-24-zoom-cadence-and-sandbox.md)). Wallpaper GPU access stays disabled; no global 120 fps claim |
| Battery / standby / thermal | ADSP + PMIC GLINK readings match Android (top-right status bar and Settings Battery work; Battery Health still spins). Daily profile enables two-bank TSENS step-throttling (CPU 90 C / GPU 85 C), offlines CPU 2..7, holds GPU at 220 MHz and enables Wi-Fi 802.11 PS while screen is off (~-422 mA battery-only draw; full SoC sleep remains open) |
| Buttons / fold | Holding power for 3 s warm-resets to fastboot; short-press screen off/on and Hall lid-follow pass unattended cycles, pending attended physical fold and volume-key checks |
| Wi-Fi | Enabled by default alongside GPU. Native Settings selection/join, saved credentials, WPA2/DHCP/HTTPS, bidirectional 2.4<->5 GHz distinct-BSSID reconnects and zero packet loss across 10 screen off/on cycles pass; different subnets and multi-hour roaming remain open ([lifecycle record](docs/progress/2026-11-05-wifi-lifecycle.md)) |
| Bluetooth | Enabled by default (`VIRT` H4 bridge + QCA6490 SE20 UART). Settings shows Bluetooth on and scans nearby BLE devices; pairing, connections and audio are not tested yet ([11-23 record](docs/progress/2026-11-23-round5.md)) |
| Sensors | Opt-in `zizhan_slpi=4` runs the SLPI sensor DSP alongside ADSP, serves `apps_std` from RAM, streams the accelerometer at 52 Hz and drives 180-degree upside-down rotation via virtual HID services; landscape layout and physical axis mapping await handheld verification ([11-23 record](docs/progress/2026-11-23-round6.md)) |
| Cellular / audio / camera | MPSS boots offline (radio off) and publishes 45 QMI services; AudioReach + dual CS35L41 silent data path reaches the amplifier pins (speaker listening pending); all 4 camera sensors answer CCI chip IDs (no capture) |

## Hardware differences from Pong

| | Pong | zizhan |
| --- | --- | --- |
| Inner panel | 1080x2412 | 2160x1916 command-mode DSC, 60/120 Hz (plus a 1080x2520 outer panel) |
| Touch | Goodix Berlin-D | Inner ST FTS (`fts-pri` on SPI0; SE4 is GSI-only), outer ST FTM5 (`fts-sec` on SPI2) |
| Second DRAM range | 0xf1400000 + 0xec00000 | 0xf4600000 + 0xba00000 |
| ADSP carveout | 0x85700000 + 0x2800000 | 0x9fd00000 + 0x3100000 |
| Battery property IDs | QTI standard | Xiaomi: shifted by one after 12 |
| Amplifiers | 2xTFA9873 | 2xCS35L41 |

## Build

The build host is an Apple silicon Mac with full Xcode, Homebrew llvm@18, lld, dtc and Python 3.12. The pipeline actually used is in [`tools/zizhan/mac/`](tools/zizhan/mac/); every script finds its work directory through `ZIZHAN_SRC` (default `~/zizhan`):

```text
$ZIZHAN_SRC/
  UniXNU-zizhan/   this repository
  uefi-zizhan/     firmware repository (with submodules)
  UniXNU-pin/      UniXNU 0fc0fc1f9ddd0fbd4787738afef38667df5e0a95 (fetched by the scripts)
  sheng/           UniXNU-sheng 08cde20 (fetched by ci_prep.sh)
  work/  out/  logs/  venv/
```

| Step | Script |
| --- | --- |
| Firmware FD and UEFI boot image | `build_fw.sh`, `build_bootimg.py` -> `out/zizhan-uefi.img` |
| Dependencies (sheng, Apple OSS sources, DarwinBoot24) | `ci_prep.sh` |
| Kernel | `DEFCONFIG=zizhan_ipados18_gpu_defconfig ci_kernel.sh` |
| Userland and root image install | `userland/build-ipados18.py` (see [migration/ipados18](migration/ipados18/README.zh.md)), `os-trial-edit.py`, `install_pinned.py` |
| Boot image | `PROFILE=zizhan_ipados18_gpu ci_boot_ufs.sh NAME <root image 4K block>` |
| One unattended device run | `zround.sh` / `cycle.sh IMG RUN`: boot -> watchdog reset to fastboot -> RamoopsDump to logfs -> back in Android, log fetched |
| Partial refresh | `metal-refresh.sh`, `ui-refresh.sh`, `fw-refresh.sh` (rewrite only the OS window of the root image) |
| Diagnostics | `zlog.py`, `zoom-cadence.py`, `gsym.py` / `dscsym.py`, `settings-ui.py` |

Local development builds both the reduced CI configuration and the full device configuration; GitHub Actions supplements those checks:

| Workflow | Trigger |
| --- | --- |
| `Porting record` | Runs on every push and PR to validate structure and changed-area coverage, including documentation-only commits. |
| Display, Wi-Fi and daily-use host regressions | Run automatically when relevant sources, tests, workflows or shared inputs change (or manually); skipped on documentation-only commits. |
| `zizhan macOS build` | Manual `workflow_dispatch` (`task=kernel` or `task=disk-probe`). Default cloud kernel builds disable Wi-Fi, Adreno and backlight and do not replace full local builds or hardware checks. |

The upstream catalog lists this repository. Unified `scripts/package.sh` and the 22H374 defconfig mapping are present; full package-action adoption awaits upstream Darwin24/UFS release support ([packaging follow-up](docs/progress/2026-11-06-boot-release-followup.md#release-01)).

## Private inputs

The repository holds source, patches, configuration and scripts only. Prepare these locally and **do not commit them**:

- Apple's 22H374 restore and the components extracted from it ([proprietary-files.txt](proprietary-files.txt));
- the phone's own firmware and board files: Adreno 730 `a730_sqe.fw`, `gmu_gen70000.bin`, `a730_zap.*`; ADSP / SLPI / MPSS images (merged by `tools/zizhan/mdt2mbn.py`) and sensor calibration seeds (`tools/zizhan/mac/sensors-fs-pack.py`);
- userland images, the root image and partition backups.

## Risks

- Back up every partition first. The default flow only uses `fastboot boot` and keeps the root in a contiguous file on `/data`, but it needs an unlocked bootloader and root.
- The daily profile enables two-bank TSENS step-throttling by default (CPU 90 C / GPU 85 C). Disabling it with `zizhan_daily=0` or running sustained stress rounds still heats the phone; `cycle.sh` waits for the battery to cool before starting the next run.

## Credits and license

- [UniXNU](https://github.com/UniXNU/UniXNU) and its components;
- [UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong) and [uefi-pong](https://github.com/Chengdong-Yu/uefi-pong) by Chengdong-Yu, the base of this port;
- [UniXNU-sheng](https://github.com/UniXNU/UniXNU-sheng), the reference for the battery protocol, reset and timeout settings.

Device-level code is BSD-2-Clause-Patent ([LICENSE](LICENSE)). Imported components keep their own licenses and SPDX identifiers; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This project is not affiliated with Apple, Xiaomi or Nothing.
