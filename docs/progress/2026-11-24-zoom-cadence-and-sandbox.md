# 2026-11-24: app open/exit zoom cadence, zero-copy fence snapshot, and minisandbox extension_issue

Unattended rounds `uxz-1..uxz-5` on the iPad7,12 identity (kernels `play158..160`, active OS trial `sens-hid-v9`), compared against the retained `2026-11-23` round-6 baselines `ux-p4`, `ux-p5`, and `uxt-2` (`pong_touch_synth=6 touch_lat=1 pong_autoreset=420 pong_autoreset_sb=360 tsa=6227`). Silent arms only (`pong_audio=0`), temporary `fastboot boot` only, no partition writes.

## Why the single max-gap metric missed the zoom stutter

`tap-stall.py` measures the single longest inter-frame gap after an icon tap (~175 ms, which includes the synthetic 60 ms press hold before finger-up) and after swipe-home touch-down (~58 ms). Separating each cycle into four windows (`tools/zizhan/mac/zoom-cadence.py`: `open_blank`, `open_zoom`, `exit_zoom`, `home_settle`) exposed three independent issues hidden behind that single number:

1. **Cross-frame / cross-process fence chasing in `zcFlip()` (`drivers/PongAdreno/PongAdrenoScanout.cpp`)**:
   - `zcFlip()` called `pong_gpu_fences(&submitted, &retired)` both at entry (`gGtRing->lastSubmitted()`) and **again on every 100 µs poll iteration**, overwriting `submitted` whenever `backboardd` (already encoding frame `N+1` into a second canvas) or a foreground app submitted new GPU work onto `gGtRing`.
   - Any multi-layer zoom frame taking ~10–12 ms on GPU chased newer fences up to **42.1–42.7 ms**, holding `MiniIOMobileFramebuffer::fLock` + `scanout_gate`, then flushing `N+1` 8.3 ms later — producing pairwise `[42–50 ms, 8.3 ms]` bunching across **21–25%** of both `open_zoom` (`53.2–53.5 fps`) and `exit_zoom` (`48.8–51.5 fps`) frames.
2. **`MobileNotes` `SIGABRT` every ~6.3 s after launch (`bsd/kern/kern_minimac.c`)**:
   - Symbolicating the `MobileNotes` crash stacks in `ux-p5.txt` against `22H374` (`dsc-22H374`) traced every abort to `AppIntents` (`-[LNClientConnection fetchAppShortcutParametersWithCompletionHandler:]`) -> `LinkMetadata` -> `Foundation` (`LocalizedStringResource.encode(to:)`, `0x18183e078`) -> `sandbox_extension_issue_file_to_process` -> `libsystem_sandbox.dylib::__sandbox_extension_issue` (`0x1e130ef2c`).
   - `__sandbox_extension_issue` allocates its 2048-byte destination buffer with `malloc_type_malloc(2048)` (uninitialized) and invokes `__mac_syscall("Sandbox", 5, ...)`. `minisandbox_policy_syscall` returned `0` without writing `dest`, returning recycled non-UTF-8 heap bytes; `-[NSString initWithCString:encoding:NSUTF8StringEncoding]` then returned `nil`, throwing `"Unable to create a string suitable for sandbox extension."` (`NSInvalidUnarchiveOperationException` -> `SIGABRT`, 11–12 crashes per round).
   - Because `MobileNotes` died on every cycle, every subsequent tap in `ux-p4/ux-p5/uxt-2` paid a cold `launchd` spawn + dyld + Metal setup penalty (`open_blank` p90 `207.9–216.2 ms`).
3. **Zero-copy canvas slot exhaustion (`NONE FREE`, 8–11 per round)**:
   - With `pong_dpu_bufs=2`, the 128 MiB loader panel pool (`7` 17 MiB strides) held `2` copy buffers + `5` zero-copy canvas slots (`active 5/5` whenever `SpringBoard`, `backboardd`, and two apps held triple-buffered full-screen surfaces). Under `pong_zc=2`, `db[0]` (the inherited splash buffer) plus `1` pool buffer already double-buffers early-boot copy frames, freeing a **6th** zero-copy canvas slot (`pong_dpu_bufs=1`).

## Changes

- **Per-swap compositor fence snapshot (`drivers/PongAdreno/ZizhanZcFence.h`, `NativeAdrenoSubmit.cpp`, `PongAdrenoScanout.cpp`, XNU patch `0039-zizhan-swap-fence-snapshot.patch`)**:
  - `NativeAdrenoSubmit` records `native_adreno_compositor_fence` atomically when `backboardd` submits on `gGtRing`.
  - `MiniIOMobileFramebuffer::present()` snapshots `pong_gpu_present_fence()` into `fSwapTargetFence[slot]` (guarded by `#if XNU_PONG_ADRENO_PLATFORM`) and passes it into `pong_scanout_set_fence()` before `show()`.
  - `zcFlip()` polls `retired` against that fixed `target` without re-reading `submitted` inside the poll loop (`pong_zc_fence_mode`, default `2 = SwapCompositor`; `0` restores legacy moving-fence behavior).
- **`minisandbox` request 5 stand-in token (XNU patch `0040-zizhan-minisandbox-extension-issue.patch`)**:
  - `minisandbox_extension_issue()` validates the 56-byte `22H374` argument block and `ext_class` C-string, then writes `"invalid"` — the same canonical stand-in token `__sandbox_extension_issue()` emits locally for `SANDBOX_EXTENSION_CANONICAL` (`flags & 1`) and `_sandbox_extension_consume()` accepts in user space without a syscall (`pong_sbext=0` restores legacy no-op).
- **Default 6 zero-copy canvas slots (`drivers/PongPower/ZizhanPanelPool.h`, `drivers/PongAdreno/PongAdrenoScanout.cpp`)**:
  - `zizhan_panel_pool::plan` accepts explicit `requested_buffers == 1` (`0` still defaults to `2`); `dbInit()` defaults `want = (zc >= 2) ? 1 : 2` when `pong_dpu_bufs` is unset (`1` pool copy buffer + `6` zero-copy slots in 128 MiB).
- **Host tests & tooling (`tools/zizhan/tests/zc-fence-target.{cpp,py}`, `panel-pool.cpp`, `tools/zizhan/mac/zoom-cadence.py`, `cycle.sh`)**:
  - `zc-fence-target.py` and `panel-pool.py` pass under Clang and GCC 14 with ASan/UBSan (wired into `check-daily-use-host.py`).
  - `cycle.sh` caps post-`RamoopsDump` USB VBUS kicks to at most 2 kicks spaced by 300 s and supports optional `ZIZHAN_ANDROID_RETURN_IMG` direct Android RAM boot when `klog` is armed.

## Device A/B progression

| Round | Configuration | `open_blank` med / p90 (post-up med) | `open_zoom` fps / `>33ms` | `exit_zoom` fps / `>33ms` | `home_settle` fps / `>25ms` | `NONE FREE` | `SIGABRT` | `zc_fence` max (`saved`) |
|---|---|---|---|---|---|---|---|---|
| `ux-p4` | 11-23 baseline | 191.2 / 216.2 ms (~131.2 ms) | 53.2 / 23% | 48.8 / 25% | 90.2 / 0% | 8 | 11 | 42.1 ms (—) |
| `ux-p5` | 11-23 baseline | 178.8 / 207.9 ms (~118.8 ms) | 53.5 / 22% | 51.5 / 21% | 96.5 / 0% | 10 | 12 | 42.7 ms (—) |
| `uxt-2` | 11-23 baseline | 166.3 / 207.9 ms (~106.3 ms) | 53.5 / 21% | 50.0 / 22% | 88.7 / 0% | 9 | 11 | 42.7 ms (—) |
| `uxz-1` | `play158`: `+ 0039` (`SwapCompositor` fence) | 158.0 / 207.9 ms (~98.0 ms) | **59.8 / 5%** | **56.8 / 11%** | 91.9 / 0% | 7 | 7 | **21.3 ms** (1371) |
| `uxz-2` | `play159`: `+ 0040` (`pong_sbext=1`) | **149.6 / 158.1 ms** (~89.6 ms) | **57.9 / 5%** | 54.6 / 12% | **95.2 / 0%** | 11 | **0** | 22.4 ms (665) |
| `uxz-3` | `play159`: `+ pong_dpu_bufs=1` (6 ZC slots) | **141.5 / 158.0 ms** (~81.5 ms) | **59.4 / 4%** | **54.9 / 10%** | 91.1 / 0% | **0** | **0** | **20.8 ms** (716) |
| `uxz-4` | `play159`: `+ pong_dpu_bufs=1 pong_zc_async=1` (rejected) | 150.8 / 161.2 ms (~90.8 ms) | 48.9 / 8% (`>25ms` 31%) | 45.8 / 19% (`>25ms` 33%) | 92.1 / 2% | 0 | 0 | 22.0 ms (728) |
| `uxz-5` | `play160`: shipping defaults (`0039` + `0040` + default 6 ZC slots) | **149.7 / 166.3 ms** (~89.7 ms) | **58.6 / 5%** | **54.3 / 11%** | **95.7 / 0%** | **0** | **0** | **21.2 ms** (766) |

### Why `pong_zc_async=1` (`uxz-4`) was rejected

Decoupling DPU TE latch waiting from `SwapEnd` reduced `show_wait` (`13.7 -> 11.7 ms` in `open_zoom`, `6.1 -> 1.9 ms` in `home_settle`), but removed VSYNC phase pacing from `backboardd`: median zoom intervals drifted off the 8.33 ms TE grid (`16.6 -> 17.2 / 18.2 ms`), `>25 ms` frames jumped from `7% / 13%` to `31% / 33%`, and overlapping `N+1` GPU encoding with `N` scanout raised GPU fence wait slightly. `pong_zc_async` stays `0` by default.

## Remaining limits & future directions

- **Warm post-finger-up launch gap (~80–90 ms median, p25 ~65 ms)**: with `MobileNotes` no longer crashing, the remaining ~80 ms is `SpringBoard` (`-[SBFluidSwitcherViewController performTransitionWithContext:...]`) + `CoreAnimation` commit on CPU before the first zoom frame submits.
- **Multi-layer zoom rate (~55–60 fps vs 91–96 fps home settle)**: during full-screen zoom transitions, `backboardd`'s `metal2vulkan` CPU translation + Adreno rendering (~10–13 ms combined with synchronous TE latch) steps cleanly to 60 Hz (`16.6 ms` median) rather than 120 Hz (`8.3 ms`), with ~10% 2-frame (`33.2 ms`) steps on `exit_zoom` when `SpringBoard` updates switcher snapshots. Moving zoom from 60 fps toward 120 fps will require either `IOSurfaceSharedEvent` GPU-side synchronization or reducing per-pass `metal2vulkan` descriptor/command overhead.
- **Attended visual check**: all `uxz-1..5` metrics are unattended `touch_lat=1` / `PONG ZC` measurements; a quick hands-on feel check is listed in `HUMAN_CHECKLIST.md`.
