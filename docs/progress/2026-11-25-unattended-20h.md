# 2026-11-25: 20-hour unattended session — multi-session CS35L41 daemon, single-step volume keys, live FTM5 cover desktop, SLPI ALS/SUID/quiesce, and endurance soak

Unattended engineering and hardware verification session on branch `wip/2026-11-17-audio-sensors` (`iPad7,12` identity, active OS trial `sens-hal-v3`, OS `6ec87b7cab84`, UI `78d94d15d55c`, helper `14653f8d151c`). Every unattended device round ran **100% silent** (`audio_output=0`; `zizhan_pulse_dry=1` kept CS35L41 `AMP_EN=0` and `BST_EN=0`), used temporary `fastboot boot` RAM images only (`0` partition writes, `main` untouched at `dac504b`), and enforced thermal/battery preflight gates (`T <= 38.0 C`, `level >= 35%`).

## 1. Root-cause & fix for `loud-2`: multi-session CS35L41 daemon mode & single-step volume pacing (`c023b77584a6b0bdc8897cf7f78f1ba50b301285`)

### Attended `loud-2` symptom & two root causes

During the brief attended `loud-2` check (`PRM 41` 1 s speaker test), no audible sound played while physical volume keys adjusted smoothly without jumping to 100%, except each short press stepped slightly more than one tick:

1. **Single-shot `PRM 41` slot consumed by `audiomxd` at boot (`drivers/PongPower/ZizhanAudioPrm.cpp`, `ZizhanAudioTransfer.h`, `ZizhanAudioSilent.h`, `ZizhanCs35l41.{h,c}`, `ZizhanSe15Prepare.cpp`)**:
   - In iPadOS 18.7.10 (`22H374`), `audiomxd` opens a short zero-frame probe IO session on `ZizhanAudio.driver` during desktop startup (`StartIO -> StopIO` with `0` nonzero frames). Single-shot `PRM 41` treated that first `Start` as its sole playback window and tore down the graph before any user or test playback session started.
   - **Fix (`M1-DAEMON-AMP`)**: wired `ZizhanAudioAmpSession` (`arm` -> 50 ms `poll` checking sticky/raw CS35L41 fault registers and `HALO_STATE == 2` -> `disarm`) into persistent multi-session `PRM 42` (`daemon_mode_round`, `clocked_profile=20`) whenever `zizhan_audio_attended=1` or `zizhan_pulse_dry=1`. Idle boot probes (`idle_stop=1`) release cleanly without consuming or tearing down the dual CS35L41 HALO DSP session, while every subsequent real `StartIO` arms (`dry=1` silent when `zizhan_pulse_dry=1`; `AMP_EN=1` + `BST_EN=1` only when `zizhan_audio_attended=1` without `zizhan_pulse_dry=1`) and disarms on `StopIO`.
2. **`SBVolumeControl` internal ~75 ms hold-repeat firing 2–3x during a 150–200 ms human click (`patches/xnu-ipados18/0044-zizhan-volume-key-click-pace.patch`, `drivers/ArmDTPlatform/ZizhanVolumeInjectPlan.h`, `ZizhanVolumeInject.h`)**:
   - Even with `cancelVolumeEvent` disarming repeat on key-up, holding a physical PMIC key for a normal `150–200 ms` human click allowed `22H374` `SBVolumeControl`'s internal short-delay timer to step `2–3` ticks (`12.5–18.75%`) before key-up arrived.
   - **Fix (`IO-01-PACE`)**: `pong_volume_key_sample` (`zizhan_vol_pace=1` default, `=0` restores raw hold edges) emits an atomic 1-step `DOWN+UP` pulse immediately on physical press, suppresses extra steps below a `400 ms` hold threshold, and paces sustained holds (`>= 400 ms`) at `1` step per `112 ms` (`~8.9 steps/s`).

### Silent hardware verification (`hal-amp-dry-1`, kernel `play169` `53d1ce2f2ca6`)

- `session=1` (`audiomxd` desktop startup probe) exited cleanly as `idle_stop=1` without consuming or stopping the persistent `PRM 42` / CS35L41 session.
- `session=2` (`184,320` frames) and `session=3` (`196,320` frames) streamed consecutively with `starved=0`: `ZIZHAN_AUDIO_DAEMON_AMP: PASS sessions=2 arms=2 disarms=2 ready=1/1 streamed=1 polls=156 reason=0 ... dry=1` and `ZIZHAN_AUDIO_PRM: PASS` (`audio_output=0`).
- `zizhan_vol_inject=3`: two `192 ms` human `Vol-` clicks emitted strictly `1` pulse each (`pulses=1`), and one `560 ms` `Vol+` hold stepped `ZizhanAudio HAL: volume set` `-48.00 -> -45.00 -> -42.00 -> -39.00 dB` (**exact `3.00 dB = 1/16 = 6.25%` per step**).

## 2. Lid-gated live `FTM5` cover touch & 2:1 interactive cover desktop (`cda7ae317e73d11f5e7025408e414c57e47af149`)

### Implementation (`M2`)

1. **Pure C++17 `FTM5` (`st,spi-sec`, chip `0x3640`) protocol, viewport mapper & SWAR blitter (`drivers/ArmDTPlatform/ZizhanOuterTouchPlan.h`, `tools/zizhan/tests/outer-touch.{cpp,py}`)**:
   - Decodes `0x87` burst FIFO status/pointer frames (`0x13` enter, `0x23` motion, `0x33` leave, `0xb5` release-all, `0x03` controller ready), supporting both `16-bit` super-res (`10800x25200`) and `12-bit` (`1080x2520`) coordinates while filtering hover/palm touches.
   - Maps the centered `1080x958@y781` cover desktop viewport onto `2160x1916` inner coordinates with `96 px` top/bottom bezel gesture clamping so Control Center pull-down and Home-bar swipe-up work on the letterboxed cover panel.
   - Implements a UTC+8 calendar helper (`MM-DD HH:MM`) and a 64-bit SWAR 2x2 box-filtered `2160x1916 -> 1080x958` BGRA downscaler.
2. **Lid-gated `FTM5` lifecycle & `zizhan_outer_fold=1/2` (`drivers/ArmDTPlatform/ZizhanOuterProbe.h`, `ZizhanDailyProfile.h`, `ZizhanDisplayPowerService.h`, `ZizhanHallProbe.h`, `drivers/PongAdreno/PongAdrenoScanout.cpp`)**:
   - `zizhan_outer_touch=1` (daily default `kOuterTouch=1`) performs zero hardware touches while unfolded; on fold (`closed=1`), enables `WL2866D` `DVDD2=1.8V` + `AVDD2=3.0V` (preserving outer panel bits `0x05`), initializes `FTM5` (`0x3640`) over `QUP2 SE19 SPI`, polls `GPIO119` at 125 Hz, and cleanly powers down + restores rails and GPIO pads on unfold (`restored=1`).
   - `zizhan_outer_fold=1` (daily default): draws `MM-DD` + battery pill & `%` + large `HH:MM` on `0xb8fc9900` when folded and clears to black (`ad495edf`) when unfolded.
   - `zizhan_outer_fold=2` (opt-in live interactive cover desktop): `zizhan_inner_panel_cover_mute(1)` puts **only** the inner 8.02" OLED, backlight, and inner `FST2` touch to sleep while keeping `zizhan_display_power_state() == 1` (CPUs, Adreno, and Wi-Fi stay awake), and `pong_scanout_set_cover_redirect(1)` redirects `zcFlip`/`finish` via a wait-free seqlock (`pong_scanout_cover_snapshot`) onto the 6.56" cover framebuffer without `CTL0` latch timeouts.

### Hardware verification (`outer-s2-1` & `outer-s2-2`, kernel `play170` `9b4e315d4387`)

- `outer-s2-1` (`zizhan_outer_fold=1`): `ZIZHAN_OUTER_FOLD: closed clock 11-25 01:23 batt=100 chg=1 misr=3d826258`, `ZIZHAN_OUTER_TOUCH_LIVE: UP ok=1 id=3640 sysinfo=a501/19x0 super_res=1 max=10800x25200 irq=1 map_mode=1` -> `DOWN ... restored=1`, `open black misr=ad495edf`.
- `outer-s2-2` (`zizhan_outer_fold=2`): `ZIZHAN_DISPLAY_POWER: cover-mute PASS target=off saved=1024 gpio86_edges=0 samples=5000 state=1`, `ZIZHAN_OUTER_DESKTOP: SUMMARY blits=16 seq_first=303 seq_last=356 first_misr=5514bd87 last_misr=919b9928 distinct_misrs=5 open_misr=ad495edf`, `cover-mute PASS target=on restored=1024 dsi=0 bl=0 gpio86_edges=34 state=1`, `1,882` zero-copy frames with **`0` fence timeouts and `0` latch timeouts**.

## 3. SLPI 8-sensor SUID inventory, ambient light auto-brightness & standby quiesce (`1d50a638fc4b37a337b0d79f113e29df6ab4059b`)

### Implementation (`SENS-04/05`)

- **Pure C++17 sensor helpers (`drivers/PongPower/ZizhanSlpiSensors.h`, `tools/zizhan/tests/slpi-sensors.{cpp,py}`)**:
  - Soft-float IEEE-754 `milli_from_f32` converter (safe in XNU C++ without FP traps), perceptual L18 OLED `lux_to_dbv` curve (`56..2047` DBV across `0..10000+ lux`) with `>= 25%` lux change + `2.5 s` anti-flicker hysteresis and manual slider override detection (`|current_dbv - last_auto_dbv| > 96`), plus `QuiesceState` (`500 ms` screen-off stream pause, `250 ms` worker poll relaxation, `0 ms` instant wake resume).
- **SLPI multi-sensor discovery, ALS/gyro streams & HID bridge (`drivers/PongPower/ZizhanSlpi.cpp`, `userland/touch.{h,c}`)**:
  - Queries SUIDs for all 8 sensor types (`registry`, `accel`, `ambient_light`, `gyro`, `mag`, `pressure`, `proximity`, `hinge_angle`).
  - Streams `ambient_light` (`SNS_STD_SENSOR_MSGID_SNS_STD_ON_CHANGE_CONFIG = 514` with `513` fallback) -> `zizhan_sensor_post(17, ...)` -> `IOHIDEventCreateAmbientLightSensorEvent` (`0xff00`/`0x04`), with opt-in kernel OLED auto-brightness (`zizhan_als_autobright=1`) and opt-in 52 Hz gyroscope (`zizhan_gyro=1`, `zizhan_sensor_post(18, ...)`).
  - Default-on screen-off SLPI stream quiesce (`zizhan_slpi_quiesce=1`, `=0` disables) closes active sensor client streams `500 ms` after `zizhan_display_power_state() == 0` and re-subscribes immediately on wake.

### Hardware verification (`sens-s3-1`, kernel `play171` `82f2f8e3b6e2`)

- **All 8 SLPI sensor types discovered (`sensors=1` for each)**: `registry` (`e12754a7...`), `accel` (`1fbb6afc...`), `ambient_light` (`c2d1027d...`), `gyro` (`97d3c489...`), `mag` (`ebd5604d...`), `pressure` (`00572995...`), `proximity` (`e0e3f71b...`), and `hinge_angle` (`72e3913a...`).
- Simultaneous `accel` (`5,033` samples), `gyro` (`rx=10 ry=-4 rz=-1 mrad/s accuracy=3`), and `ambient_light` (`als #1/#2 lux_milli=0 accuracy=3` in dark room) streamed cleanly; **`ZIZHAN_SLPI: als-autobright lux_milli=0 bl 1024 -> 56 rc=0 updates=1`** dimmed physical L18 OLED brightness from `1024` to `56` DBV (`rc=0`).
- Across lid sleep/wake: `ZIZHAN_SLPI: standby quiesce streams=off pauses=1 resumes=0` froze `accel=2588` / `gyro=2588` completely during standby, and `ZIZHAN_SLPI: wake resume streams=on pauses=1 resumes=1` immediately resumed `als #2` + `accel`/`gyro` on wake (`Wi-Fi ping 107/107 s`, `0` panics).

## 4. Pure-battery multi-subsystem standby & 3x fold/cover-desktop stress (`pwr-s4-1`, `fold-stress-1`, `play171`)

- **Pure-battery 210 s standby with `zizhan_slpi=4` + `PRM 42` + `zizhan_outer_touch=1` (`pwr-s4-1`, `ZIZHAN_USB_OFF_AT=15`, `42` 5 s screen-off samples)**:
  - Display off triggered `ZIZHAN_SLPI: standby quiesce streams=off pauses=1` (`accel=308` frozen across the entire standby window) + `ZIZHAN_CPU_OFFLINE: offline_mask=fc in 0 ms` + `scan_off=1`.
  - Once the bounded `ZizhanAudio` `PRM 42` test completed (`ZIZHAN_AUDIO_DAEMON: SUMMARY PASS`), pure-battery discharge settled immediately to **`-428 .. -435 mA` (`median -434.0 mA`, `median 1886.0 mW`, `min 1737 mW`)** — within `~10 mA (< 0.05 W)` of the bare no-SLPI/no-audio baseline (`-415..-424 mA`), proving near-zero idle standby penalty from `zizhan_slpi=4` (with quiesce) + idle `PRM 42` + idle `zizhan_outer_touch=1`.
- **3-cycle fold / cover-desktop / `FTM5` / unfold stress (`fold-stress-1`, `zizhan_hall_simulate_cycles=3`)**:
  - Completed `3/3` full cycles (`inner cover-mute off` -> live `ZIZHAN_OUTER_DESKTOP` `blits=156, 151` with `distinct_misrs=35` + `FTM5` `0x3640` power-up & synthetic cover touch -> `FTM5` power-down `restored=1` + outer black `ad495edf` + inner OLED wake `gpio86_edges=46`), continuous Wi-Fi `ping` `98/98 s` (`0` gaps `> 2 s`), `0` panics.

## 5. 30-minute integrated all-subsystems endurance soak (`soak-1125`, `play172` `d697c9fa87e0`)

Ran all new and baseline subsystems together for 30 minutes (`pong_autoreset=1800 pong_autoreset_sb=1680`) on clean commit `1d50a638fc4b37a337b0d79f113e29df6ab4059b` (`build-ci172` + `build-play172` `d697c9fa87e0`, active OS trial `sens-hal-v3`, **100% silent** `zizhan_pulse_dry=1`, `audio_output=0`):

- **Zero faults / crashes**: `0` panics, `0` user `SIGABRT`, `0` `PastBuffer`, `0` `canvas slot NONE FREE`, automatic deadline return to Android (`WAIT_ANDROID: PASS`, `0.13 s`).
- **Multi-session CS35L41 + ADSP daemon (`PRM 42`, `zizhan_pulse_dry=1`)**: `ZIZHAN_AUDIO_DAEMON_PRM: invoked=1 amp_pass=1 pulse_verified=1 release_safe=1 data_connected=1 data_restored=1 ok=1` and `ZIZHAN_AUDIO_DAEMON_RUN: PASS acks=12 sessions=1 idle_stops=1 frames=130080 nonzero=42114 max_abs=348 starved=0 polls=53 poll_stops=0 amp=1 mapped=1 restored=1 ... audio_output=0 finished=1` -> `ZIZHAN_AUDIO_PRM: PASS`.
- **3-cycle live cover desktop + `FTM5` cover touch + inner cover-mute (`zizhan_outer_fold=2`)**: `ZIZHAN_OUTER_DESKTOP: SUMMARY` (`blits=213, distinct_misrs=50` and `blits=56, distinct_misrs=9`, `open_misr=ad495edf`), `ZIZHAN_OUTER_TOUCH_LIVE: UP ok=1 id=3640 ... map_mode=2` -> `DOWN events=3 posted=3 restored=1`, and `ZIZHAN_DISPLAY_POWER: cover-mute PASS target=off/on` (`gpio86_edges=0` muted -> `46/52` on wake).
- **SLPI multi-sensor + standby quiesce (`zizhan_slpi=4 zizhan_als_autobright=1 zizhan_gyro=1`)**: **`83,406` accelerometer + `83,406` gyroscope + `509` ambient-light samples** (`lux_milli=0..3000`) with **`quiesced=0(2/2)`** (`2` clean screen-off stream pauses and `2` instant wake resumes alongside ADSP).
- **GPU, Wi-Fi & thermals**: `261` 5 s display windows (`median 107 frames/5 s`), `12,449` clean GMU DCVS transitions, **`1,459 / 1,469 s` (`99.3%`) ICMP replies with `0` gaps `> 2 s` (`median RTT 106.0 ms`)**, peak CPU `74.2 C`, peak GPU `64.7 C` (`capped windows 0/151`), battery `100% -> 100%`.
- **Attended scripts prepared (host-only packaged on `play172`, gated by `ZIZHAN_USER_PRESENT=yes`)**: `work/m1-firstsound/run-video-1.sh` (10 min speaker audio + 1-step volume keys + 4-way rotation + ALS auto-brightness + cover clock) and `work/m1-firstsound/run-fold-desk-1.sh` (10 min live 2:1 outer cover desktop + `FTM5` touch).

## Rollback switches

| Subsystem / change | Default on `wip/2026-11-17-audio-sensors` | Legacy rollback boot-arg |
|---|---|---|
| Multi-session CS35L41 daemon (`PRM 42`) | opt-in (`zizhan_audio_service=1 zizhan_audio_prm=42`) | `zizhan_audio_prm=41` or omit `zizhan_audio_service` |
| Single-step physical volume key pacing (`0044`) | on (`zizhan_vol_pace=1`) | `zizhan_vol_pace=0` |
| Lid-gated `FTM5` cover touch (`ZizhanOuterProbe.h`) | on in daily profile (`zizhan_outer_touch=1`) | `zizhan_outer_touch=0` |
| Cover display mode on fold (`ZizhanOuterProbe.h`) | clock + battery (`zizhan_outer_fold=1`); `=2` live 2:1 desktop | `zizhan_outer_fold=0` |
| SLPI screen-off stream quiesce (`ZizhanSlpi.cpp`) | on when `zizhan_slpi=4` (`zizhan_slpi_quiesce=1`) | `zizhan_slpi_quiesce=0` |
| SLPI ALS OLED auto-brightness & gyro (`ZizhanSlpi.cpp`) | opt-in (`zizhan_als_autobright=1`, `zizhan_gyro=1`) | omit or `=0` |
