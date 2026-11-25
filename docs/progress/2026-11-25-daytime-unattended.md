# 2026-11-25 白天无人值守收口：CS35L41 4.34 s 快启、外屏时钟/实时桌面双击切换、六轴+磁力+气压+转轴传感器与纯电池待机优化

本轮在用户白天离场期间全程执行**静音无人值守工程与实机闭环验证**（所有音频测试均强制 `zizhan_pulse_dry=1`、`audio_output=0`；全程仅 `fastboot boot` 临时启动 RAM 镜像，零分区刷写、零 `/data` 系统镜像改动，`main` 保持 `dac504b` 不变）。

## 1. M1-FASTBOOT：双 CS35L41 固件与校准突发加载提速 7.3 倍（`612d919`）

- **问题**：此前 `hal-amp-dry-1` 中两颗 CS35L41 保护固件（`59,752 B`）与校准系数（`7,532 B`）通过 `100 kHz` GENI I2C 逐笔 4 字节标量写入（共 `21,072` 次 I2C 事务），仅固件与系数阶段就耗时 `24.66 s`（`ram_us=21.72 s`、`coeff_us=2.94 s`），整链路冷启动需等约 `31.5 s` 才能出声。
- **改动**：
  - `GeniI2c.{h,c}`：在深度为 16 字（64 字节）的硬件 TX/RX FIFO 内新增多元组突发读写入口 `geni_i2c_start_write_burst`（最高 `52 B`）与 `geni_i2c_start_read_burst`（最高 `48 B`），原有 `1..8 B` 与 `12/20 B` 接口保持不变。
  - `ZizhanGeniTiming.h`：为 `SE15` 模式 40（`PRM 41/42`）启用 `400 kHz` Fast-mode 时序（`GENI_CLK_CFG=0x21`、`GENI_SCL_COUNTERS=0x503018`），诊断模式 `1..39` 保持 `100 kHz` 不变。
  - `ZizhanCs35l41.{h,c}` 与 `ZizhanSe15Prepare.cpp`：新增 `zizhan_cs35l41_ram_fast` 与 `zizhan_cs35l41_coefficients_fast`（`48 B` XM/YM 与 `40 B` PM 突发写读，`zizhan_amp_fast=0` 可回退旧路径），保持每颗芯片 `7,469` 个固件字与 `933/950` 个校准字 **100% 逐字节读回校验**；`ZizhanAudioRing.h` 新增左右声道独立峰值 `max_lr` 统计。
- **实机闭环（`amp-fast-1`，`play173` `86d6588190f9`）**：
  - `ZIZHAN_SE15_PREP: bringup_ms=4336 fast=1 ram_us=3556857 ram_bursts=1420/1420 coeff_us=461661 coeff_bursts=157/157`：双功放完整冷启动时间从 **约 `31.5 s` 缩短至 `4.34 s`**（RAM 加载 `21.72 s → 3.56 s`、系数加载 `2.94 s → 0.46 s`，`mismatches=0`）。
  - 同一 65 s 静音守护窗口内连续完成 `3` 轮 CoreAudio HAL 音频流会话（`581,280` 帧，`max_lr=183/183`，`starved=0`，`audio_output=0`），`ZIZHAN_AUDIO_PRM: PASS`，`0` panic。

## 2. M2-UX：外屏低功耗时钟 ↔ 2:1 实时桌面双击唤醒与底部锁屏胶囊回切（`862b290`）

- **改动**：
  - `ZizhanOuterTouchPlan.h`：在 `ClockWakeOnly` 模式加入双击检测器（双指尖单击间隔 `40..500 ms`），在 `DesktopViewport` 模式底部黑边区（`x=360..720, y=1880..1980`，位于主屏上滑手势区 `y≤1835` 下方）加入锁屏/时钟胶囊命中检测；两者均在抬手 `PointerLeave` 触发，切换瞬间 `down_mask=0` 零残留触点。
  - `ZizhanOuterProbe.h` 与 `ZizhanHallProbe.h`：打通 `zizhan_outer_fold=1`（日常默认）下的双态无缝切换——合盖默认进入低功耗外屏时钟（内屏物理断电、`CPU 2..7` 下线、`SLPI` 停流）；**双击外屏**先置位 `cover-mute` 与 `cover_redirect` 再唤醒系统，使 iPadOS 在**完全不点亮 8.02" 内屏 OLED 与背光**的前提下直接在外屏呈现 2:1 实时交互桌面与底部锁屏胶囊；**点击底部胶囊**立即切回外屏时钟并重新进入熄屏低功耗；任意状态下展开手机均自动恢复内屏显示与触控。同时新增静止帧跳过计数 `idle_skips` 与 `zizhan_outer_touch_synth=2` 全链路自检。
- **实机闭环（`outer-ux-1`，`play174` `d6dd9c23108e`）**：
  - 合盖 → 外屏时钟（`misr=c0160a1d`，内屏 TE `gpio86_edges=0`）→ 合成双击 → 外屏实时桌面激活（`cover_muted=1`，`blits=183`，`idle_skips=179`，滑动手势产生 `25` 个不同 MISR 签名，`posted=3`）→ 点击底部胶囊 → 回切外屏时钟并熄屏下线副核（`misr=57ab2396`，`end_display=0`，`CPU offline: exit 2, start 2, failed 0`）→ 展开手机 → 外屏清黑（`misr=ad495edf`）、外屏触控断电还原（`restored=1`）、内屏恢复扫描（`gpio86_edges=46`），`0` panic。

## 3. SENS-06：SLPI 磁力计、气压计与连续转轴角度传感器接入（`b14a860`）

- **改动**：
  - `ZizhanSlpiSensors.h`：新增纯整数三轴磁场模长（`milli-µT`）、气压 `hPa/Pa` 转换与合理范围门控、以及连续转轴角度姿态分级（`0` 闭合 `<30°`、`1` 悬停 `30°..150°`、`2` 展平 `>150°`）与主机单测。
  - `ZizhanSlpi.cpp` 与 `userland/touch.{h,c}`：接入 `mag`（`5 Hz`）、`pressure`（`2 Hz` 连续采样）与 `hinge_angle`（`on-change` 带连续流回退），支持单项开关与 `zizhan_sensors_all=1`，向 `/dev/zizhan-sensors` 投递类型 `19..21`（与已装机的 `sens-hal-v3` 100% 二进制兼容），并全部纳入熄屏自动停流与亮屏即时恢复状态机。
- **实机闭环（`sens-s6-1` `play175` `f582ce464e2d`、`pwr-bw-ab1` `play176` `4d9452d99125`）**：
  - 6 路传感器同时在线稳定输出：`accel`（`52 Hz`）、`ambient_light`（`13..17 lux`，`accuracy=3`）、`gyro`（`52 Hz`）、`mag`（`norm≈216..229 µT`，`accuracy=3`）、`pressure`（`99,792 Pa` / `997.92 hPa`，`accuracy=3`）、`hinge_angle`（桌面展平 `177.0°`，`posture=2`）；熄屏期间全部采样计数完全冻结，亮屏后立即恢复。

## 4. POWER-05 + M1-MIC：待机总线恢复修正与纯电池 A/B/C 实测 + 录音采集通道准备（`c7d3fd2`）

- **录音采集环形缓冲与 HAL 输入流（`M1-MIC`）**：
  - 新增 `ZizhanAudioCapture.h`（位于 4 KiB 控制页偏移 `128` 处，保持 112 字节 v2 播放控制头完全不变），提供无撕裂 seqlock 水位、`96 kHz` 双声道 → `48 kHz` 单声道 2:1 抽取、越界自动静音填零及 ADSP TX 图报文（`zizhan_cap_open/config/read`、`ar_open/config_mic_device`）主机 ASan/UBSan 测试（`cs35l41-capture.py`）。
  - `userland/ZizhanAudioPlugIn.c` 支持检测到采集头时自动映射输入环形缓冲并向 CoreAudio 暴露 `Microphone` 输入流与 `ReadInput` IO 路径（`zizhan-audio-plugin.py` 编译与协议检查通过）。
- **待机 RPMh BCM 投票修正与纯电池实测（`POWER-05`）**：
  - 实机 `ZIZHAN_RPMH_DUMP` 确认 `TCS0` 中 `SN2=0x213c44f0` 不带 `BCM_COMMIT`（bit 30），此前单槽位唤醒恢复漏置 bit 30 导致 VCD 未即时提交，且 `MC0/SH0` 唤醒硬编码 `kBcmMax`（`0x7fffffff`）会把 DDR 锁死在最高档。
  - `ZizhanStandbyRpmh.cpp` 修正为恢复 `b.boot | BCM_COMMIT`（`MM0=0x6247d09f`、`SN2=0x613c44f0`），并按 `cmd-db` 实测 `unit/width` 计算 `MC0=0x6003c03e`、`SH0=0x6000800b`（约 `3.2 GB/s` 常态档）。
  - **纯电池 260 s 熄屏三段式对照（`pwr-bw-ab2`，`play176`，`ZIZHAN_USB_OFF_AT=1`，Wi-Fi + 蓝牙 + ADSP + 6 路 SLPI 全部加载）**：开机 `trustd` 活动结束后，对比固件启动高 DDR 档位（`-417～-422 mA` / `1.80 W`），降低 `MM0+SN2+MC0+SH0` 稳态中位达 **`-394.0 mA`（`1,698.5 mW`）**，切换回常态 `MC0/SH0` + 降低 `MM0+SN2` 稳态达 **`-391.0 mA`（`1,685.0 mW`）**，亮屏唤醒 `132 frames/5s` 零卡顿、`0` panic。

## 5. 30 分钟全子系统静默浸泡 `soak-1125-day`（`play177` `eafb9e367066`）

- 干净提交 `c7d3fd2` 上同时开启：默认日常温控/电源策略 + `PRM 42` 4.34 s 快启双功放静音守护（`zizhan_pulse_dry=1`）+ 音量单格注入 + 3 轮模拟折叠与双击外屏桌面/胶囊回切（`zizhan_outer_fold=1 zizhan_outer_touch_synth=2`）+ 6 路 SLPI 传感器与自动亮度 + `zizhan_standby_bw=3` + 周期桌面触控（`1800 s`）：
  - **传感器与待机联动**：累计 `79,703` 次加速度、`79,702` 次陀螺仪、`884` 次环境光、`7,668` 次磁力计、`7,663` 次气压计（`99,712 Pa`）、转轴角度 `177.0°`，跨 3 轮合盖时钟/双击外屏桌面/胶囊锁屏/展开共完成 **`6/6` 次自动停流与恢复**（`quiesced=0(6/6)`）。
  - **网络、显存与温控**：Wi-Fi 连续 ping **`1,499 / 1,510 s`**（`>2 s` 断档 **0 次**）；768 MiB GPU BO 池峰值 `720 MiB`；CPU 最高 `82.4 °C`、GPU 最高 `65.1 °C`（限频窗口 `0/132`），电量保持 `100% -> 100%`。
  - **零异常**：`0` panic、`0` `SIGABRT`、`0` `SIGSEGV`、`0` `PastBuffer`、`0` `NONE FREE`，到期自动平稳回 Android。
  - 在场验收脚本 `run-video-1.sh` 与 `run-fold-desk-1.sh` 已同步重打包至最终干净内核 `build-play177`（带 `ZIZHAN_USER_PRESENT=yes` 门禁，无人值守期间未启动）。
