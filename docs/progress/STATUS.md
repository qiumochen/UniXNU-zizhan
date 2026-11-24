# 当前进度与接续入口

更新：2026-11-25（UTC+8）。本页只保留**当前**状态，限一页。11-17 之前的完整台账原文在 [2026-11-17-status-archive.md](2026-11-17-status-archive.md)；规划依据见 [接手梳理与重新规划](../plan/2026-11-17-takeover-replan.md)。管理规则见 [AGENTS.md](../../AGENTS.md#progress-management)，本机操作细节在仓库外 `$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`。

## 推进规则（11-17 起）

- 一条主线 + 最多一条纯主机支线。主线按**用户能感知的功能**验收，“限定 PASS”只算过程证据。
- 里程碑最长 1 周，结束时做一次 ≤30 分钟的集中用户验收；需要人在场的步骤提前约时间。
- 同一问题连续 6 次设备轮或 1 天没有用户可见进展 → 停下复盘，换假设或挂起，并写在本页。
- 每天收尾在 Mac 上按主题本地提交；里程碑结束后推送。本页每次更新都要保持一页。

## 基线

| 项目 | 状态 |
| --- | --- |
| 源码 | `main`（`dac504b`）保持稳定基线；`wip/2026-11-17-audio-sensors` 新增多会话持久 CS35L41 功放守护模式（`PRM 42`）、实体音量键严格单格步进（`3.00 dB`/格）、合盖按需上电外屏 `FTM5` 触控与 2:1 实时外屏桌面（`zizhan_outer_fold=2`）、SLPI 全量 8 传感器发现与环境光自动亮度/熄屏静默。 |
| 启动方式 | Mac 后部 USB-A `fastboot boot` 临时启动（默认 300 s 自动返回、电源键 3 s 回 fastboot）；离开 Mac 启动待 M3 在场写 `boot_a`。 |
| 手机安装 | iPadOS 18.7.10 / 22H374（默认 `iPad7,12` 身份），试验版 `sens-hal-v3`（OS `6ec87b…`、UI `78d94d…`）；回退链与 10-28 全分区备份保留。 |
| 默认能用 | 内屏 120 Hz + GPU 零拷贝（6 槽位）、触控、拼音输入法、Wi-Fi（自动加入/跨频段/熄屏不断线）、手动亮度、电量与充电显示、北京时间、日用温控（CPU 90 / GPU 85 °C）、电源键熄屏/亮屏（CPU 2–7 下线 + 触控停扫）、实体音量键单格调节（6.25%/格）、合盖外屏日期电量时钟与 `FTM5` 触控生命周期。 |
| 需参数/待在场 | 双扬声器外放（`PRM 42` 多会话持久功放模式已修好 `loud-2` 启动探测抢占并完成静音干跑 `hal-amp-dry-1`，待在场跑 `run-video-1.sh` 播放视频）、外屏实时 iPadOS 桌面与触控（`zizhan_outer_fold=2` 已闭环验证 `outer-s2-2`，待在场跑 `run-fold-desk-1.sh`）、四向自动旋转 + 环境光自动亮度（`zizhan_slpi=4 zizhan_als_autobright=1`）。 |
| 不能用 | GPS/定位、蓝牙射频配对、相机出图、蜂窝数据（基带离线已就绪）、天气预报（卡 FairPlay）、系统深睡（熄屏纯电池约 1.8–1.9 W）。 |

## 当前里程碑

| ID | 里程碑 | 状态 | 验收 | 下一动作 |
| --- | --- | --- | --- | --- |
| M0 | 收口 | DONE（11-17） | 未提交工作按主题提交并推 wip 分支；CI 四项全绿；本页一页 | 已完成。 |
| M1 | 能出声 | DOING（主线） | 用户在场：Safari/系统视频能听声、音量键每次一格平滑调节 | 已定位并修好 `loud-2` 无声根因（`audiomxd` 开机空探测占掉单次 `PRM 41`）与步进偏大问题：持久 `PRM 42` 功放守护跳过空探测、多段音频连续开关功放（`hal-amp-dry-1`），`0044` 音量键单次短按严格 `3.00 dB`（1/16）。下一步：在场跑 `run-video-1.sh` 直接播视频验收。 |
| M2 | 外屏 + 折叠 + 熄屏/亮屏 | DOING（第二优先） | 外屏能显示与触控、开合自动切屏；熄屏/亮屏循环可触控 | 合盖按需上电 `WL2866D` + `FTM5`（`0x3640`）接入 HID、开盖断电恢复，默认外屏显示日期+电量+时钟（`outer-s2-1`），`zizhan_outer_fold=2` 仅灭内屏并在外屏实时渲染 2:1 iPadOS 桌面与边缘手势（`outer-s2-2`、`fold-stress-1` 连续 3 轮折叠全通）。下一步：在场跑 `run-fold-desk-1.sh` 真机折叠体验。 |
| M3 | 离开电脑启动 | DOING（需在场） | 不接电脑开机：默认进安卓，菜单可选 iPadOS；回退路径保留 | A′ 双份标记失败已定位（11-21/23）；下一版改为不复位的直接跳转，仅在用户在场手旁时写 `boot_a`（`boot_b` 保持原厂）。 |

## 挂起与边界（用户点名才恢复，详见对应记录）

| ID | 当前状态与停点 | 记录 |
| --- | --- | --- |
| SENS-03..05 传感器组 | `zizhan_slpi=4` 已枚举全部 8 类传感器（含环境光/陀螺仪/磁力计/气压/距离/`hinge_angle`），接通环境光自动调光（暗室 `1024 → 56` DBV）与熄屏自动停流省电（纯电池熄屏中位 `-434 mA`）；待手持确认横屏方向后可一并转默认 | [11-24 夜间](2026-11-24-overnight.md)、[11-25 无人值守](2026-11-25-unattended-20h.md) |
| UX 动画与首帧 | 缩放栅栏快照 + 6 零拷贝槽位 + `minisandbox` 修复已合入 `main`（`open_zoom` 58.6 fps、`exit_zoom` 54.3 fps）；`PongAdreno` 跨段步进与 `client_healthy` 限频已合入 `wip` | [11-24 白天](2026-11-24-zoom-cadence-and-sandbox.md)、[夜间](2026-11-24-overnight.md) |
| POWER-01..05 | `iPad7,12` 电池页正常；全开 `SLPI`（熄屏静默）+ `PRM 42` + 外屏待命的纯电池熄屏稳态 `-428～-435 mA`（`1.89 W`），进一步降功耗需平台深睡 | [11-24 夜间](2026-11-24-overnight.md)、[11-25 无人值守](2026-11-25-unattended-20h.md) |
| APP-01 天气 / BT-01 / MODEM-01 / CAM-01 | 天气卡 FairPlay；蓝牙卡 `BlueTool` `WifiVendor` 固件表；基带离线 QMI 45 服务就绪（阶段 2 需插卡授权）；四摄 CCI 芯片 ID 已通 | [11-19 晚](2026-11-19-evening-daily-default.md)、[11-20](2026-11-20-round3.md) |

## 待用户验收（集中 ≤15 分钟）

| 事项 | 时长 | 操作入口 |
| --- | --- | --- |
| 综合体验（外放视频 + 单格音量键 + 四向旋转 + 自动亮度 + 合盖外屏时钟） | ~8 分钟 | `ZIZHAN_USER_PRESENT=yes bash work/m1-firstsound/run-video-1.sh`，详见 [待人工清单](HUMAN_CHECKLIST.md) |
| 折叠外屏实时 iPadOS 桌面与外屏触控 | ~5 分钟 | `ZIZHAN_USER_PRESENT=yes bash work/m1-firstsound/run-fold-desk-1.sh`，合上手机直接在外屏点按/上滑主屏 |

## 近期分日记录索引

- **2026-11-25（20h 无人值守）**：修好 `loud-2` 无声与音量步进（多会话 `PRM 42` CS35L41 守护 + `0044` 单格 3 dB 步进）；合盖 `FTM5` 外屏触控与 `zizhan_outer_fold=2` 实时 2:1 外屏桌面；SLPI 8 传感器发现、环境光自动亮度与熄屏停流省电（纯电池 `-434 mA`）；30 min 全子系统浸泡 `soak-1125` → [2026-11-25-unattended-20h.md](2026-11-25-unattended-20h.md)。
- **2026-11-24 夜间**：静音 CoreAudio HAL 全链路、SLPI 3.8 s 快启与 90°/270° Metal 旋转修复、熄屏触控停扫、`soak-1124` → [2026-11-24-overnight.md](2026-11-24-overnight.md)。
- **2026-11-24 下午**：`zcFlip` 合成栅栏快照（`0039`）、`minisandbox` 修复 `MobileNotes`（`0040`）、6 零拷贝槽位 → [2026-11-24-zoom-cadence-and-sandbox.md](2026-11-24-zoom-cadence-and-sandbox.md)。
- **2026-11-19～23 日用与传感器基线**：S3/S4 熄屏亮屏、温控、GPU 768 MiB 池、`iPad7,12` 默认身份、SLPI/ADSP 共存 → [round3](2026-11-20-round3.md)、[round4](2026-11-21-round4.md)、[round5](2026-11-23-round5.md)、[round6](2026-11-23-round6.md)。
