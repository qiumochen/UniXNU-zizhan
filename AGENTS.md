# zizhan device repository

- This is the Xiaomi MIX Fold 2 (zizhan, SM8475) format 2 device repository, derived from UniXNU-pong and targeting iPadOS 18.7.10 / 22H374 / Darwin 24. Follow https://unixnu.com/zh/porting/device-repository and https://unixnu.com/zh/porting/records.
- Keep the ARM64, SM8475, OS profile and board layers separate. Preserve the community component identity PongPlatform24; zizhan changes stay in the device layer and name zizhan in comments and patch file names.
- zizhan values come from the phone's own live DT (`tools/gen_reserved.py`) or from the vendor sources. Do not reuse Pong or Sheng constants without checking them against zizhan.

# Probe policy

- Optional probes must record PASS, FAIL, SKIP or RUNNING and return to the normal boot path. Never end a boot with panic, halt or an unbounded loop to show a probe result.
- On failed prerequisites, log the reason and skip dependent MMIO/DMA work. Do not fabricate services, media or hardware resources to keep boot moving.
- Preserve real kernel panic handling. A failed optional probe does not authorize suppressing a genuine fatal error.

# Device trials

- Use temporary `fastboot boot` only; do not flash partitions. Every boot argument set must include a way back (pong_autoreset, panic reset, power-key reset), otherwise the phone stays in iOS until someone unplugs it.
- Keep a full partition backup before any write to the phone. The root image is a pinned file on /data; never move or defragment it from Android.
- Keep the kernel command line under 1024 bytes (DarwinBoot24 refuses longer lines).

# Publishing

- Publish source, configuration, porting records and scripts only. Apple restore binaries, firmware, userdata, root images, credentials and build artifacts stay local.
- Record host build results separately from device results in porting.toml. A successful build is not evidence of a working device function.
- Scripts read paths from ZIZHAN_SRC; do not commit personal paths, host names or account names.
