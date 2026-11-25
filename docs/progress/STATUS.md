# 当前进度与接续入口

更新：2026-11-26 清晨（UTC+8）。本页只保留**当前**状态，限一页。11-17 之前的完整台账原文在 [2026-11-17-status-archive.md](2026-11-17-status-archive.md)；规划依据见 [接手梳理与重新规划](../plan/2026-11-17-takeover-replan.md)。管理规则见 [AGENTS.md](../../AGENTS.md#progress-management)，本机操作细节在仓库外 `$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`。

## 推进规则（11-17 起）

- 一条主线 + 最多一条纯主机支线。主线按**用户能感知的功能**验收，“限定 PASS”只算过程证据。
- 里程碑最长 1 周，结束时做一次 ≤30 分钟的集中用户验收；需要人在场的步骤提前约时间。
- 同一问题连续 6 次设备轮或 1 天没有用户可见进展 → 停下复盘，换假设或挂起，并写在本页。
- 每天收尾在 Mac 上按主题本地提交；里程碑结束后推送。本页每次更新都要保持一页。

## 基线

| 项目 | 状态 |
| --- | --- |
| 源码 | `main`（`dac504b`）保持稳定基线；`wip/2026-11-17-audio-sensors` 已完成 **WCD9385 SoundWire DP1 + `VA/TX_MACRO` 物理主麦克风真声拾音**、CS35L41 `4.34 s` 突发快启与全双工 CoreAudio HAL、**QCA6490 `3 Mbps` 蓝牙串口（固件 `552 ms` 快载）**、`0045` 缩放动画快路径、单格音量键、合盖口袋防误触 + 双击 2:1 外屏桌面 + 四胶囊快捷栏 + 暗光压暗、7 路传感器与 M3 独立双启动包 `m3-standalone-1126`。 |
| 启动方式 | Mac 后部 USB-A `fastboot boot` 临时启动（带 `usb-attach.py` 双脉冲自愈、默认 300 s 自动返回、电源键 3 s 回 fastboot）；不复位直跳版 M3 包 `m3-standalone-1126` 已离线通过全项预检，待在场 RAM 测试与写 `boot_a`。 |
| 手机安装 | iPadOS 18.7.10 / 22H374（默认 `iPad7,12` 身份），试验版 **`sens-hal-v4`**（OS `43ab28…`、UI `c798a3…`，无需重刷即可直接使用物理麦克风与扬声器）；回退链 `sens-hal-v3` 与 10-28 全分区备份完整保留。 |
| 默认能用 | 内屏 120 Hz + GPU 零拷贝（6 槽位 + `0045` 快路径，`exit_zoom` `p90=25.0 ms`）、触控、拼音输入法、Wi-Fi（自动加入/跨频段/熄屏不断线）、`3 Mbps` 蓝牙扫描与传输层、手动亮度、电量/充电/固件 SOH（`80%`）、北京时间、日用温控、电源键熄屏/亮屏、单格音量键、合盖外屏时钟（防误触+暗光压暗）双击唤醒 2:1 桌面及底部 `VOL−/HOME/CLOCK/VOL+` 快捷栏。 |
| 需参数/待在场 | **双扬声器外放 + 物理主麦克风真声录音（`run-video-1.sh`，待在场听声与录音验收）**、合盖直进外屏桌面（`run-fold-desk-1.sh`）、四向旋转 + 自动亮度 + 7 路全传感器（`zizhan_slpi=4 zizhan_als_autobright=1 zizhan_sensors_all=1`）、待机总线降档（`zizhan_standby_bw=3`，纯电池 `-391～-394 mA`）。 |
| 不能用 | GPS/定位、蓝牙配对连接（`3 Mbps` 固件/HCI/扫描已通，上层配对未通）、相机出图、蜂窝数据、天气预报（卡 FairPlay）、系统深睡（约 `1.69 W`）。 |

## 当前里程碑

| ID | 里程碑 | 状态 | 验收 | 下一动作 |
| --- | --- | --- | --- | --- |
| M0 | 收口 | DONE（11-17） | 未提交工作按主题提交并推 wip 分支；CI 全绿；本页一页 | 已完成。 |
| M1 | 能出声 + 麦克风录音 | DOING（主线） | 用户在场：视频有声、音量键每次一格、麦克风能录真声 | 双功放 `4.34 s` 快启 + 单格音量 + **WCD9385 物理主麦 → CoreAudio 真声拾音**均已实机闭环（`mic-hw-4`）；下一步：在场跑 `run-video-1.sh` 听声与录音验收。 |
| M2 | 外屏 + 折叠 + 熄屏/亮屏 | DOING（第二优先） | 外屏能显示与触控、口袋不误触、开合自动切屏 | 距离防误触、`41 ms` 双击 2:1 外屏桌面、底部四胶囊（`VOL−/HOME/CLOCK/VOL+`）与夜间自动压暗已通过多轮 30 分钟浸泡；待在场手感验收。 |
| M3 | 离开电脑启动 | DOING（需在场） | 不接电脑开机：默认进安卓，菜单可选 iPadOS；回退保留 | `m3-standalone-1126`（`direct=1` + `build-play189`）已通过离线全项与原厂备份校验；仅在用户在场时先跑 RAM 测试再决定写 `boot_a`。 |

## 挂起与边界（用户点名才恢复，详见对应记录）

| ID | 当前状态与停点 | 记录 |
| --- | --- | --- |
| SENS-03..07 传感器组 | `zizhan_slpi=4` 已实机跑通 7 路传感器（加速度/陀螺/环境光/磁力/气压/转轴角度/距离），内屏熄屏自动停流恢复；待手持确认横屏方向后转默认 | [11-26 凌晨](2026-11-26-overnight-unattended.md)、[11-25 夜间](2026-11-25-evening-unattended.md) |
| POWER-01..05 + HEALTH | 纯电池熄屏稳态 `-391～-394 mA`（`1.69 W`）；ADSP 真实 `Health=1`、`SOH=80%` 已接入 `AppleSmartBattery` | [11-25 夜间](2026-11-25-evening-unattended.md)、[11-25 白天](2026-11-25-daytime-unattended.md) |
| BT-02 / MODEM-01 / CAM-01 | QCA6490 SE20 已升至 `3 Mbps`（`rampatch` `552 ms`、`0` 崩溃、与 Wi-Fi 无干扰共存），上层配对仍挂起；基带离线 QMI 就绪；四摄 ID 已通 | [11-26 凌晨](2026-11-26-overnight-unattended.md)、[11-23](2026-11-23-round5.md) |

## 待用户验收（集中 ≤15 分钟）

| 事项 | 时长 | 操作入口 |
| --- | --- | --- |
| 一键全能验收（双扬声器外放 + **WCD9385 主麦真声录音** + 单格音量 + 7 传感器 + 合盖防误触/双击桌面/四胶囊 + `0045` 顺滑退回） | ~8 分钟 | `ZIZHAN_USER_PRESENT=yes bash work/m1-firstsound/run-video-1.sh`，详见 [待人工清单](HUMAN_CHECKLIST.md) |
| 合盖直进外屏桌面对照 / M3 不复位直跳 RAM 预演（均零分区写入） | ~5 分钟 | `run-fold-desk-1.sh` / `work/m3-standalone-1126/run-m3-ram-test.sh` |

## 近期分日记录索引

- **2026-11-26 凌晨**：WCD9385 SoundWire DP1 物理主麦真声拾音、QCA6490 `3 Mbps` 蓝牙串口、XNU `0045` 缩放快路径、`usb-attach` 自愈与 `m3-standalone-1126` → [2026-11-26-overnight-unattended.md](2026-11-26-overnight-unattended.md)。
- **2026-11-25 白天与夜间**：外屏防误触 + 四胶囊 + 暗光压暗、`sens-hal-v4` 全双工采集环、电池 SOH `80%`、CS35L41 `4.34 s` 快启 → [夜间](2026-11-25-evening-unattended.md)、[白天](2026-11-25-daytime-unattended.md)、[凌晨](2026-11-25-unattended-20h.md)。
- **2026-11-24 白天与夜间**：`zcFlip` 合成栅栏快照（`0039`）、`minisandbox`（`0040`）、静音 CoreAudio HAL、SLPI 3.8 s 快启与四向旋转 → [白天](2026-11-24-zoom-cadence-and-sandbox.md)、[夜间](2026-11-24-overnight.md)。
