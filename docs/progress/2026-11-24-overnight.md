# 2026-11-24: overnight silent CoreAudio HAL, 4-way auto-rotation fix, standby input quiesce, and backboardd zoom profile

12-hour unattended session on branch `wip/2026-11-17-audio-sensors` (`iPad7,12` identity, active OS trial `sens-hal-v3`, OS `6ec87b7cab84`, UI `78d94d15d55c`). All device rounds ran strictly silent (`pong_audio=0`; CS35L41 AMP/BST/SE15 physical speaker output never enabled), via temporary `fastboot boot` only, with zero partition writes and thermal/battery gates (`T <= 38.0 C`, `level >= 35%`).

## 1. Silent end-to-end CoreAudio HAL daemon & physical volume keys (`7afed2d`, `hal-s1-1..4`, `IO-01`)

### Root causes & fixes

1. **PID 1 read-only `/pong/audio-hal18` `bindfs` overlay (`patches/xnu-ipados18/0041-zizhan-audio-hal-bindfs.patch`)**:
   - Mounting `/pong/audio-hal18` (`ZizhanAudio.driver`) onto `/Library/Audio/Plug-Ins/HAL` from PID 1 failed `IOCurrentTaskHasEntitlement(BINDFS_ENTITLEMENT)` with `EPERM` once `native_bindfs_boot_mount` cleared.
   - Patch `0041` permits PID 1 (`proc_selfpid() == 1`) read-only (`MNT_RDONLY`) `bindfs` mounts solely when `zizhan_audio_service=1` and the source path is `/pong/audio-hal18` (`BINDM_FORCE_ROOT`).
2. **Continuous silent ADSP stream consumer (`drivers/PongPower/ZizhanAudioPrm.cpp`, `ZizhanAudioFlow.h`, `ZizhanAudioRing.h`, `ZizhanAudioServiceGate.h`, `userland/ZizhanAudioPlugIn.c`)**:
   - `zizhan_audio_prm=42` pairs with `zizhan_audio_service=1` so `coreaudiod` -> `ZizhanAudio.driver` -> `ZizhanAudioUserClient` drives real `StartIO` / `StopIO` transitions over the shared 48 kHz / 2 ch ring into the ADSP `WR_SHMEM -> MFC (0x07001015) -> SAL -> TDM` graph, draining 2880-byte / 480-frame 24-bit periods with `V2DONE` completions while keeping CS35L41 AMP/BST/SE15 completely untouched (`pong_audio=0`).
3. **Physical volume key auto-repeat disarm (`userland/ui_compat18.c`, `drivers/ArmDTPlatform/ZizhanVolumeInject.h`)**:
   - PMIC volume key presses (`0xe9`/`0xea`) armed `SBVolumeControl`'s repeat timer, causing runaway `0%`/`100%` stepping or oscillation across consecutive Up/Down presses. Interposing `-[SBVolumeControl handleVolumeButtonWithType:down:]` to disarm via `cancelVolumeEvent` on key-up (`pong_vol_norepeat=0` opts out) produced clean discrete HUD steps (`hal-s1-4.shot1..4.png`, verified via `zizhan_volume_inject` and `volume-inject.py`).

### Device verification (`hal-s1-4`, kernel `play162`)

- `coreaudiod` loaded `ZizhanAudio.driver`, mapped the v2 control page and 48 kHz stereo ring, and serviced two sequential `AudioQueue` sessions (`440 Hz` and `660 Hz`, `callbacks=16` each, `zizhan_audio_daemon_s` in `ui_compat18.c`).
- Kernel `ZizhanAudio` + `PRM 42` completed both `StartIO -> Ready -> continuous 2880 B WRITE_DONE -> StopIO -> drain -> DMAMAP unmap` lifecycles with `starved=0`, `pong_audio=0`, and zero speaker output.

## 2. 4-way auto-rotation closure (`545ab56`, `SENS-03`: `sens-s2-1..3`)

### Two independent root causes behind the 11-23 auto-rotation issues

1. **~61 s SLPI startup delay (`drivers/PongPower/ZizhanSlpi.cpp`, `ZizhanSlpiStd.h`, `userland/ui_compat18.c`)**:
   - `ZizhanSlpi` previously slept a fixed `25 s` after `PAS 12` `auth_and_reset` before `fastrpc_shell_3` and `20 s` before QMI `400` (`suid_lookup`). Polling SMP2P `ready` (`100 ms` steps, observed `~300 ms`) and QMI service `400` (`250 ms` steps, retaining a `250 ms` `fastrpc` `ping` floor so `apps_std` reads never miss SMD ring wakeups) reduced SLPI `auth_and_reset -> 52 Hz accelerometer streaming` from **`~61 s` to `~3.8 s`** (`sens-s2-2`: first sample at `11.1 s` monotonic, `z = 9.74 m/s^2`).
2. **Frozen / blank landscape scanout at `90°` and `270°` (`drivers/PongMetal/PongMetalPlugin.mm`)**:
   - `sens-s2-2` confirmed that `CoreMotion`, `backboardd`, and `SpringBoard` rotated cleanly through `wori = 1 (0°) -> 3 (90°) -> 2 (180°) -> 4 (270°)`, but `90°` and `270°` frames never reached scanout while `PONG GPU[42]` (`backboardd`) logged `150` `PastBuffer` (`x150`) texture creation refusals.
   - Disassembly of `22H374` `QuartzCore` (`LC_UUID FAF7D7EA-18F0-3C2D-816C-AC56D16C40A3`) localized two sites where `CA_DISABLE_FRAMEBUFFER_COMPRESSION=1` (`CADeviceUseFramebufferCompression == 0` and `CADeviceUseCompression == 0`) caused `QuartzCore` to assume an uncompressed A7–A11 Apple AGX GPU with hardware `-[MTLTextureDescriptor setRotation:]` ROP support:
     1. `CA::OGL::MetalContext::get_surface` (`0x18439940c`): when `([IOMFBDisplay + 0x290] & 0xfd) == 1` (`90°` or `270°`), it requested a swapped `1916x2160` (`setRotation:3`) texture over the `2160x1916` (`bpr = 8640`, `17,825,792 B`) scanout `IOSurface` (`2159 * 8640 + 1916 * 4 = 18,661,424 B > 17,825,792 B` -> `PastBuffer`), aborting every `WindowServer` frame at `90°`/`270°`.
     2. `CAMetalLayerPrivateNextDrawableLocked` (`0x18444e7dc`): allocated transposed `(height, width)` `IOSurface`s (`1916x728`, `bpr = 7680`, `5,591,040 B`) and requested `(width, height)` (`728x1916`) `MTLTexture`s (`1915 * 7680 + 728 * 4 = 14,710,112 B > 5,591,040 B` -> `PastBuffer`).
   - **Fix (`PongMetalPlugin.mm`, active in `sens-hal-v3`)**: UUID- and instruction-verified patches at `0x18439940c` and `0x18444e7dc` route `22H374` `QuartzCore` onto its own compressed-GPU unrotated surface path (`CoreAnimation` rotates via vertex projection into standard `(width, height)` `IOSurface`s; `zizhan_agx_texrot=1` restores stock behavior).

### Device verification (`sens-s2-3`, kernel `play164`)

- `Pong Metal: 22H374 unrotated Metal surfaces surf=1 layer=1` logged in `backboardd`, `SpringBoard`, and `Preferences`.
- `backboardd` `PastBuffer` refusals dropped **`150 -> 0`** (`c863/0 d863 p62 x7`, unchanged across rotation).
- All 4 orientations (`0°`, `90°`, `180°`, `270°`) visually verified in `Settings > General` split view (`sens-s2-3.shot2/4/6/8.png`, inter-orientation pixel diffs `7846–7909 / 16200`). `zizhan_slpi=4` stays opt-in pending a 2-minute physical handheld axis-sign check (`HUMAN_CHECKLIST.md`).

## 3. Screen-off standby input quiesce & battery health read-only audit (`184d8bb`, `POWER-03/04`, `POWER-01`)

### Changes

- **Standby touch scan stop & button poll relaxation (`patches/xnu-ipados18/0042-zizhan-standby-input-quiesce.patch`)**:
  - `bsd/dev/pong_fts.c`: `zizhan_touch_standby_ms` (default `100`, `0` restores legacy continuous 8 ms drain, capped at `1000`) sends `FTS_FW_SCAN_MODE = 0x00` (`scan_off=1`) when `pong_touch_set_paused(true)` enters standby and sleeps `100 ms` with **zero GPIO bit-bang SPI transfers**; on wake, `fts_scan_on()` (with `fts_system_reset() + fts_scan_on()` fallback) restores active scan and drains status events.
  - `bsd/dev/pong_buttons.c`: relaxes SPMI key observer polling from `16 ms` (`62.5 Hz`) to `64 ms` (`15.6 Hz`) while `pong_touch_standby_active()` is non-zero and all three keys are idle, returning to `16 ms` immediately on any key edge.
- **Read-only `AppleSmartBattery` diagnostic (`drivers/PongPower/PongBattery.cpp`)**:
  - `zizhan_batt_health_diag=1` (default off) logs published `PSProperties` keys, `BatteryData` fields, and `native_charger_stats[13]` authentic bits without fabricating battery health or serial state.

### Pure-battery A/B & wake verification (`play165` `57abe699ce2f`)

`ZIZHAN_USB_OFF_AT=15` cut VBUS ~25 s before `ZizhanPowerInject` pressed power OFF at `61 s`, measuring `180 s` (`36` 5-second samples) on battery only:

| Round | Setting | Screen-off FTS GPIO SPI polls (`60->240 s`) | All-off median (`n=36`) | Steady-state (`n=30`) med / mean / min | Wake + touch |
|---|---|---|---|---|---|
| `pwr-ab-legacy` | `zizhan_touch_standby_ms=0` (`scan_off=0`, `31.6 C`) | `750 -> 2250` (`+1500` polls) | `-416.0 mA` / `1796.3 mW` | `-415.0` / `-449.4` / `-400 mA` (`1791.6` / `1938.9` / `1728.0 mW`) | `1/1` off/on PASS |
| `pwr-ab-quiesce` | `zizhan_touch_standby_ms=100` (`scan_off=1`, `35.1 C`) | `445 -> 445` (**`+0` polls**) | `-424.0 mA` / `1827.4 mW` | `-424.0` / `-448.6` / `-407 mA` (`1826.6` / `1931.9` / `1754.6 mW`) | `1/1` off/on PASS |
| `pwr-wake-batt` | default `standby_ms=100` + `zizhan_power_inject=2` + `pong_touch_synth=8` | `scan_off=1` -> `resumed` (`0` errors) | USB on (`+546 mA`) | — | `1/1` off/on PASS, post-wake Today View swipe `6839/16200` px (`shot1..4.png`) |

- Eliminating 100% of screen-off FST2 GPIO SPI polls (`1500 -> 0`) and 75% of SPMI key polls is completely clean across sleep/wake, while total system standby draw (`~1.8 W` / `-415..-424 mA`, `mean 1938.9 vs 1931.9 mW`) remains governed by uncollapsed APSS/DRAM rails (`POWER-02` platform sleep).
- `PongBattery` publishes complete real `BatteryData` (`DesignCapacity=4500`, `NominalChargeCapacity=3435`, `CycleCount=916`, `98%`, `4428 mV`); on the shipping `iPad7,12` identity, stock iPadOS 18 `Settings > Battery` uses the standard iPad battery level/chart layout (the old `POWER-01` health-page spinner was specific to the retired `iPhone17,1` identity).

## 4. `backboardd` on-CPU zoom profiling & `PongAdreno` kernel optimization (`9c459a9`, `prof-bb-1..2`)

### Multi-thread `backboardd` IPI sampler (`patches/xnu-ipados18/0043-zizhan-sample-proc.patch`, `tools/zizhan/mac/sample-profile.py`)

- `zizhan_sample_proc=<name>` selects any target process (`backboardd` instead of `SpringBoard`), and `zizhan_sample_oncpu=1` rotates across active on-CPU threads in `task->threads` on each 2 ms cross-call (`cpu_xcall`) sample, capturing both even (`open_zoom`) and odd (`exit_zoom`) 640 ms windows without `thread_suspend` frame-rate perturbation.

### `backboardd` CPU & kernel breakdown during zoom (`prof-bb-1`, `play166` -> `prof-bb-2`, `play167`)

- **User vs kernel split**: during `640 ms` `open_zoom` windows, `backboardd` is active on CPU **`54%`** of wall time (`41%` user = `~261 ms`, `13%` kernel = `~84 ms`, `38%` waiting on VSYNC/IPC); during `exit_zoom` windows, **`66%`** (`50–53%` user = `~321–338 ms`, `16%` kernel = `~101 ms`).
- **User-mode call tree (`CA::WindowServer::IOMFBServer::vsync_callback` -> `render_for_time`)**:
  - `CA::OGL::Renderer::render` -> recursive `CA::OGL::LayerNode::apply` / `ImagingNode::render` (`37–44%` of user samples, including `5–9%` `capture_backdrop` + `GaussianBlurFilter` temporary blur surfaces) + `prepare_layers_roi` (`10%`) + `CA::Render::Update::add_context` (`5–7%`).
  - `CA::OGL::MetalContext::draw` (`28–30%` inclusive across user+kernel) + `MetalContext::flush` (`3.5–4.7%` inclusive) inside `PongMetalBridge.dylib` (`metal2vulkan`) and `libvulkan_freedreno.dylib` (`Turnip`); leaf user time is flat across `_xzm_xzone_malloc_tiny` / `_xzm_free` (`5.1–5.3%`), `QuartzCore` layer prep (`4.2–4.7%`), and `_platform_memmove` (`1.3–2.3%`).
- **Two kernel hotspots found in `prof-bb-1` and optimized in `9c459a9` (`drivers/PongAdreno/ZizhanSpanCollect.h`, `NativeAdrenoMemory.cpp`, `PongAdrenoCp.cpp`)**:
  1. **`client_healthy()` uncached SMMU+GX MMIO reads on every ioctl (`PongAdrenoCp.cpp`)**: `runCore()` called `faulted()` (`2` SMMU + `2` GX Device-nGnRnE MMIO reads) on every `BoAlloc`/`BoMap`/`BoFree`/`WaitFence`/`Submit` ioctl (`24` `smmu_read` samples in `prof-bb-1`). Rate-limiting healthy MMIO polls to `pong_gpu_health_us=500` (`2 kHz`) while keeping `gpu->poisoned` and IRQ-driven `gpu->irq_fatal` checks instantaneous on every call cut `client_healthy` `smmu_read` samples from **`20` to `0`** (total `smmu_read` **`24 -> 5`**, `-79%`).
  2. **Whole-segment physical span stride & compact inline allocation (`NativeAdrenoMemory.cpp`)**: `importTask()` and scattered `allocate()` now step by the contiguous `length` returned by `IOMemoryDescriptor::getPhysicalSegment()` (`pong_fast_spans=1`, `4x` fewer `dmaCommandOperation` walks on scattered 16 KiB user pages and `1` walk instead of `4352` on contiguous 17 MiB `IOSurface`s) and avoid `104 KiB` `kalloc_large` allocations for `<= 4`-span buffers (`adreno-span-collect.py` ASan/UBSan test).
  - In `prof-bb-2` (`play167`), `open_blank` median improved **`158.1 -> 133.1 ms`** (post-up `~73.1 ms`), zero-copy `fence wait avg` improved **`2717 -> 2584 us`**, and `latch wait avg` improved **`4294 -> 3928 us`** (`0` panics, `0` `NONE FREE`, `0` `SIGABRT`).
  - Remaining kernel CPU time during zoom (`~6%` inclusive) is XNU `DEVELOPMENT` `ml_set_interrupts_enabled_with_debug` (`8–10%`) and `IOGeneralMemoryDescriptor::wireVirtual` / `complete` (`upl_commit_range` `btref_get` backtrace packing) when CoreAnimation creates and releases temporary backdrop-blur `IOSurface`s per zoom.

## 5. 25-minute integrated all-defaults soak (`soak-1124`, `play168` `eafce0fd4d08`)

Ran all overnight subsystems together on `sens-hal-v3` + `play168` (`9c459a9`, silent `pong_audio=0`): default daily profile (thermal mode 2, display power, Hall follow, CPU `2..7` offline, standby input quiesce `zizhan_touch_standby_ms=100`) + `zizhan_power_inject=2` sleep/wake + continuous `tsa=6227` (`pong_touch_synth=8`) app open/exit zoom loop (`184` touch boosts) + silent `ZizhanAudio` CoreAudio HAL daemon (`zizhan_audio_service=1 zizhan_audio_prm=42 zizhan_audio_daemon_s=120`) + SLPI sensor/orientation bridge (`zizhan_slpi=4`) + continuous Wi-Fi `ping` (`PING=1`, `pong_autoreset=1620 pong_autoreset_sb=1500`):

- **Zero faults / crashes**: `0` panics, `0` user `SIGABRT`, `0` `canvas slot NONE FREE`, `0` `PastBuffer`, `0` FTS touch errors (`14,880+` polls), automatic deadline return to Android (`WAIT_ANDROID: PASS`, `0.195 s`).
- **Display & GPU (`PONG ZC` / `PONG GPU`)**: `27,932` zero-copy frames presented (`mode 2 saved 2,330` cross-frame fence chases; `fence wait avg 3.29 ms / max 23.80 ms`, `latch wait avg 4.23 ms / max 8.61 ms`, `0` timeouts); `768 MiB` BO pool peaked at `695 MiB` (`631 MiB` steady) with **`0` pool fallbacks (`scattered +0`)** and `11,229` clean GMU DCVS transitions (`0` failed).
- **Sensors (`zizhan_slpi=4` alongside ADSP)**: continuous 25-minute coexistence (`t = 1,490,000 ms`), `74,345+` 52 Hz accelerometer samples (`x=871, y=543, z=9881 mm/s^2`), `glink rx=104,230 / tx=104,205`, `fastrpc served=5,989`, `missing=0`, `fail=0`.
- **Wi-Fi (`PING=1`)**: `1,352` ICMP replies over `1,361 s` (`99.3%`), **`0` gaps `> 2 s`**, median RTT `106.6 ms`.
- **Thermals & battery**: peak CPU `79.7 C`, peak GPU `66.3 C` (`capped windows 0/23`), battery `100% -> 100%`.

## Rollback switches

| Subsystem / change | Default on `wip/2026-11-17-audio-sensors` | Legacy rollback boot-arg |
|---|---|---|
| CoreAudio HAL service & `bindfs` overlay (`0041`) | opt-in (`zizhan_audio_service=1 zizhan_audio_prm=42`, `pong_audio=0`) | omit `zizhan_audio_service` |
| `SBVolumeControl` repeat disarm (`ui_compat18.c`) | on | `pong_vol_norepeat=0` |
| `22H374` unrotated Metal surfaces (`PongMetalPlugin.mm`) | on (`sens-hal-v3`) | `zizhan_agx_texrot=1` |
| SLPI sensor & orientation bridge (`ZizhanSlpi.cpp`) | opt-in (`zizhan_slpi=4`) | omit `zizhan_slpi` |
| Screen-off FST2 scan stop & button 64 ms poll (`0042`) | on (`zizhan_touch_standby_ms=100`) | `zizhan_touch_standby_ms=0` |
| Adreno whole-segment span stride (`NativeAdrenoMemory.cpp`) | on (`pong_fast_spans=1`) | `pong_fast_spans=0` |
| Adreno `client_healthy()` MMIO rate-limit (`PongAdrenoCp.cpp`) | on (`pong_gpu_health_us=500`) | `pong_gpu_health_us=0` |
