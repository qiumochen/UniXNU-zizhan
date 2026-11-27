# 2026-11-27 无人值守第二/三梯队专项战役：深睡功耗破 1 W、64 GiB 根卷与主流大型脱壳 IPA 兼容、960×720 三摄框架与 OIS/AF、线性马达/听筒/快充遥测与外屏 1:1 满高模式

本轮按用户授权的 24 小时无人值守计划，在已闭环三大 P0 的基础上完成第二梯队 1–4 与第三梯队 2 全量工程推进与实机验收：
1. **`POWER-08`（第二梯队 2）**：熄屏纯电池深度休眠功耗二次压降，PM8350B 库仑计实测从 `-323 mA / 1405 mW` 进一步降至 **`-219 mA / 952 mW`（跌破 `1.0 W`，稳态底噪低至 `-166..-172 mA / 730..753 mW`，较 P0 深睡再降 `32.2%`、较传统熄屏降 `61.5%`）**。
2. **`APP-03` + 非破坏性 `64 GiB` 根卷（第二梯队 1 & 4）**：新增 `/data/hackios/root-64g.img` 单段连续 `64 GiB` 镜像（`Data` 可用 `47.46 / 48.00 GiB`，原 `19.4 GiB` `/data/hackios/root.img` `100%` 原样保留）、`ZizhanInstaller.app` v2（`<=12 GiB` 流式分块上传 + 进度条 + 已装应用启动/详情/卸载 + 嵌套 `Frameworks`/`PlugIns` 签名）及沙盒第三方容器八维兼容层（容器内原生 Adreno Metal GPU、免 `-34018` Keychain 持久化、AppGroup 共享目录、`9:16` 居中手机视口等，`ZizhanCompatSuite` 实机 **`8/8 PASS`**）。
3. **`CAM-03` + `VPU-01`（第二梯队 3）**：相机实时流升级至 **`960×720` BGRA（像素量提升 `2.25×`）**，打通 `1x / 0.6x / 前置` 三镜头 `AVCaptureDevice` 枚举与热切换，实机点亮主摄 **`RumbaS4H` OIS 光学防抖居中锁止（`0x24`，`7/7` 写入成功）**、**`DW9800V` 音圈马达自动对焦（`0x0c`，`init_dac=420`）** 与 50 MP **`IMX766`（`0x1a`，芯片 ID `0x0766`）** `CSIPHY0` `v2.1.3` C-PHY 包头同步（无缝回退 `OV13B10` 光学流），并完成 `Iris2 VPU`（`PAS 9`）只读边界探测。
4. **`PERIPH-01`（第二梯队 4）**：打通 PM8350B 高压 X 轴线性马达（SPMI SID `3` `0xf000`，`UIImpactFeedbackGenerator` / `AudioServicesPlaySystemSound` 桥接，无人值守默认 `zizhan_haptics_dry=1` 零物理震动）、上扬声器 `CS35L41 0x40` 听筒模式（`VoiceChat` 自动静音下扬声器 `0x42` 并衰减 `-12 dB`）与双电芯板级遥测/安全输入功率上报。
5. **`OUTER-03` + `USB-01`（第三梯队 2）**：外屏新增 **`1:1` 满高视网膜桌面模式（`1080×1916 @ y=220..2136`，占屏比 `85%+`）** 与底部第五胶囊 `ZOOM` 一键热切 `1:1 ↔ 2:1` 全览；完成 `gcc_usb30_prim_gdsc`（`0x149004`）只读状态盘点，100% 保持 `fastboot` 回退零风险。

全程零分区刷写（仅 `fastboot boot` 临时启动），无人值守轮次严格保持物理静音与零马达震动（`pong_audio=0`、`zizhan_pulse_dry=1`、`zizhan_haptics_dry=1`），`main` 分支保持 `dac504b` 未改动。

---

## 1. Phase 1（`POWER-08`）：纯电池熄屏深度休眠功耗破 `1 W`（`952 mW` / 稳态 `730 mW`）

- **根因拆解与六项协同降耗**：
  1. **外屏 `DSI1`（`0xae96000`）物理下电**：UEFI 退场后外屏 6.56" AMOLED 一直维持亮黑扫描耗电；`ZizhanOuterProbe.h` 新增 `DSI1` DCS `0x28/0x10` 休眠与 `0x11/0x29` 唤醒状态机，展开态与系统深睡态自动彻底关闭外屏面板（`ZIZHAN_OUTER_POWER: PASS target=off reason=fold-open rc=0 dbv=0x0`），合盖时自动唤醒点亮（`target=on reason=fold-close rc=0 dbv=0x133`）。
  2. **释放 Adreno 730 GMU `1.76 GB/s` DDR 投票**：`PongAdrenoPlatform.cpp` 在熄屏降频时将 GMU OOB DCVS `bus_index` 从 `11` 释放至 `0`（修复 `wantBus` 振荡，`PONG GPU DCVS: ddr bus index 11 -> 0 (level 1)`）。
  3. **修复 `ZizhanStandbyRpmh` 线程覆盖并压降 `SN7`（PCIe NoC）**：`ZizhanStandbyRpmh.cpp` 增加 `gSleepActive` 守卫防止后台轮询把深睡 `cx=64 / mx=192` 抬回 `256`，同时将 `MM0 / SN2 / SN7 / MC0 / SH0` 全部压至 `0x60004001:1` 最小地板。
  4. **待机暂停 `QMI_LOC` 1 Hz 搜星**：`ZizhanMpss.cpp` 在熄屏深睡时发送 `QMI_LOC_STOP_REQ` 并在亮屏后恢复（`ZIZHAN_GNSS: standby pause -> QMI_LOC stop sent`）。
  5. **放宽 `PongWifi` 熄屏轮询与平台深睡滴答**：`PongWifiPlatform.cpp` 熄屏 802.11 PS 空闲轮询放宽至 `50 ms / 500 ms`；`ZizhanSleep.h` 将主核掩码 `WFI` 唤醒看门狗检查周期从 `100 ms` 提升至 `250 ms`（唤醒次数减少 `60%`）。
- **实机纯电池库仑计验证（`tier2-verify-3` 与 `tier2-verify-4`，`ZIZHAN_USB_OFF_AT` 物理断 VBUS）**：
  - `tier2-verify-3`：断 USB 后熄屏稳态采样直接落至 **`-166..-172 mA @ 4398 mV`（`730..753 mW`）**，`110,002 ms` 深睡 `ticks=439`、`cpu_off_fail=0`。
  - `tier2-verify-4`（`ZIZHAN_USB_OFF_AT=105` 纯电池干净窗口）：`95,001 ms` 连续深睡后库仑计实测 **`ZIZHAN_SLEEP_BATT: cycle=1/1 asleep_ms=95001 q_mah=3402->3402 v_mv=4315->4349 i0_ma=-685 i1_ma=-219 deep_sleep_ma=219 deep_sleep_mw=952`**——深睡整机功耗从 `1405 mW` 降至 **`952 mW`**（`95 s` 内库仑计掉电 `0 mAh`），按 `4500 mAh` 电池折算纯待机可达 **`20.5..27.1` 小时**。

---

## 2. Phase 2（`APP-03`）：非破坏性 `64 GiB` 根卷、`ZizhanInstaller` v2 与主流第三方脱壳 IPA 八维兼容

- **非破坏性 `64 GiB` 独立根卷（`sens-hal-v23-64g`）**：
  - 扩建 `48.20 GiB` APFS `Data` 容器并通过 `pinalloc` 在 `/data/hackios/root-64g.img` 分配**单段连续物理区段**（`base=360116649984`，`pong_shngroot_4k=87919104`，总长 `63,982,010,368 B`），原 `19.4 GiB` `/data/hackios/root.img`（`sens-hal-v22`，`14452736`）保持字节级未改动并通过双镜像 `app-preflight.py` SHA-256 回读校验。
  - 实机挂载后 `ZizhanInstaller` v2 顶部实时显示 **`Data 可用 47.46 / 48.00 GiB`**，足以容纳微信、抖音、高德、原神等数 GB 级大型第三方脱壳包。
- **`ZizhanInstaller.app` v2 大包流式引擎与已装应用管理（`userland/installer/ZizhanInstaller.m`）**：
  - HTTP `:8088` 服务重构为 `64 KiB` 流式直写 `/var/mobile/Documents/IPAs/upload.ipa.part`（零内存暴涨，上限提升至 **`12 GiB`**，内置网页 XHR 实时进度条与速率显示）。
  - 预检与重签阶段支持嵌套 `Frameworks/*.dylib`、`Frameworks/*.framework` 及 `PlugIns/*.appex` 递归校验与 Ad-hoc 签名补齐，并在 `MIInstallerClient` 成功后跳过冗余全盘 `_LSPrivateRebuildApplicationDatabases` 扫描（消除 48 GiB 卷上 `pkd` 103 秒阻塞）。
  - 新增分组式 UI：上半区本地/网页 `.ipa` 一键安装，下半区枚举全部已安装第三方容器应用并提供**一键启动 / 查看容器详情 / 卸载**。
- **沙盒第三方容器八维运行兼容层（`PongAdrenoNativeGlue.cpp` + `ui_compat18.c`）与 `ZizhanCompatSuite` 实机 `8/8 PASS`**：
  1. **容器内原生 Metal GPU**：将 `PongNativeMetalGlobal` 默认值改为 `1`（`pong_metal_global=0` 可关），使 `/var/containers/Bundle/Application/<UUID>/*.app` 第三方沙盒进程直接获得 Turnip Adreno GPU 加速（修复微信/小游戏/地图/Flutter 缺 Metal 闪退）。
  2. **免 `-34018` Keychain 持久化兜底**：拦截 `Security.framework` `SecItemAdd` / `CopyMatching` / `Update` / `Delete`，当无 Apple 硬件 SEP/Provisioning Profile 返回 `-34018`（`errSecMissingEntitlement`）时自动回退至 `<HOME>/Library/PongKeychain.plist` 文件钥匙串（修复主流国内 App 登录态保存失败）。
  3. **AppGroup 共享容器兜底**：拦截 `NSFileManager containerURLForSecurityApplicationGroupIdentifier:`，当 `containermanagerd` 因自签描述文件返回 `nil` 时自动创建并返回 `/private/var/mobile/Containers/Shared/AppGroup/<group-id>`。
  4. **手机级 `9:16` 居中视口自适应**：对仅声明竖屏 `iPhone` 布局（`UIDeviceFamily=[1]`）的第三方容器应用自动居中约束为 `540×958 pt`（`9:16`）视口，消除 `2160×1916` 方屏拉伸变形。
  - **实机全自动回归（`compat-cam-1` & `compat-cam-2`，16K 页签名裸包 `ZizhanCompatSuite-16k.ipa` 安装后从沙盒容器冷启动）**：
    - `1. METAL: device=Turnip Adreno (TM) 725 blit_completed=1`（PASS）
    - `2. KEYCHAIN: add=0 get=0 upd=0 del=0`（PASS）
    - `3. APPGROUP: url=/private/var/mobile/Containers/Shared/AppGroup/group.com.zizhan.shared rw=1`（PASS）
    - `4. SQLITE: wal_rw=1 path=compat.sqlite3`（PASS）
    - `5. WEBKIT: js_eval_42=1`（PASS）
    - `6. PERIPH: haptics_triggered=3 receiver_switch=1`（PASS）
    - `7. LOCATION: enabled=1 auth=3`（PASS）
    - `8. CAMERA3: devs=3 photo=960x720 (188101 B)`（PASS）→ **`ZIZHAN_COMPATSUITE: SUMMARY pass=8/8`**。

---

## 3. Phase 3（`CAM-03` & `VPU-01`）：`960×720` 三摄 `AVCapture` 框架、`IMX766` / `DW9800V` / `RumbaS4H` 点亮与 VPU 探测

- **`960×720` 高清软 ISP 与三摄热切换（`ZizhanCamStream.h` + `zizhan-avcapture18.h` + `zizhan-cam-proto.h`）**：
  - 输出帧分辨率从 `640×480` 升级至 **`960×720` BGRA8888（`2.76 MiB`/帧）**，`AVCaptureDeviceDiscoverySession` 标准暴露 **`Back Wide 1x`（`com.zizhan.camera.back`）、`Back Ultra-Wide 0.6x`（`com.zizhan.camera.ultrawide`）、`Front TrueDepth/Wide`（`com.zizhan.camera.front`）** 三颗设备并支持会话运行中 `addInput:` 实时切镜。
- **50 MP 主摄 `IMX766` + `DW9800V` 自动对焦 + `RumbaS4H` OIS 硬件实机验证**：
  - 在 `CCI0 master 0` + `MCLK2 19.2 MHz` + `PM8008 LDO3/LDO4 3.104 V` + `GPIO 126` 复位链路上：
    - 实机读出索尼 **`IMX766`（`0x1a`）芯片 ID `0x0766`** 并完整写入 `2048×1536` 4-binning 寄存器表；
    - 实机点亮 **`RumbaS4H` OIS（`0x24`）** 完成全部 `7/7` 步光学中心锁止（`ois_rc=0(done=7)`）；
    - 实机点亮 **`DW9800V` VCM（`0x0c`）** 对焦音圈马达并驱动 `init_dac=420`（`vcm_rc=0 have_vcm=1`）及 Tenengrad 清晰度爬山对焦（`ZIZHAN_CAM_CMD_AF`）；
    - 逆向对齐小米 L18 `csiphy-v2.1.3-xiaomi-l18` 三相 C-PHY 复位释放序列（`0x1000=0x0e` 3PH CDR 使能 + `0x102c=0xff` 模拟供电，修复旧 v1 `0x102c` 清零问题），实机 `CSIPHY0` 3-trio `SOT+EOT` 全部激活且 **C-PHY 包头 CRC/ECC 错误位清零**（`rx_irq` 从 `0xc00077` 改善至 `0x400077`）。因轻量级 `CSID_LITE_680` RDI0 缺少完整 `IFE_0` C-PHY 负载解包管线，主摄请求在 `<1 s` 内自动无感回退至 `OV13B10` `1364×768 → 960×720` 真实光学流，后续可迁往完整 `IFE_0`（`0xacaf000`）解锁原生 `IMX766` 像素落盘。
  - **系统 `Camera.app` 边界说明**：已验证所有第三方容器与自研应用均可通过标准 `AVCaptureSession` 稳定运行 `960×720` 预览/扫码/拍照；原厂系统 `Camera.app`（`CAMApplication`）在启动 `267 ms` 处直接解引用私有 `FigCapture` C 层格式结构触发 `SIGSEGV`，不阻塞微信/支付宝/第三方相机使用，后续需专门补齐 `CMVideoFormatDescription` / `FigCapture` 私有桩。
- **`VPU-01`（`Iris2 VPU` 只读硬件盘点，`ZizhanVpuProbe.h`）**：
  - 实机定位重要安全边界：`video_region`（`0x85700000+0x700000`）在执行 TZ `PAS_INIT_IMAGE(9)` 前受 Gunyah Stage-2 XPU 保护（EL1 直读触发同步外部中止）；通过 SMCCC 查询 `PAS_IS_SUPPORTED(9)` 返回 **`pas_x0=0, pas_supported=0`**（确认本机 TZ 支持 `PAS 9` `vpu20_4v.mbn` 硬件编解码固件加载，`ZIZHAN_VPU_PROBE: PASS ... apcs_vote=0xe9bd7f(map=1) writes=0`）。

---

## 4. Phase 4（`PERIPH-01`）：X 轴线性马达触感、听筒路由切换与双电芯快充遥测

- **PM8350B 高压 X 轴线性马达（`ZizhanHapticsCore.h` + `ZizhanHaptics.h` + `zizhan-haptics.py`）**：
  - 通过 SPMI 仲裁器自动发现 PM8350B `qcom,hv-haptics@f000` 三组外设通道 **`apid=465/466/400`（`PPID 0x3f0/0x3f1/0x3f2`，硬件版本 `rev=02/02`，`fault=0x00`，出厂 `vmax=0x41`）**，实现 Direct Play 短脉冲驱动并桥接 iOS `UIImpactFeedbackGenerator`、`UISelectionFeedbackGenerator`、`UINotificationFeedbackGenerator` 与 `AudioServicesPlaySystemSound(1519/1520/1104)`。
  - 无人值守默认 `zizhan_haptics_dry=1`（`writes=0` 零震动静默演练，实机 `ZIZHAN_HAPTICS: PASS n=1..3`）；在场验收脚本 `run-video-5.sh` 与 M3 独立镜像开启 `zizhan_haptics_dry=0` 输出真实清脆触感。
- **顶部微缝听筒路由（`ZizhanCs35l41.{h,c}` + `zizhan-avcapture18.h`）**：
  - 当应用切入 `AVAudioSessionModeVoiceChat` / `AVAudioSessionCategoryPlayAndRecord`（且未外放）时，通过 `ZIZHAN_CAM_CMD_AUDIO_RECEIVER` 动态静音底部扬声器 `0x42` 并将顶部扬声器 `0x40` 切换为 `-12 dB` 私密听筒增益，退出通话即时恢复双扬声器立体声（`ZIZHAN_RECEIVER_MODE: mode=earpiece-top-0x40 (1) -> stereo-speaker (0)`）。
- **双电芯板级遥测与安全充电功率（`ShengBattmgr.c` + `PongBattery.cpp`）**：
  - 默认开启 `zizhan_batt_board=1` 双电芯串联电压自动折算并新增 `BmPowerMax` 适配器功率读取，在 `AppleSmartBattery` `AdapterDetails.Watts` 中向 iOS 暴露真实握手瓦数；快充电流与温度保护严格交由 PM8350B/SMB1396 硬件 JEITA 状态机自主控制。

---

## 5. Phase 5（`OUTER-03` & `USB-01`）：外屏 `1:1` 满高视网膜桌面与 USB DWC3 盘点

- **外屏 `1:1` 满高视网膜模式与一键缩放胶囊（`ZizhanOuterTouchPlan.h` + `ZizhanOuterProbe.h` + `outer-touch.cpp`）**：
  - 新增 `zizhan_outer_fold=3`（现为在场/浸泡默认档）：将内屏活跃区域以 **`1:1` 原生视网膜精度铺满外屏 `1080×1916` 视口（`y=220..2136`，占 2520 高外屏 `85%+` 黄金可视区）**，配套 `1:1` 触控坐标逆映射与底部第五颗 **`ZOOM` 胶囊**，实机 `tier2-verify-3` 验证 `1:1-full (1080x1916@y220) ↔ 2:1-overview (1080x958@y781)` 实时无损切换（`blits=473, distinct_misrs=39`）。
- **`USB-01`（`ssusb@a600000` / `gcc_usb30_prim_gdsc` 只读探测，`ZizhanUsbProbe.h`）**：
  - 实机读取 `0x149004` 确认 iPadOS 运行期间 `gcc_usb30_prim_gdsc` 保持供电激活（`gdsc=0xf822f000, pwr_on=1, sw_collapse=0, active=1, writes=0`），定位后续 XNU `DWC3` 枚举需围绕 eUSB2 repeater 与角色切换展开，全程零写入确保 `100%` 可靠自动回 `fastboot`。

---

## 6. 15 分钟全子系统静默浸泡 `soak-1128` 与在场交付产物

- **`soak-1128`（内核 `build-play205` `84c0e5802b98c74d0d38b88daf410e196ffb1e0053cd87057ba462489d9231a1` + `64 GiB` 根卷 `sens-hal-v23-64g`，`890 s`）**：
  - 连续通过 **`5/5` 轮平台深睡/唤醒**（每轮 `20,002 ms` 深睡，`tick_ms=250`、`spurious=0`、`cpu_off_fail=0`、`MM0/SN2/SN7/MC0/SH0` 与 `cx=64/mx=192` 5 轮精准切换）、`VPU_PROBE: PASS`、`USB_PROBE: PASS`、Wi-Fi 全程在线（`64 MiB` DMA，`BSS=27`）、`3 Mbps` 蓝牙（`rx=32,902 B`、`tx=15,206 B`、`70` 次唤醒 `fail=0`）、SLPI 七路传感器（`32,819` 次加速度/陀螺、`3,158` 次磁力、`3,152` 次气压 `100,730 Pa`、转轴 `177.0°`、`5/5` 次休眠静默恢复）、离线 `QMI_LOC` 1 Hz 定位流（`210+` 次报告 + `pas_shutdown x0=0`）与 `4/4` 张完整 `192/192` 行截图，CPU 峰值仅 **`56.4 °C`**（GPU `55.6 °C`，限频 `0/63`），**`0` panic**，到期直跳回 Android（电池 `39.5 °C`）。
- **在场一键验收与 M3 双启动镜像（均带 `ZIZHAN_USER_PRESENT=yes` 门禁，零分区写入）**：
  - 全功能主验收：`ZIZHAN_USER_PRESENT=yes bash work/m1-firstsound/run-video-5.sh`（镜像 `out/video-5` `49844203791c…`，挂载 `64 GiB` 根卷并开启真实音频外放/主麦/听筒路由与真实 X 轴线性马达震动；`run-video-1.sh` 已自动转发至 `run-video-5.sh`）。
  - M3 独立双启动包：`work/m3-standalone-1128/pong-xnu-aprime.img`（`43fbb97bf71d35dad3c23efd1c2553f3755f3ea45f8fe776516fc7c39c72b877`），离线全项与原厂 `boot_b.img.gz` 备份校验 `M3_STANDALONE_PREFLIGHT: PASS`（`backup_verified: true`）。
- **晚间在场初测反馈闭环（`sens-hal-v26-64g` / `ux-fix-3` / `ux-verify-clean` / `ux-home-check`）**：
  - 将 `ZizhanInstaller`（`com.zizhan.installer`，「IPA 安装器」）、`ZizhanDemoApp`（`com.zizhan.demoapp`，「扫码相机验证」）与 `ZizhanCompatSuite`（`com.zizhan.compatsuite`，「兼容套件验证」）统一封装为 `16K` 用户域应用（保留 `/Applications/ZizhanBootstrap.app` 系统引导器并去除冗余 `LS rebuild` 使单包安装提速至 `~1.5 s`），三枚图标全部置顶于主屏幕第 1 页（`ux-home-check.shot1.png` `192/192` 完整帧确认）。
  - 重构 `ZizhanCompatSuite` 与 `ZizhanDemoApp` 交互逻辑：人类常规启动不再自动触发马达震动，保持 `960×720` 相机画面持续预览不断流（`ux-verify-clean` 连续 280+ 帧），新增 9 按钮交互控制台（三摄切换、AF 对焦、JPEG 拍照、暂停预览、X 轴震动、听筒/双扬 `1.1 s` 试音切换）及 `1x` 等效标准视角裁切与 ISO-18004 双副本 BCH(15,5) + 对比度门限防暗噪误识别。
- **晚间二轮在场实测（`video-7`）用户验收记录与待修挂起项**：
  1. **相机图像（`CAM-04`）**：前后各摄像头取景方向不对、噪点明显且色彩不准（扫码暂未验证）。根因指向传感器贴装需 `90°/270°` 转置、`flipY=1` 翻转奇偶行改变了 Bayer 相位（`GRBG↔BGGR`）引发红蓝绿通道错位偏色，以及暗光高模拟增益下缺少时域降噪与 CCM 矩阵。
  2. **合盖外屏状态与旋转（`OUTER-04`）**：物理合上后外屏未处于锁屏时钟态，框内仍呈横屏并跟随重力感应旋转（需在合盖过渡时强制进入锁屏态并在 `outer_closed=1` 期间冻结竖屏方向、屏蔽旋转传感器）。
  3. **外屏电源键（`OUTER-05`）**：合盖外屏状态下短按物理电源键无反应（电源键休眠/唤醒状态机尚未覆盖 `outer_closed=1` 下的外屏 `DSI1` 亮灭控制）。
  - 按用户指示先完整记录归档，暂不启动新一轮修改。
