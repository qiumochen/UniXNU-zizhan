# 当前进度与接续入口

更新：2026-11-26 晚间（UTC+8）。本页只保留**当前**状态，限一页。11-17 之前的完整台账原文在 [2026-11-17-status-archive.md](2026-11-17-status-archive.md)；规划依据见 [接手梳理与重新规划](../plan/2026-11-17-takeover-replan.md)。管理规则见 [AGENTS.md](../../AGENTS.md#progress-management)，本机操作细节在仓库外 `$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`。

## 推进规则（11-17 起）

- 一条主线 + 最多一条纯主机支线。主线按**用户能感知的功能**验收，“限定 PASS”只算过程证据。
- 里程碑最长 1 周，结束时做一次 ≤30 分钟的集中用户验收；需要人在场的步骤提前约时间。
- 同一问题连续 6 次设备轮或 1 天没有用户可见进展 → 停下复盘，换假设或挂起，并写在本页。
- 每天收尾在 Mac 上按主题本地提交；里程碑结束后推送。本页每次更新都要保持一页。

## 基线

| 项目 | 状态 |
| --- | --- |
| 源码 | `main`（`dac504b`）保持稳定基线；`wip/2026-11-17-audio-sensors` 已完成 **Wi-Fi 与常驻音频 SMMU 并发修复（9/9 冷启动全通）**、**扬声器 `-26 dB` 软拐点响度提升（80% 档 `+9.3 dB`）+ WCD9385 主麦 `+20 dB` 增益校准**、**展开防闪白 + 外屏单指上滑进桌面/下巴上滑解锁**、**双开关精简电池页 + 冷启动容器防横闪**、**MPSS `QMI_LOC` 1 Hz GPS → `CoreLocation` 全链路**、**超广角 `OV13B10` 4-lane MIPI CSI-2 实帧 `1364×768` RAW10 出图**，及 `m3-standalone-1127`。 |
| 启动方式 | Mac 后部 USB-A `fastboot boot` 临时启动（带 `usb-attach.py` 双脉冲自愈、默认自动返回、电源键 3 s 回 fastboot）；不复位直跳版 M3 包 `m3-standalone-1127`（`7d4baddaf301…`）已通过离线全项与原厂备份校验，待在场 RAM 测试与写 `boot_a`。 |
| 手机安装 | iPadOS 18.7.10 / 22H374（默认 `iPad7,12`），已安装并整盘回读校验 **`sens-hal-v10`**（UI `1a2bcfc0…`）；完整保留 `sens-hal-v7`、`sens-hal-v4`、`sens-hal-v3` 与 10-28 全分区备份。 |
| 默认能用 | 内屏 120 Hz + GPU 零拷贝、触控、拼音输入法、Wi-Fi（与音频常驻并发）、`3 Mbps` 蓝牙扫描与密钥目录、手动亮度与展开平滑恢复、「设置 → 电池」干净双开关（电池百分比/低电量模式）、北京时间、日用温控、电源键熄屏/亮屏、单格音量键、锁向冷启动容器与首帧全竖屏、物理合盖切屏 + 外屏单指上滑桌面/下巴上滑 Home。 |
| 需参数/待在场 | **全功能集中验收 `run-video-3.sh`**（Wi-Fi 视频 + 增响扬声器 + 提增益麦克风 + 双开关电池 + 外屏上滑 + 展开无闪白 + 窗边/户外 1 Hz GPS）、**实景拍照导出 PNG `run-cam-live.sh`**、蓝牙配对实测、M3 独立双启动 RAM 预演。 |
| 不能用 | 系统相机 App 实时取景（内核 `OV13B10` MIPI CSI-2 实帧采集已通，AVCapture HAL 未接）、蜂窝数据（MPSS 锁定离线零发射仅跑 GPS）、天气预报（卡 FairPlay）、系统深睡（约 `1.69 W`）。 |

## 当前里程碑

| ID | 里程碑 | 状态 | 验收 | 下一动作 |
| --- | --- | --- | --- | --- |
| M0 | 收口 | DONE（11-17） | 未提交工作按主题提交并推 wip 分支；CI 全绿；本页一页 | 已完成。 |
| M1 | 能出声 + 麦克风录音 | DOING（待在场终验） | 音调清晰响亮、Wi-Fi 并发正常、主麦录音回放响度适中 | 已修 Wi-Fi/音频 SMMU 锁冲突并提升扬声器/主麦增益；下一步：在场跑 `run-video-3.sh` 播在线视频与语音备忘录。 |
| M2 | 外屏 + 折叠 + 方向与 UI | DOING（待在场终验） | 合盖/展开亮度平稳、外屏单指上滑可用、冷启动无横闪、双开关电池页干净 | 展开 `1800 ms` 防 `1024` 闪白、外屏上滑解锁、容器方向补齐与双开关电池页均已实机通过；待在场手持确认。 |
| M3 | 离开电脑启动 | DOING（需在场） | 不接电脑开机：默认进安卓，菜单可选 iPadOS；回退保留 | `m3-standalone-1127`（`direct=1` + `build-play201`）已通过离线全项校验；仅在用户在场时先跑 RAM 测试再决定写 `boot_a`。 |

## 挂起与边界（用户点名才恢复，详见对应记录）

| ID | 当前状态与停点 | 记录 |
| --- | --- | --- |
| GNSS-01 / SENS | 3-DSP（ADSP+SLPI+MPSS）并发与 `QMI_LOC` 1 Hz 定位流 → `CLLocationManager` 已实机打通（零蜂窝发射）；室内无卫星信号，待窗边/户外验真实经纬度收敛 | [11-26 晚间](2026-11-26-evening-unattended.md) |
| CAM-01 相机管线 | 后置超广角 `OV13B10`（`0x560d42`）4-lane `1120 Mbps` `CSIPHY1 → CSID-lite0 → VFE-lite0` `1364×768` RAW10 零误码落盘并导出实帧/彩条 PNG；iOS 相机 App 框架层未接 | [11-26 晚间](2026-11-26-evening-unattended.md) |
| POWER-BATT / BT-02 | 电池页按用户要求精简为双开关（健康/图表已禁用）；`3 Mbps` 蓝牙与配对密钥目录已就绪，待在场手动试配对 | [11-26 晚间](2026-11-26-evening-unattended.md)、[11-26 白天](2026-11-26-daytime-unattended.md) |

## 待用户验收（集中 ≤10 分钟）

| 事项 | 时长 | 操作入口 |
| --- | --- | --- |
| 主验收（Wi-Fi 在线视频 + 增响外放/主麦录音 + 锁向冷启动无横闪 + 双开关电池页 + 外屏上滑解锁 + 展开无闪白 + 窗边 GPS） | ~8 分钟 | `ZIZHAN_USER_PRESENT=yes bash work/m1-firstsound/run-video-3.sh`，详见 [待人工清单](HUMAN_CHECKLIST.md) |
| 后置超广角 `OV13B10` 实景拍照自动导出 PNG / M3 双启动 RAM 预演（均零分区写入） | ~5 分钟 | `run-cam-live.sh` / `work/m3-standalone-1127/run-m3-ram-test.sh` |

## 近期分日记录索引

- **2026-11-26 晚间、白天与凌晨**：Wi-Fi/音频 SMMU 并发、展开防闪白 + 外屏上滑、麦/扬增益重调、双开关电池页 + 容器正屏、`QMI_LOC` 1 Hz GPS 与 `OV13B10` 实帧出图（[晚间](2026-11-26-evening-unattended.md)）；`PCM_DEC` 音调修复、物理转轴切屏、IMU `90°` 对齐（[白天](2026-11-26-daytime-unattended.md)）；WCD9385 主麦与 `3 Mbps` 蓝牙（[凌晨](2026-11-26-overnight-unattended.md)）。
- **2026-11-25 白天与夜间**：外屏防误触 + 四胶囊 + 暗光压暗、`sens-hal-v4` 全双工采集环、CS35L41 `4.34 s` 快启 → [夜间](2026-11-25-evening-unattended.md)、[白天](2026-11-25-daytime-unattended.md)、[凌晨](2026-11-25-unattended-20h.md)。
- **2026-11-24 白天与夜间**：`zcFlip` 合成栅栏快照（`0039`）、`minisandbox`（`0040`）、静音 CoreAudio HAL、SLPI 3.8 s 快启 → [白天](2026-11-24-zoom-cadence-and-sandbox.md)、[夜间](2026-11-24-overnight.md)。
