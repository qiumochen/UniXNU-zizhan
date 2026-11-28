# 2026-11-26 白天无人值守收口：扬声器电子音/音调修复、物理转轴折叠与外屏开机消隐、IMU 90° 对齐、锁向冷启动首帧防横跳、BM4V 实测电池健康页与蓝牙配对目录预置

本轮针对 `video-1` 在场主验收反馈的全部 6 项问题完成逆向定位、代码修复、主机 ASan/UBSan 回归与实机静默闭环（全程强制 `zizhan_pulse_dry=1`、`audio_output=0` 零外放；零分区刷写；经用户授权通过 `os-trial-edit.py` 增量更新至 `sens-hal-v7`，保留 `sens-hal-v4` / `sens-hal-v3` 完整回退链；`main` 保持 `dac504b` 不变）。

## 1. M1-AUDIO-DEC：修复扬声器“电子音、音调不对”与多会话起始欠载

- **根因定位**：
  1. `ZizhanAudioPrm.cpp` 中服务模式 `40/41/42` 原默认 `dec = 0`，将 `WR_SHMEM` 输出的 `48 kHz` 立体声紧凑 24-bit（`3 B/sample`，`2880 B / 480 frames`）字节流跳过 `PCM_DEC` 流解码器直接送入期望 32-bit 容器（`4 B/sample`）的 `MFC`，导致样点按 `4/3` 错位拼接且有效帧率失配。
  2. `daemon_round` 在双 CS35L41 上电臂展（耗时约 `90 ms`）后未重锚安全提前量（`0` 缓冲垫），致使 `video-1` 首会话即累计 `starved=410`。
- **改动**：
  - `drivers/PongPower/ZizhanAudioPrm.cpp`、`drivers/PongPower/ZizhanAudioServiceGate.h`：将模式 `40/41/42` 默认设为 `dec = 1`（启用 `WR_SHMEM → PCM_DEC → PCM_CNV → MFC` 标准解码链，`zizhan_audio_dec=0` 可回退对照），并在功放臂展后通过 `zizhan_service_daemon_start_frame` 预留 `5` 个周期（`50 ms`）安全缓冲垫；新增 `zizhan_audio_loopback=4`（`nodec-mfc-sal`）硬件环回对照模式与 `cs35l41-service-gate` / `cs35l41-prm-platform` 单测。
- **实机 A/B 验证（`build-play190`，`audio_output=0`）**：
  - `500 Hz` 硬件 ADSP `96 kHz` `MFC → SAL` 环回背靠背实测：旧路径 `dec=0`（`audio-loop-nodec-1`）测得 **`est_hz=1515 max_step_l=259`**（基频翻 `3.03×` 且伴随 `259` 幅度阶跃撕裂，精确复现“电子音、音调偏高失真”）；新默认 `dec=1`（`audio-loop-dec-1`）测得 **`est_hz=506 max_step_l=20`**（纯净连续 `500 Hz` 正弦波）。
  - 全双工多会话 `audio-hal-dec-1` 与 `ux-all-v5-1`：4/4 会话全部 `starved=0`（累计输出 `785,280` / `744,480` 帧，`440/660 Hz` 基频精确吻合；WCD9385 物理主麦同步拾音 `864,864` / `825,504` 帧，`bob_released=1`）。

## 2. M2-HINGE-FOLD：物理转轴角度驱动合盖切屏 + 展开态外屏开机小米图标消隐

- **根因定位**：
  1. `GPIO62` 磁敏霍尔在未吸附特定磁极位置时保持高电平（`raw=0x1 asserted=0`），而 SLPI `hinge_angle` 传感器虽已实时上报物理开合角（展开 `177°`、合盖 `4°～5°`），但尚未接入 `ZizhanHallProbe` 合盖状态机，且熄屏 `step_quiesce` 会停掉 `hinge_angle` 流。
  2. `ZizhanOuterProbe.h` 仅在合盖→展开跳变且 `prev == 1u` 时涂黑外屏 `INTF_2`（`0xb8fc9900`），手机展开态开机时从未触发跳变，导致外屏一直残留 UEFI 阶段的白色小米启动图标。
- **改动与实机验证（`fold-hinge-1` `play191`、`ux-all-v5-1` `play192` `2258f5a5d7ef`）**：
  - `ZizhanSlpiSensors.h`、`ZizhanSlpi.cpp`、`ZizhanHallProbe.h`、`ArmDTPlatform.cpp`：新增带回差防抖的 `step_hinge_fold`（`<= 35°` 判合盖、`>= 55°` 判展开），由 `gpio_asserted || (zizhan_slpi_hinge_closed() == 1)` 联合驱动内外屏切换，并在熄屏省电期保持 `hinge_on=1` 以确保合盖休眠后物理展开即刻唤醒；同步补齐精简 CI 内核桩函数。
  - `ZizhanOuterProbe.h`：`cover_desktop_worker` 在 `rootvnode` 就绪后若处于展开态立即执行一次 `draw(false)` 清黑 `INTF_2`，并将展开清屏条件扩至 `prev != 0u`（覆盖 `2:1` 外屏桌面直退）。
  - `ux-all-v5-1` 实机日志证实：开机展开态即刻输出 **`ZIZHAN_OUTER_FOLD: open black misr=ad495edf`** 抹除小米图标；转轴 `177° → 5°` 合盖即刻点亮外屏时钟（`closed clock 11-26 03:00 batt=99 chg=1 misr=b54e7054`）并关闭内屏（`PASS target=off`），展开 `5° → 177°` 即刻恢复外屏纯黑 `ad495edf` 并唤醒内屏（`PASS target=on`）。

## 3. SENS-ROT90：SLPI 三轴 IMU 坐标系顺时针 `90°` 对齐

- **根因定位与改动**：Mix Fold 2 8.02 英寸内屏扫描方向与传感器芯片物理贴装方向相差 `90°`（竖直握持时原 `+Y` 重力分量落入横屏轴）。在 `ZizhanSlpiSensors.h` / `ZizhanSlpi.cpp` 中将默认 `kDefaultImuRot` 设为 `1`（`(x', y', z') = (-y, +x, +z)`，同步作用于加速度计、陀螺仪与磁力计，支持 `zizhan_imu_rot=0..3` 覆盖），通过 `slpi-sensors` ASan/UBSan 单测及 `ux-all-v5-1..3` 实机 7 路传感器并发流验证（`ZIZHAN_SLPI: RUNNING pas=12 ... imu_rot=1`）。

## 4. UX-SCENE-ORIENT：消除锁定方向下冷启动新开应用的横屏首帧闪跳

- **逆向根因定位（两层叠加）**：
  1. `SpringBoard` `-[SBDeviceApplicationSceneHandle _modifyApplicationSceneSettings:fromRequestContext:entity:]`（`0x1d2530b28`）在应用冷启动创建新 `FBScene`（`[self sceneIfExists] == nil`）时，`traitsParticipantForSceneHandle:` 尚未在切换器注册而返回 `nil`（跳至 `0x1d2531248`），使初始 `UIMutableApplicationSceneSettings`（转发至 `FBSSceneSettingsCore`）保留默认方向 `1`（在 Mix Fold 2 面板上呈横向 `1080×958`）。
  2. 同时 `-[SBDeviceApplicationSceneHandle _mainSceneSupportedInterfaceOrientations]`（`0x1d251fbb4`）在应用首帧 `uiClientSettings` 回传前读取 `FBSApplicationInfo`，对仅配置了 `UISupportedInterfaceOrientations~ipad` 与 `~iphone` 的系统应用（如 `Preferences.app`）仅返回 `0x2`（仅 Portrait），导致首帧强制按 `1` 启动、待首帧 `uiClientSettings` 上报 `0x1e` 后才二次旋转为 `4`（`958×1080`）。
- **改动与实机闭环（`ux-all-v5-3`，`sens-hal-v7`）**：
  - 新增纯 C 策略头 `userland/zizhan-scene-orient18.h` 与 `tools/zizhan/tests/scene-orient18.{c,py}`，并在 `userland/ui_compat18.c` 挂载 ABI 校验钩子（`zizhan_scene_orient=0` 可关闭）：在非 Classic iPad 应用首帧 `uiClientSettings` 到达前按真实 `Info.plist` `UISupportedInterfaceOrientations~ipad` 补齐支持掩码（纯竖屏应用仍严格保持 `0x2` 不强转），并在 `sceneIfExists == nil` 时直接按 `SBOrientationLockManager` 锁定方向（或当前主屏活动方向）预设初始 `interfaceOrientation`。
  - `ux-all-v5-3` 实机冷启动「设置」日志：**`Zizhan scene18: cold launch initial orientation 1 -> 4 (after=4 locked=1 lock_ori=4 active=4)`**，紧接着 `Preferences` 主队列第 `0` 次轮询即输出 **`Zizhan scene18 diag[Preferences]: poll=0 win=0/1 UIWindow wori=4 bounds=958x1080`**，首帧即竖直 `958×1080`，彻底消除横屏闪跳。

## 5. POWER-BATT-UI：「设置 → 电池」接入高通 ADSP 硬件实测健康度与电量日志目录

- **根因定位**：
  1. 默认 `iPad7,12` 身份下原厂 `BatteryUIController` 不显示电池健康入口，且 `Preferences` 进程内 `IOPSCopyPowerSourcesByType("InternalBattery")` 返回空（`no internal source`）；而内核 `PongBattery`（`AppleSmartBattery`）已在 IORegistry 实时发布高通 ADSP `battmgr` 真实电芯数据。
  2. 用量曲线后台 `PerfPowerServices` / `powerlogHelperd` 通信在 `video-1` 与本轮均正常（`Received valid BUI graph response on try 0.`），仅因短轮测试尚未跨多小时桶累积样本而显示系统原生提示语。
- **改动与实机闭环（`ux-all-v5-2`、`ux-all-v5-3`）**：
  - 新增 `userland/zizhan-battery-ui18.h` 与 `tools/zizhan/tests/battery-ui18.{c,py}`，在 `userland/settings_compat18.c` 通过 IOKit `IOServiceGetMatchingService("AppleSmartBattery")` 直读真实硬件属性并挂载至 `BatteryUIController`（及 `iPhone17,1` 下的 `BatteryHealthUIController`），**零伪造**任何 Apple 认证或维修状态位；同时在 `userland/init.c` 与 `userland/build-ipados18.py` 预建 `/private/var/mobile/Library/BatteryLife/Archives`（`501:501`）确保持久化落盘。
  - `ux-all-v5-3` 实机 `UITableView` 渲染转储确认完整展示：**`电池健康（BM4V 硬件实测）`** 分组、**`最大容量` `78% (3502 / 4500 mAh)`**、**`循环计数` `916 次`**、**`电芯状态` `SOH 80% · 4383 mV · 33.1 °C`** 及原生 `PLBatteryUIGraphTextCell`。

## 6. BT-PAIR-PREP：蓝牙配对密钥目录预建与安全配对 HCI/SMP 通透校验

- **逆向核查与改动**：反汇编 `bluetoothd` 定位 `OI_LinkKeyStorage_WriteKey`（`0x1001485c8`）对 `%s/Library/Preferences/Bluetooth` 仅做单层 `mkdir(..., 0755)`；在 `userland/init.c` 与 `userland/build-ipados18.py` 开机预建 `/private/var/MobileBluetooth`、`/private/var/mobile/Library/Preferences/Bluetooth`（`0755`，`501:501`）与 `/private/var/root/Library/Preferences/Bluetooth`（`0700`，`0:0`），并在 `tools/zizhan/tests/bt-hci-shim.c` 新增 Classic SSP（`0x040b/0x0413/0x042b/0x042c`）、BLE LESC（`0x2019/0x201a/0x2025/0x2026`）、LE Privacy 解析表及含 `0xfc/0xfd/0xfe` 密钥字节的 L2CAP SMP ACL 帧零篡改直通测试（无人值守全程严禁主动向外部设备发起配对）。

## 7. `sens-hal-v7`、在场验收包与 `m3-standalone-1127` 刷新

- **手机端已就绪状态**：已安装并整盘回读校验 **`sens-hal-v7`**（OS `6b4d10581843…`，UI `9b09a0942776…`），历史回退包 `sens-hal-v4` / `sens-hal-v3` 原样保留。
- **在场与独立双启动产物（基于最终内核 `build-play192` `2258f5a5d7ef…`，零分区写入）**：
  - `out/video-2`（`2b2a7455020f…`）：`run-video-2.sh`（`run-video-1.sh` 自动转发并保留历史 `logs/video-1.txt`）。
  - `out/fold-desk-1`（`172c0b7f745b…`）：`run-fold-desk-1.sh`（合盖直进 `2:1` 实时桌面）。
  - `work/m3-standalone-1127/pong-xnu-aprime.img`（`239b10412029…`，`direct=1`）：离线通过 `check-m3-standalone.py` 全项与原厂 `boot_b.img.gz` 哈希校验（`M3_STANDALONE_PREFLIGHT: PASS`，`backup_verified: true`）。
- **15 分钟全子系统静默浸泡 `soak-1126-day2`（`play192` `2258f5a5d7ef` + `sens-hal-v7`，`900 s`）**：全开 `dec=1` 多会话静音音频 + WCD9385 物理主麦、`3 Mbps` 蓝牙（`rx=65,766 B`、`tx=19,126 B`、`113` 次 IBS 唤醒 `fail=0`）、3 轮合盖时钟/桌面/展开循环、7 路 `imu_rot=1` SLPI 传感器（`36,068` 次加速度/陀螺、`3,472` 次磁力、`3,465` 次气压 `99,615 Pa`、转轴 `177.0°`、距离 `prox=4`、`6/6` 次待机停流恢复）、15 轮应用冷启动锁向校验（`1 -> 4`）与 Wi-Fi 连续 ping **`700 / 704 s`（`>2 s` 断档 `0` 次）**，CPU 峰值仅 `59.9 °C`（限频 `0/53`），`0` panic、`0` `SIGABRT`、`0` `PastBuffer`、`0` `NONE FREE`，到期平稳回 Android（电量 `100%`）。
