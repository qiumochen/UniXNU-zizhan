# 当前进度与接续入口

更新：2026-11-27 凌晨（UTC+8）。本页只保留**当前**状态，限一页。11-17 之前的完整台账原文在 [2026-11-17-status-archive.md](2026-11-17-status-archive.md)；规划依据见 [接手梳理与重新规划](../plan/2026-11-17-takeover-replan.md)。管理规则见 [AGENTS.md](../../AGENTS.md#progress-management)，本机操作细节在仓库外 `$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`。

## 推进规则（11-17 起）

- 一条主线 + 最多一条纯主机支线。主线按**用户能感知的功能**验收，“限定 PASS”只算过程证据。
- 里程碑最长 1 周，结束时做一次 ≤30 分钟的集中用户验收；需要人在场的步骤提前约时间。
- 同一问题连续 6 次设备轮或 1 天没有用户可见进展 → 停下复盘，换假设或挂起，并写在本页。
- 每天收尾在 Mac 上按主题本地提交；里程碑结束后推送。本页每次更新都要保持一页。

## 基线

| 项目 | 状态 |
| --- | --- |
| 源码 | `main`（`dac504b`）保持稳定基线；`wip/2026-11-17-audio-sensors` 已攻克三大 P0：**平台级熄屏深度休眠（`CPU 1..7` `PSCI CPU_OFF` + 主核掩码 `WFI`，纯电池 `-323 mA / 1.40 W`，较同轮传统熄屏降 `43.4%`，`19+` 轮唤醒 `0` panic）**、**机内原生 `IPA 安装器`（支持 4K/16K 脱壳 `.ipa` 网页直传免签永久安装，拦截 `cryptid=1` 加密包）**、**系统级 `AVCaptureSession` 前后置双摄实时流（后置 `OV13B10` + 前置 `IMX596` 预览 / 扫码 / JPEG 拍照 / 视频帧回调，空闲 `0 mW`）**，叠加 Wi-Fi/音频并发、增响外放/主麦、外屏上滑与 1 Hz GPS。 |
| 启动方式 | Mac 后部 USB-A `fastboot boot` 临时启动（默认自动返回、电源键 3 s 回 fastboot）；M3 双启动包 `m3-standalone-1127`（`44f555f190d1…`，`build-play204`）已通过离线全项与原厂备份校验，待在场 RAM 预演。 |
| 手机安装 | iPadOS 18.7.10 / 22H374（默认 `iPad7,12`），已安装并整盘回读校验 **`sens-hal-v22`**（UI `e8a257433d2f…`，内置 `IPA 安装器` 与相机/签名桥接）；完整保留 `sens-hal-v10/v7/v4/v3` 与 10-28 全分区备份。 |
| 默认能用 | 内屏 120 Hz + GPU 零拷贝、**熄屏平台深睡与电源键/霍尔开合唤醒**、**`IPA 安装器`（4K/16K 脱壳第三方应用安装运行）**、**第三方与系统相机 `AVCaptureSession`（前后摄切换/扫码/拍照）**、触控、拼音输入法、Wi-Fi、`3 Mbps` 蓝牙、双开关电池页、北京时间、温控、锁向冷启动无横闪、合盖切屏 + 外屏单指上滑解锁。 |
| 需参数/待在场 | **全功能集中验收 `run-video-4.sh`**（熄屏深睡唤醒 + 局域网上传安装脱壳 IPA + 真机扫码/拍照/前后摄切换 + Wi-Fi 在线视频外放 + 主麦录音 + 外屏上滑 + 户外 GPS）、M3 双启动 RAM 预演。 |
| 不能用 | 未脱壳 App Store 加密 IPA（`cryptid=1`，受限于无 Apple 硬件 FairPlay 密钥；安装器已明确拦截提示）、蜂窝数据（MPSS 锁定离线零发射仅跑 GPS）、天气预报（卡 FairPlay）。 |

## 当前里程碑

| ID | 里程碑 | 状态 | 验收 | 下一动作 |
| --- | --- | --- | --- | --- |
| M0 | 收口 | DONE（11-17） | 未提交工作按主题提交并推 wip 分支；CI 全绿；本页一页 | 已完成。 |
| M1 | 能出声 + 麦克风录音 | DOING（待在场终验） | 音调清晰响亮、Wi-Fi 并发正常、主麦录音回放响度适中 | 已修 Wi-Fi/音频 SMMU 锁冲突并提升扬声器/主麦增益；下一步：在场跑 `run-video-4.sh` 一次终验。 |
| M2 | 外屏 + 折叠 + 深睡与生态 | DOING（待在场终验） | 熄屏深睡秒醒、脱壳 IPA 免签直装、相机扫码拍照可用、外屏/电池页顺畅 | 三大 P0（深睡 `-323 mA`、16K IPA 侧载、前后置 `AVCaptureSession` 扫码拍照）均已实机闭环；待在场手持确认。 |
| M3 | 离开电脑启动 | DOING（需在场） | 不接电脑开机：默认进安卓，菜单可选 iPadOS；回退保留 | `m3-standalone-1127`（`44f555f190d1…`）已刷新至 `build-play204` 并通过校验；仅在用户在场时先跑 RAM 测试。 |

## 挂起与边界（用户点名才恢复，详见对应记录）

| ID | 当前状态与停点 | 记录 |
| --- | --- | --- |
| POWER-02 熄屏深睡 | `IOPMrootDomain` + `CPU 1..7` `PSCI CPU_OFF` + 主核掩码 `WFI` 已实机跑通 `19+` 轮 `0` panic，纯电池降至 `-323 mA / 1.40 W`；更深 AOSS 系统休眠受限于 QCA6490/UFS 固件重枚举，当前已达稳定日用甜点 | [11-27 攻坚](2026-11-27-unattended-sleep-ipa-camera.md) |
| APP-02 / CAM-02 | 4K/16K 脱壳 `.ipa` 免签安装（含 `:8088` 网页直传）与前后置 `OV13B10`/`IMX596` `AVCaptureSession` 预览/扫码/拍照已在 16K 容器应用内实机跑通；`cryptid=1` 加密包受硬件边界限制不可运行 | [11-27 攻坚](2026-11-27-unattended-sleep-ipa-camera.md) |
| GNSS-01 / BT-02 | 离线 `QMI_LOC` 1 Hz GPS → `CoreLocation` 与 `3 Mbps` 蓝牙已就绪；待在场窗边/户外验搜星定位及手动试蓝牙配对 | [11-26 晚间](2026-11-26-evening-unattended.md) |

## 待用户验收（集中 ≤12 分钟）

| 事项 | 时长 | 操作入口 |
| --- | --- | --- |
| 全功能主验收（熄屏深睡唤醒 + `:8088` 网页上传安装脱壳 IPA + 前后摄实时取景/扫码/拍照 + Wi-Fi 外放/主麦录音 + 外屏上滑 + 户外 GPS） | ~10 分钟 | `ZIZHAN_USER_PRESENT=yes bash work/m1-firstsound/run-video-4.sh`，详见 [待人工清单](HUMAN_CHECKLIST.md) |
| M3 不复位直跳独立双启动 RAM 预演（零分区写入） | ~3 分钟 | `ZIZHAN_USER_PRESENT=yes bash work/m3-standalone-1127/run-m3-ram-test.sh` |

## 近期分日记录索引

- **2026-11-27 凌晨（P0 专项攻坚）**：平台级熄屏深度休眠（纯电池 `-323 mA`）、巨魔/签名边界调研与原生 `IPA 安装器`（16K 脱壳 IPA 免签安装）、前后置 `OV13B10`/`IMX596` 系统级 `AVCaptureSession` 扫码与拍照 → [11-27 攻坚](2026-11-27-unattended-sleep-ipa-camera.md)。
- **2026-11-26 晚间、白天与凌晨**：Wi-Fi/音频 SMMU 并发、展开防闪白 + 外屏上滑、麦/扬增益重调、双开关电池页、`QMI_LOC` 1 Hz GPS 与 `OV13B10` RAW10 出图 → [晚间](2026-11-26-evening-unattended.md)、[白天](2026-11-26-daytime-unattended.md)、[凌晨](2026-11-26-overnight-unattended.md)。
- **2026-11-25 及更早**：外屏防误触四胶囊、全双工采集环、CS35L41 `4.34 s` 快启、`zcFlip` 合成栅栏与 `minisandbox` → [11-25 夜间](2026-11-25-evening-unattended.md)、[11-24 白天](2026-11-24-zoom-cadence-and-sandbox.md)。
