# 当前进度与接续入口

更新：2026-11-25 深夜（UTC+8）。本页只保留**当前**状态，限一页。11-17 之前的完整台账原文在 [2026-11-17-status-archive.md](2026-11-17-status-archive.md)；规划依据见 [接手梳理与重新规划](../plan/2026-11-17-takeover-replan.md)。管理规则见 [AGENTS.md](../../AGENTS.md#progress-management)，本机操作细节在仓库外 `$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`。

## 推进规则（11-17 起）

- 一条主线 + 最多一条纯主机支线。主线按**用户能感知的功能**验收，“限定 PASS”只算过程证据。
- 里程碑最长 1 周，结束时做一次 ≤30 分钟的集中用户验收；需要人在场的步骤提前约时间。
- 同一问题连续 6 次设备轮或 1 天没有用户可见进展 → 停下复盘，换假设或挂起，并写在本页。
- 每天收尾在 Mac 上按主题本地提交；里程碑结束后推送。本页每次更新都要保持一页。

## 基线

| 项目 | 状态 |
| --- | --- |
| 源码 | `main`（`dac504b`）保持稳定基线；`wip/2026-11-17-audio-sensors` 已完成 CS35L41 `4.34 s` 突发快启（`PRM 42` 多会话守护）+ TX 录音采集环与 CoreAudio 全双工 HAL、单格音量键（`3.00 dB`/格）、合盖口袋防误触 + 双击 2:1 外屏桌面 + 底部四胶囊快捷栏 + 暗光自动压暗、SLPI 7 路传感器 + 熄屏停流、电池固件 SOH（`80%`）读出与 M3 独立双启动离线预检。 |
| 启动方式 | Mac 后部 USB-A `fastboot boot` 临时启动（默认 300 s 自动返回、电源键 3 s 回 fastboot）；不复位直跳版 M3 包 `m3-standalone-1125` 已离线通过预检，待在场 RAM 测试与写 `boot_a`。 |
| 手机安装 | iPadOS 18.7.10 / 22H374（默认 `iPad7,12` 身份），试验版 **`sens-hal-v4`**（OS `43ab28…`、UI `c798a3…`）；回退链 `sens-hal-v3` 与 10-28 全分区备份完整保留。 |
| 默认能用 | 内屏 120 Hz + GPU 零拷贝（6 槽位）、触控、拼音输入法、Wi-Fi（自动加入/跨频段/熄屏不断线）、手动亮度、电量/充电/固件 SOH（`80%`）显示、北京时间、日用温控（CPU 90 / GPU 85 °C）、电源键熄屏/亮屏（CPU 2–7 下线 + 触控停扫）、单格音量键、合盖外屏时钟（带距离感应防误触与暗光压暗）双击唤醒 2:1 实时桌面及底部 `VOL−/HOME/CLOCK/VOL+` 快捷栏。 |
| 需参数/待在场 | 双扬声器外放 + 全双工 CoreAudio HAL（待在场跑 `run-video-1.sh` 听声验收）、合盖直进外屏桌面（`run-fold-desk-1.sh`）、四向旋转 + 自动亮度 + 7 路全传感器（`zizhan_slpi=4 zizhan_als_autobright=1 zizhan_sensors_all=1`）、待机总线降档（`zizhan_standby_bw=3`，纯电池 `-391～-394 mA`）。 |
| 不能用 | 模拟麦克风声学拾音（CoreAudio ↔ HAL ↔ 内核采集环 ↔ ADSP `RD_SHMEM` 已实机 48 kHz 跑通，缺 WCD9385 SoundWire + `TX_MACRO` 驱动）、GPS/定位、蓝牙配对（扫描已通）、相机出图、蜂窝数据、天气预报（卡 FairPlay）、系统深睡（约 `1.69 W`）。 |

## 当前里程碑

| ID | 里程碑 | 状态 | 验收 | 下一动作 |
| --- | --- | --- | --- | --- |
| M0 | 收口 | DONE（11-17） | 未提交工作按主题提交并推 wip 分支；CI 全绿；本页一页 | 已完成。 |
| M1 | 能出声 + 音频通道 | DOING（主线） | 用户在场：视频能听声、音量键每次一格；录音软件通道就绪 | 双功放 `4.34 s` 快启 + 单格音量 + `sens-hal-v4` CoreAudio 输入/输出流实机全通（`mic-hal-1`）；下一步：在场跑 `run-video-1.sh` 听声验收。 |
| M2 | 外屏 + 折叠 + 熄屏/亮屏 | DOING（第二优先） | 外屏能显示与触控、口袋不误触、开合自动切屏 | 已实机打通距离传感器口袋防误触、`41 ms` 双击切 2:1 外屏桌面、底部四胶囊（`VOL−/HOME/CLOCK/VOL+`）与夜间自动压暗（`outer-pocket-bar-1`、`soak-1125-night-a`）；待在场真机手感验收。 |
| M3 | 离开电脑启动 | DOING（需在场） | 不接电脑开机：默认进安卓，菜单可选 iPadOS；回退保留 | `m3-standalone-1125`（`direct=1` 不复位直跳 + `build-play182`）已通过离线全项校验并生成门禁脚本；仅在用户在场时先跑 RAM 测试再决定写 `boot_a`。 |

## 挂起与边界（用户点名才恢复，详见对应记录）

| ID | 当前状态与停点 | 记录 |
| --- | --- | --- |
| SENS-03..07 传感器组 | `zizhan_slpi=4` 已实机跑通 7 路传感器（加速度/陀螺/环境光/磁力/气压/转轴角度/距离），合盖保留距离防误触、内屏熄屏 6/6 次自动停流恢复；待手持确认横屏方向后转默认 | [11-25 夜间](2026-11-25-evening-unattended.md)、[11-25 白天](2026-11-25-daytime-unattended.md) |
| POWER-01..05 + HEALTH | 纯电池熄屏稳态降至 `-391～-394 mA`（`1.69 W`）；ADSP 真实 `Health=1`、`SOH=80%` 已接入 `AppleSmartBattery`（不伪造苹果原装认证芯片状态） | [11-25 夜间](2026-11-25-evening-unattended.md)、[11-25 白天](2026-11-25-daytime-unattended.md) |
| M1-MIC / BT-01 / MODEM-01 / CAM-01 | `AudioQueueNewInput` 48 kHz 采集链路实机零丢帧跑通，真实声波拾音待 WCD9385 SoundWire + `TX_MACRO`；蓝牙扫描已通；基带离线 QMI 就绪；四摄 ID 已通 | [11-25 夜间](2026-11-25-evening-unattended.md)、[11-23](2026-11-23-round5.md) |

## 待用户验收（集中 ≤15 分钟）

| 事项 | 时长 | 操作入口 |
| --- | --- | --- |
| 一键全能体验（4.3 s 快启视频外放 + 单格音量 + 7 传感器 + 合盖防误触/双击桌面/四胶囊快捷栏） | ~8 分钟 | `ZIZHAN_USER_PRESENT=yes bash work/m1-firstsound/run-video-1.sh`，详见 [待人工清单](HUMAN_CHECKLIST.md) |
| 合盖直进外屏桌面对照 / M3 不复位直跳 RAM 预演（均零分区写入） | ~5 分钟 | `run-fold-desk-1.sh` / `work/m3-standalone-1125/run-m3-ram-test.sh` |

## 近期分日记录索引

- **2026-11-25 夜间**：外屏距离防误触 + 四胶囊快捷栏 + 暗光压暗、`sens-hal-v4` CoreAudio 全双工采集环、电池 SOH `80%`、`m3-standalone-1125` 预检、`soak-1125-night-a` → [2026-11-25-evening-unattended.md](2026-11-25-evening-unattended.md)。
- **2026-11-25 白天与凌晨**：CS35L41 `4.34 s` 突发快启、外屏时钟双击切 2:1 桌面、磁力/气压/转轴传感器、纯电池 `-391 mA`、多会话 `PRM 42` 与 `0044` 单格音量 → [白天](2026-11-25-daytime-unattended.md)、[凌晨](2026-11-25-unattended-20h.md)。
- **2026-11-24 白天与夜间**：`zcFlip` 合成栅栏快照（`0039`）、`minisandbox`（`0040`）、静音 CoreAudio HAL、SLPI 3.8 s 快启与四向旋转 → [白天](2026-11-24-zoom-cadence-and-sandbox.md)、[夜间](2026-11-24-overnight.md)。
