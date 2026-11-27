# 当前进度与接续入口

更新：2026-11-27 下午（UTC+8）。本页只保留**当前**状态，限一页。11-17 之前的完整台账原文在 [2026-11-17-status-archive.md](2026-11-17-status-archive.md)；规划依据见 [接手梳理与重新规划](../plan/2026-11-17-takeover-replan.md)。管理规则见 [AGENTS.md](../../AGENTS.md#progress-management)，本机操作细节在仓库外 `$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`。

## 推进规则（11-17 起）

- 一条主线 + 最多一条纯主机支线。主线按**用户能感知的功能**验收，“限定 PASS”只算过程证据。
- 里程碑最长 1 周，结束时做一次 ≤30 分钟的集中用户验收；需要人在场的步骤提前约时间。
- 同一问题连续 6 次设备轮或 1 天没有用户可见进展 → 停下复盘，换假设或挂起，并写在本页。
- 每天收尾在 Mac 上按主题本地提交；里程碑结束后推送。本页每次更新都要保持一页。

## 基线

| 项目 | 状态 |
| --- | --- |
| 源码 | `main`（`dac504b`）保持稳定基线；`wip/2026-11-17-audio-sensors` 在三大 P0 基础上再完成第二梯队 1–4 与第三梯队 2：**熄屏纯电池深睡破 1 W（`-219 mA / 952 mW`，稳态底噪 `730 mW`）**、**非破坏性 `64 GiB` 根卷（`48 GiB` Data）+ `IPA 安装器` v2（`<=12 GiB` 流式上传 + 第三方容器 Metal/Keychain/AppGroup/`9:16` 视口 `8/8 PASS`）**、**`960×720` 三摄 `AVCaptureSession`（`1x/0.6x/前置` 热切 + `DW9800V` 对焦 + `RumbaS4H` OIS）**、**PM8350B X 轴线性马达 + 顶部听筒路由** 与 **外屏 `1:1` 满高视网膜桌面（`1080×1916` + `ZOOM` 胶囊）**。 |
| 启动方式 | Mac 后部 USB-A `fastboot boot` 临时启动（默认自动返回、电源键 3 s 回 fastboot）；M3 双启动包 `m3-standalone-1128`（`43fbb97bf71d…`，`build-play205`）已通过离线全项与原厂备份校验，待在场 RAM 预演。 |
| 手机安装 | iPadOS 18.7.10 / 22H374（默认 `iPad7,12`），新增单段连续 `64 GiB` **`sens-hal-v23-64g`**（`/data/hackios/root-64g.img`，`Data` 空余 `47.2 GiB`，UI `253ae60032b2…`），并 `100%` 原样保留 `19.4 GiB` `sens-hal-v22`（`/data/hackios/root.img`）及 10-28 全分区备份。 |
| 默认能用 | 内屏 120 Hz + GPU 零拷贝、**`<1 W` 熄屏深睡与开合/电源键秒醒**、**`48 GiB` Data + `IPA 安装器` v2（大型脱壳应用免签安装/Metal/Keychain/卸载）**、**第三方应用 `960×720` 三摄扫码/拍照/切换**、**X 轴线性马达触感**、**通话听筒/双扬声器自动路由**、**外屏 `1:1` 满高桌面 + `ZOOM` 切换**、Wi-Fi、`3 Mbps` 蓝牙、双开关电池页、北京时间、温控、1 Hz GPS。 |
| 需参数/待在场 | **全功能集中验收 `run-video-5.sh`**（`<1 W` 熄屏深睡 + `48 GiB` 空间局域网直传大包 IPA + 第三方三摄 `960×720` 扫码/拍照 + 线性马达手感 + 通话听筒/外放 + 外屏 `1:1` 满高操作 + 户外 GPS）、M3 双启动 RAM 预演。 |
| 不能用 | 未脱壳 App Store 加密 IPA（`cryptid=1`，安装器已拦截提示）、原厂系统 `Camera.app`（`FigCapture` 私有 C 层未桩，第三方相机/`AVCaptureSession` 均正常）、蜂窝数据（MPSS 锁定离线仅跑 GPS）、天气预报（卡 FairPlay）。 |

## 当前里程碑

| ID | 里程碑 | 状态 | 验收 | 下一动作 |
| --- | --- | --- | --- | --- |
| M0 | 收口 | DONE（11-17） | 未提交工作按主题提交并推 wip 分支；CI 全绿；本页一页 | 已完成。 |
| M1 | 能出声 + 麦克风 + 听筒/触感 | DOING（待在场终验） | 双扬外放响亮、主麦录音清晰、VoiceChat 切顶部听筒、线性马达清脆 | 已补齐顶部听筒路由与 PM8350B X 轴线性马达；下一步：在场跑 `run-video-5.sh` 一次终验。 |
| M2 | 外屏 + 深睡 `<1 W` + 64G 应用生态 | DOING（待在场终验） | 深睡 `<1 W` 秒醒、`48 GiB` 大包脱壳 IPA 顺畅运行、三摄 `960×720` 可用、外屏 `1:1` 顺手 | `POWER-08`（`952 mW`）、`APP-03`（`8/8 PASS`）、`CAM-03` 与 `OUTER-03` 均已实机闭环；待在场手持确认。 |
| M3 | 离开电脑启动 | DOING（需在场） | 不接电脑开机：默认进安卓，菜单可选 iPadOS（挂载 `64 GiB` 根卷）；回退保留 | `m3-standalone-1128`（`43fbb97bf71d…`）已刷新至 `build-play205` 并通过校验；仅在用户在场时先跑 RAM 测试。 |

## 挂起与边界（用户点名才恢复，详见对应记录）

| ID | 当前状态与停点 | 记录 |
| --- | --- | --- |
| POWER-08 深睡功耗 | 外屏 `DSI1` 断电 + GMU DDR 释放 + `SN7` 压降 + GPS 暂停后纯电池深睡降至 **`-219 mA / 952 mW`**（稳态 `730 mW`），`25+` 轮唤醒 `0` panic；已达日用长续航目标 | [11-27 二期战役](2026-11-27-tier2-campaign.md) |
| CAM-03 / VPU-01 / USB-01 | 第三方容器三摄 `960×720` + `DW9800V` AF + `RumbaS4H` OIS 已跑通，`IMX766` C-PHY 包头已锁定（像素落盘待迁完整 `IFE_0`）；`PAS 9` VPU 支持已确认（`video_region` 受 Stage-2 XPU 保护）；`USB3_GDSC` 激活已确认 | [11-27 二期战役](2026-11-27-tier2-campaign.md) |
| GNSS-01 / BT-02 | 离线 `QMI_LOC` 1 Hz GPS → `CoreLocation`（深睡自动暂停省电）与 `3 Mbps` 蓝牙已就绪；待在场窗边/户外验搜星定位及手动试蓝牙配对 | [11-26 晚间](2026-11-26-evening-unattended.md) |

## 待用户验收（集中 ≤12 分钟）

| 事项 | 时长 | 操作入口 |
| --- | --- | --- |
| 全功能主验收（`<1 W` 深睡 + `48 GiB` 大包 IPA 网页流式直装 + 第三方 `960×720` 三摄扫码/拍照 + X 轴线性马达 + 听筒/外放 + 外屏 `1:1` 满高桌面 + 户外 GPS） | ~10 分钟 | `ZIZHAN_USER_PRESENT=yes bash work/m1-firstsound/run-video-5.sh`，详见 [待人工清单](HUMAN_CHECKLIST.md) |
| M3 不复位直跳独立双启动 RAM 预演（零分区写入，直进 `64 GiB` 根卷） | ~3 分钟 | `ZIZHAN_USER_PRESENT=yes bash work/m3-standalone-1128/run-m3-ram-test.sh` |

## 近期分日记录索引

- **2026-11-27 下午（第二/三梯队专项战役）**：深睡破 `1 W`（`952 mW`）、非破坏性 `64 GiB` 根卷 + `IPA 安装器` v2 + 第三方沙盒 `8/8` 兼容、`960×720` 三摄 + OIS/AF、线性马达/听筒路由与外屏 `1:1` 满高模式 → [11-27 二期战役](2026-11-27-tier2-campaign.md)。
- **2026-11-27 凌晨（三大 P0 攻坚）**：平台级熄屏深睡、巨魔/签名边界调研与原生 `IPA 安装器` v1、前后双摄 `AVCaptureSession` 扫码与拍照 → [11-27 凌晨 P0](2026-11-27-unattended-sleep-ipa-camera.md)。
- **2026-11-26 及更早**：Wi-Fi/音频 SMMU 并发、增响麦/扬、双开关电池页、`QMI_LOC` 1 Hz GPS、外屏防误触四胶囊与 `minisandbox` → [11-26 晚间](2026-11-26-evening-unattended.md)、[11-26 白天](2026-11-26-daytime-unattended.md)、[11-25 夜间](2026-11-25-evening-unattended.md)。
