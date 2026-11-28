# 当前进度与接续入口

更新：2026-11-28 晚（UTC+8）。本页只保留**当前**状态，限一页。11-17 之前的完整台账原文在 [2026-11-17-status-archive.md](2026-11-17-status-archive.md)；规划依据见 [接手梳理与重新规划](../plan/2026-11-17-takeover-replan.md)。管理规则见 [AGENTS.md](../../AGENTS.md#progress-management)，本机操作细节在仓库外 `$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`。

## 推进规则（11-17 起）

- 一条主线 + 最多一条纯主机支线。主线按**用户能感知的功能**验收，“限定 PASS”只算过程证据。
- 里程碑最长 1 周，结束时做一次 ≤30 分钟的集中用户验收；需要人在场的步骤提前约时间。
- 同一问题连续 6 次设备轮或 1 天没有用户可见进展 → 停下复盘，换假设或挂起，并写在本页。
- 每天收尾在 Mac 上按主题本地提交；里程碑结束后推送。本页每次更新都要保持一页。

## 基线

| 项目 | 状态 |
| --- | --- |
| 源码 | **`main`（`a05159d`）已合入截至 `9488efa` 的全部真机验证能力并提升为仓库默认配置**；`wip/2026-11-17-audio-sensors` 在此基础上**已完成 `OUTER-04`、`OUTER-05`、`CAM-04` 与免重启局域网实时调试基建 `DBG-01` 的主机实现、多轮独立审查修复与双配置编译（`HOST_VERIFIED`）**。 |
| 启动方式 | Mac 后部 USB-A `fastboot boot` 临时启动（默认菜单标题 `iPadOS 18 (zizhan)`、自动返回、电源键 3 s 回 fastboot）；新内核 `build-play206` 离线启动包 `out/video-8`（含 `zizhan_debugd=1`） / `out/daily-1129`（默认关闭 `debugd`）与 M3 双启动包 `m3-standalone-1129` 均通过离线与原厂备份校验，待手机接回实测。 |
| 手机安装 | 本轮手机未接电脑，机内 `/data/hackios/root-64g.img` 仍为 **`sens-hal-v26-64g`**（UI `a370df9c8804…`），`19.4 GiB` 回退卷 `/data/hackios/root.img`（`sens-hal-v22`）与 10-28 全分区备份 `100%` 原样保留；新 UI 库已在主机编好，接回手机后由 `run-video-8.sh` 自动增量写入 `sens-hal-v27-64g` 并整盘回读校验。 |
| 默认能用（`main` 已实机验证） | 内屏 120 Hz + 四向 GPU 零拷贝、`<1 W` 熄屏深睡（`-219 mA / 952 mW`）、`48 GiB` Data + 主屏第 1 页三枚预置应用（`IPA 安装器` v2 / `扫码相机验证` / `兼容套件验证` 9 按钮台 + X 轴马达 + 听筒/双扬/主麦，`ZIZHAN_USER_PRESENT=yes` 出声）、Wi-Fi、`3 Mbps` 蓝牙、SLPI 7 传感器 + 自动调光、双开关电池页、北京时间、温控。 |
| 主机已修待上机终验（`wip` `HOST_VERIFIED`） | 1. **`CAM-04`**：前后摄 `90°/270°` 物理旋转、原生偶对齐 Bayer 解马赛克、高增益降噪 + sRGB CCM + 时域 IIR 及预览层四向自适应；<br>2. **`OUTER-04` / `OUTER-05`**：合盖必进锁屏时钟、解锁默认 `1:1` 满高桌面、合盖重力+SpringBoard 双重竖屏锁，以及补丁 `0048` 外屏电源键亮灭 `DSI1` 与深睡唤醒；<br>3. **`DBG-01`**：按需开启（`zizhan_debugd=1` + `/chosen/zizhan-dbg-token`）的 `:8089` Web 远程控制台与 `zctl.py`（无锁 ramoops 流、内外双屏高清实时截图、远程触控/开合盖/看门狗续租/干净重启，及 5s 试用自动回退的 `libpong-ui18.dylib` 免重启热重载）。 |
| 不能用 | 未脱壳 App Store 加密 IPA（`cryptid=1`，安装器已拦截提示）、原厂系统 `Camera.app`（`FigCapture` 私有 C 层未桩）、蜂窝数据（MPSS 锁定离线，仅可选 `zizhan_gnss=2` 跑 1 Hz GPS）、天气预报（卡 FairPlay）。 |

## 当前里程碑

| ID | 里程碑 | 状态 | 验收 | 下一动作 |
| --- | --- | --- | --- | --- |
| M0 | 收口与默认化 | DONE（11-28） | 已真机验证能力合入 `main` 默认配置；CI 全绿；本页一页 | `main`（`a05159d`）已合入并同步回 `wip`。 |
| M1 | 能出声 + 麦克风 + 听筒/触感 | DOING（待在场终验） | 双扬外放响亮、主麦录音清晰、VoiceChat 切顶部听筒、线性马达清脆 | 默认打包已就绪；接回手机后随 `run-video-8.sh` 一并终验。 |
| M2 | 外屏 + 深睡 `<1 W` + 64G 应用生态 | `HOST_VERIFIED`（待接手机实测） | 深睡 `<1 W`、`48 GiB` 安装已进 `main`；`OUTER-04`/`OUTER-05`/`CAM-04` 实机闭环 | 接回手机运行 `run-video-8.sh` 写入 `sens-hal-v27-64g` 并上机验收，通过后合入 `main`。 |
| M3 | 离开电脑启动 | `HOST_VERIFIED`（需在场） | 不接电脑开机：默认进安卓，菜单可选 `"iPadOS 18 (zizhan)"`（挂载 `64 GiB` 根卷）；回退保留 | `m3-standalone-1129` 已通过离线全项与原厂备份校验；接回手机后先跑 RAM 预演。 |

## 挂起与边界（用户点名才恢复，详见对应记录）

| ID | 当前状态与停点 | 记录 |
| --- | --- | --- |
| POWER-08 深睡功耗 | 外屏 `DSI1` 断电 + GMU DDR 释放 + `kStandbyBw=3`（`MM0/SN2/SN7/MC0/SH0`）+ GPS 暂停后纯电池深睡 **`-219 mA / 952 mW`**（稳态 `730 mW`），已提升为 `main` 默认 | [11-27 二期战役](2026-11-27-tier2-campaign.md) |
| CAM-04 / OUTER-04 / OUTER-05 / DBG-01 | 主机侧源码修复、多轮独立上下文审查闭环、ASan/UBSan + 本地 E2E 回归、双 defconfig 内核及离线包均已完成；待接回手机上机验证 | [11-28 主机修复与审查](2026-11-28-outer-cover-and-cam04-host.md) |
| GNSS-01 / BT-02 | `3 Mbps` 蓝牙已设为默认，离线 `QMI_LOC` 1 Hz GPS → `CoreLocation` 保持可选 `zizhan_gnss=2`；待在场窗边/户外验搜星定位及手动试蓝牙配对 | [11-26 晚间](2026-11-26-evening-unattended.md) |

## 待用户接回手机后验收（集中 ≤12 分钟）

| 事项 | 时长 | 操作入口 |
| --- | --- | --- |
| 自动更新 `sens-hal-v27-64g` + 全功能在场验收（外屏锁屏时钟/竖屏锁/电源键亮灭屏 + 三摄正方向低噪点扫码拍照 + `<1 W` 深睡 + 音频/触感） | ~10 分钟 | `ZIZHAN_USER_PRESENT=yes bash work/m1-firstsound/run-video-8.sh`，详见 [待人工清单](HUMAN_CHECKLIST.md) |
| M3 不复位直跳独立双启动 RAM 预演（零分区写入，菜单显示 `"iPadOS 18 (zizhan)"`，直进 `64 GiB` 根卷） | ~3 分钟 | `ZIZHAN_USER_PRESENT=yes bash work/m3-standalone-1129/run-m3-ram-test.sh` |

## 近期分日记录索引

- **2026-11-28 下午（`main` 默认化收口 + `wip` 主机修复）**：`main` 合入已验证默认配置；`wip` 完成 `OUTER-04`、`OUTER-05`、`CAM-04` 与 M3 菜单标题主机修复及独立审查闭环 → [11-28 主机修复与审查](2026-11-28-outer-cover-and-cam04-host.md)。
- **2026-11-27 下午（第二/三梯队专项战役）**：深睡破 `1 W`（`952 mW`）、非破坏性 `64 GiB` 根卷 + `IPA 安装器` v2 + 第三方沙盒 `8/8` 兼容、`960×720` 三摄 + OIS/AF、线性马达/听筒路由与外屏 `1:1` 满高模式 → [11-27 二期战役](2026-11-27-tier2-campaign.md)。
- **2026-11-27 凌晨及更早**：平台级熄屏深睡、原生 `IPA 安装器` v1、双摄扫码与前期无线/音频/传感器进展 → [11-27 凌晨 P0](2026-11-27-unattended-sleep-ipa-camera.md)、[11-26 晚间](2026-11-26-evening-unattended.md)。
