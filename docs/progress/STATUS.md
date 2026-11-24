# 当前进度与接续入口

更新：2026-11-24（UTC+8）。本页只保留**当前**状态，限一页。11-17 之前的完整台账原文在 [2026-11-17-status-archive.md](2026-11-17-status-archive.md)；规划依据见 [接手梳理与重新规划](../plan/2026-11-17-takeover-replan.md)。管理规则见 [AGENTS.md](../../AGENTS.md#progress-management)，本机操作细节在仓库外 `$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`。

## 推进规则（11-17 起）

- 一条主线 + 最多一条纯主机支线。主线按**用户能感知的功能**验收，“限定 PASS”只算过程证据。
- 里程碑最长 1 周，结束时做一次 ≤30 分钟的集中用户验收；需要人在场的步骤提前约时间。
- 同一问题连续 6 次设备轮或 1 天没有用户可见进展 → 停下复盘，换假设或挂起，并写在本页。
- 每天收尾在 Mac 上按主题本地提交；里程碑结束后推送。本页每次更新都要保持一页。

## 基线

| 项目 | 状态 |
| --- | --- |
| 源码 | `main`（`dac504b`）含日用默认、缩放栅栏快照、6 零拷贝槽位与沙盒修复；`wip/2026-11-17-audio-sensors` 新增静音 CoreAudio HAL 全链路、实体音量键防连发、SLPI 3.8 s 快启与四向旋转修复、熄屏触控静默及 GPU 导入/健康轮询优化。 |
| 启动方式 | Mac 后部 USB-A `fastboot boot` 临时启动（默认 300 s 自动返回、电源键 3 s 回 fastboot）；离开 Mac 启动待 M3 在场写 `boot_a`。 |
| 手机安装 | iPadOS 18.7.10 / 22H374（默认 `iPad7,12` 身份），试验版 `sens-hal-v3`（OS `6ec87b…`、UI `78d94d…`）；回退链与 10-28 全分区备份保留。 |
| 默认能用 | 内屏 120 Hz + GPU 零拷贝（6 槽位）、触控、拼音输入法、Wi-Fi（选网/自动加入/跨频段/熄屏不断线）、手动亮度、电量与充电显示、北京时间、日用温控（CPU 90 / GPU 85 °C）、电源键熄屏/亮屏（CPU 2–7 下线 + 触控扫描静默）、实体音量键单步调节。 |
| 需参数/待在场 | 声音（`ZizhanAudio.driver` → ADSP 静音全链路已通，待在场开功放试听 `loud-1`）、四向自动旋转（`zizhan_slpi=4` 0°/90°/180°/270° 零拷贝渲染已修好，待手持复核轴向）、折叠切外屏时钟（模拟合盖已通，待真机折叠 `fold-3`）。 |
| 不能用 | 外屏 iPadOS 桌面与触控接入、GPS/定位、蓝牙射频配对、相机出图、蜂窝数据（基带离线已就绪）、天气预报（卡 FairPlay）、系统深睡（熄屏纯电池约 1.8 W）。 |

## 当前里程碑

| ID | 里程碑 | 状态 | 验收 | 下一动作 |
| --- | --- | --- | --- | --- |
| M0 | 收口 | DONE（11-17） | 未提交工作按主题提交并推 wip 分支；CI 四项全绿；本页一页 | 已完成。 |
| M1 | 能出声 | DOING（主线） | 用户在场：系统提示音能听到、一个 App 播放能听到、音量键能调 | 数据到功放已证实（GPIO124/122），`zizhan_amp_lvl=1` 已备；11-24 晚打通 `coreaudiod` → `ZizhanAudio.driver` → `ZizhanAudio` → ADSP `PRM 42` 静音双会话全链路（`hal-s1-4`）并修好音量键连发抖动（`IO-01`）。下一步（在场约 5 分钟）：试听 `loud-1` 并接入功放开声。 |
| M2 | 外屏 + 折叠 + 熄屏/亮屏 | DOING（第二优先） | 外屏能显示与触控、开合自动切屏；熄屏/亮屏循环可触控 | S3/S4 熄屏亮屏与合盖跟随已默认开启（外屏 MISR 时钟 `fold-2` 闭环，外屏 FTM5 `0x87` 协议已通）；11-24 晚补齐熄屏内屏 FST2 停扫（SPI 轮询 `1500 → 0`）与按键降频（`184d8bb`）。下一步：在场跑 `fold-3` 真机折叠，并把外屏触点接入 HID。 |
| M3 | 离开电脑启动 | DOING（需在场） | 不接电脑开机：默认进安卓，菜单可选 iPadOS；回退路径保留 | A′ 双份标记失败已定位（11-21/23）；下一版改为不复位的直接跳转，仅在用户在场手旁时写 `boot_a`（`boot_b` 保持原厂）。 |

## 挂起与边界（用户点名才恢复，详见对应记录）

| ID | 当前状态与停点 | 记录 |
| --- | --- | --- |
| SENS-03 自动旋转 | `zizhan_slpi=4` 下 SLPI 启动缩至 ~3.8 s，修好 `22H374` `QuartzCore` 90°/270° 旋转表面越界（`PastBuffer 150 → 0`，四向截图全通）；待手持确认左右倾角符号后可转默认 | [11-24 夜间](2026-11-24-overnight.md) |
| UX 动画与首帧 | 缩放栅栏快照 + 6 零拷贝槽位 + `minisandbox` 修复已合入 `main`（`open_zoom` 58.6 fps、`exit_zoom` 54.3 fps）；11-24 晚跨核采样 `backboardd` 并消除 `client_healthy` MMIO 空转（`smmu_read -79%`），剩余主要为 `QuartzCore` 多层合成与背景高斯模糊 | [11-24 白天](2026-11-24-zoom-cadence-and-sandbox.md)、[夜间](2026-11-24-overnight.md) |
| POWER-01..05 | `iPad7,12` 下电池电量/图表与锁屏小组件正常（原转圈系旧 `iPhone17,1` 身份）；纯电池熄屏稳态 `-415～-424 mA`（~1.8 W），进一步降功耗需平台深睡 | [11-20](2026-11-20-round3.md)、[11-24 夜间](2026-11-24-overnight.md) |
| APP-01 天气 / BT-01 / MODEM-01 / CAM-01 | 天气卡 FairPlay；蓝牙卡 `BlueTool` `WifiVendor` 固件表；基带离线 QMI 45 服务就绪（阶段 2 需插卡授权）；四摄 CCI 芯片 ID 已通 | [11-19 晚](2026-11-19-evening-daily-default.md)、[11-20](2026-11-20-round3.md) |

## 待用户验收（集中 ≤20 分钟）

| 事项 | 时长 | 操作入口 |
| --- | --- | --- |
| M1：试听 `loud-1` 及实体音量键手感 | ~5 分钟 | `ZIZHAN_USER_PRESENT=yes bash work/m1-firstsound/run-loud-1.sh`，详见 [待人工清单](HUMAN_CHECKLIST.md) |
| SENS-03：手持四向自动旋转轴向复核 | ~3 分钟 | 启动参数加 `zizhan_slpi=4`（开机约 11 s 传感器就绪），验证竖屏/倒置/左横/右横与平放 |
| M2 + UX：真实折叠 `fold-3` 与缩放动画主观手感 | ~5 分钟 | `bash work/m1-firstsound/run-fold-3.sh`；主屏连续点开/上滑退出备忘录、设置、时钟 |

## 近期分日记录索引

- **2026-11-24 夜间（无人值守）**：静音 CoreAudio HAL 全链路（`PRM 42` + `0041` `bindfs`）与音量键防连发；SLPI 3.8 s 快启与 `22H374` 90°/270° Metal 旋转修复；熄屏触控停扫纯电池 A/B；`backboardd` 缩放跨核采样与 Adreno 优化；25 min 全子系统综合浸泡 `soak-1124` 通过（27932 帧、Wi-Fi 1352/1361 s 零断流、SLPI 7.4 万样本、0 panic / 0 崩溃） → [2026-11-24-overnight.md](2026-11-24-overnight.md)。
- **2026-11-24 下午**：`zcFlip` 合成栅栏快照（`0039`）、`minisandbox` 请求 5 修复 `MobileNotes` 崩溃（`0040`）、默认 6 零拷贝槽位 → [2026-11-24-zoom-cadence-and-sandbox.md](2026-11-24-zoom-cadence-and-sandbox.md)。
- **2026-11-23 第五/六轮**：CPU 本机频点、SLPI 离线与 ADSP 共存（`zizhan_slpi=4`）、SpringBoard 跨核采样 → [round5](2026-11-23-round5.md)、[round6](2026-11-23-round6.md)。
- **2026-11-19～21 日用基线**：S3/S4 熄屏亮屏、温控、折叠/外屏、基带离线、GPU 768 MiB 池、拼音键盘、`iPad7,12` 默认身份 → [11-19](2026-11-19-evening-daily-default.md)、[round3](2026-11-20-round3.md)、[round4](2026-11-21-round4.md)。
