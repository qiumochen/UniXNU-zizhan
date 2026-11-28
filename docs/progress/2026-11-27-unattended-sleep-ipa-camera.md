# 2026-11-27 无人值守 P0 专项攻坚：熄屏深度休眠、第三方 4K/16K 脱壳 IPA 免签安装器与系统级前后置相机（扫码 / 拍照 / 实时视频流）

本轮按用户授权的 24 小时无人值守 P0 攻坚计划，完整闭环三大头号工程硬骨头：**平台级熄屏深度休眠（`POWER-02`）**、**第三方 IPA 侧载方案调研与机内原生 `IPA 安装器`（`APP-02`）**、**系统级 `AVCaptureSession` 前后置双摄实时流 / 二维码识别 / JPEG 拍照（`CAM-02`）**。全程严守零分区刷写（仅 `fastboot boot` RAM 启动与 `/data` 镜像差分更新，保留 `sens-hal-v10/v7/v4/v3` 完整回退链）、无人值守 100% 物理静音（`pong_audio=0`、`zizhan_pulse_dry=1`）、MPSS 锁定 `operating_mode=3` 离线零射频发射与位置/网络隐私红线，`main` 分支保持 `dac504b` 未改动。

---

## 1. Track 1（P0）：平台级熄屏深度休眠（`IOPMrootDomain` + `IOCPUSleepKernel` + `ArmCpu` `PSCI CPU_OFF`）

- **架构与根因突破**：
  - 旧版待机仅关闭内屏背光并热插拔下线 `CPU 2..7`，而 `CPU 0..1`、XNU 调度滴答与 APSS 共享域保持活跃，纯电池待机底噪停留在 `-391 mA / 1685 mW`（早期亮屏转熄屏窗口约 `-571 mA / 2472 mW`）。
  - 新增 XNU 补丁 `patches/xnu-ipados18/0046-zizhan-platform-sleep.patch`、驱动 `drivers/ArmCpu/ArmCpuSleep.h`、`drivers/ArmDTPlatform/ZizhanSleepPlan.h` 与 `drivers/ArmDTPlatform/ZizhanSleep.h`，打通原生 `powerd → IOPMrootDomain → IOCPUSleepKernel → ArmCpu::halt` 全链路平台深睡：
    1. **多核下电与主核掩码 `WFI`**：休眠前由 `ZizhanCpuOffline` 统一归还 `CPU 2..7` 控制权，交由 `IOCPUSleepKernel` 将全部从核 **`CPU 1..7` 执行 `REASON_SYSTEM` `PSCI CPU_OFF`**（`cpu_off_fail=0`），主核 `CPU 0` 在 `DAIFSet` 掩码态进入 `WFI` 循环，每 `10 s` 轻触一次 Gunyah 看门狗喂狗（`ping_from_sleep`），彻底冻结系统空闲唤醒风暴。
    2. **多源硬件唤醒**：支持 **PMK8350 电源键**（SPMI PON 边沿检测）、**霍尔传感器 GPIO 62 合盖/展开**（`TLMM summary INTID 240` 双极性翻转唤醒）以及 **`CNTV` 架构定时器**（闹钟 / 自动复位守卫）精确唤醒，并在唤醒阶段完整恢复全部 8 核与 GIC/TLMM 中断掩码。
    3. **总线与频率压降 + 活跃业务防误睡守卫**：`PongCpuPolicy.h` 在深睡期间将三簇频率与 L3 压至最低档（`307 / 633 / 787 MHz`，L3 index `0`），`ZizhanStandbyRpmh.cpp` 同步下调 `cx=64`、`mx=192` 与 `MM0/SN2/MC0/SH0` 地板带宽投票；`ZizhanAudioService` 与 `ZizhanCamStream` 暴露 `sleep_busy` 守卫，确保正在录音/播音或开相机取景时绝不中途断流休眠。
- **纯电池实机电流 A/B 与多轮唤醒压测（`build-play203` / `build-play204`）**：
  - **`sleep-ab-batt-1`（`ZIZHAN_USB_OFF_AT=15` 断 VBUS 纯电池）**：连续深度休眠 `184,616 ms`，PM8350B 硬件库仑计实测放电降至 **`-324 mA`（`4327 mV`，`1401 mW`）**。
  - **`sleep-ab-batt-2`（`build-play204` `772f1e8a3746…`，`ZIZHAN_USB_OFF_AT=15` 同轮纯电池对照）**：同轮前段传统熄屏待机 12 次采样均值 **`-571 mA / 2472 mW`**（区间 `-556..-587 mA`）；随后原生 `powerd` 自动触发 `IOPMrootDomain` 连续深睡 **`149,517 ms`**（`wdt_pings=15`、`spurious=0`、`cpu_off_fail=0`），唤醒瞬态实测 **`-323 mA`（`4349 mV`，`1405 mW`）**——较同轮传统熄屏直降 **`43.4%`（省电 `1067 mW`）**，较历史长稳态熄屏底噪（`-391 mA / 1685 mW`）再降 **`17.4%`（省电 `280 mW`）**，两轮纯电池深睡读数高度一致（`-324 mA` vs `-323 mA`）。
  - **可靠性压测**：`sleep-smoke-1`（`3/3` 轮 PASS）、`sleep-stress-1`（**连续 `10/10` 轮快速深睡/唤醒 PASS**，`entries=10`、`cpu_off_fail=0`、Wi-Fi 不断连）及 `soak-1127`（`5/5` 轮 PASS），累计 **`19+` 次平台深睡唤醒 `0` panic**。

---

## 2. Track 2（P0）：第三方 IPA 安装方案调研（巨魔 / 签名 / 同项目机型对比）与原生 `ZizhanInstaller.app`

### 2.1 三大核心问题调研结论

| 维度 | 核查事实与工程结论 |
| --- | --- |
| **同项目其他机型怎么做？**<br>（核查 `UniXNU-pong` 与 `UniXNU-sheng` 源码） | 1. **内核层**：`UniXNU-pong`（Nothing Phone (2)）与 `UniXNU-sheng`（Xiaomi Pad 6S Pro）均启用 `cs_enforcement_disable=1 cs_library_val_enable=0 amfi_get_out_of_my_way=1` 及 `native-4k-signature-hash-cache.patch`（允许 4 KiB 页粒度 Ad-hoc 代码目录并在 `exec` 时将 16 KiB 代码页重哈希到 4 KiB 缓存）。<br>2. **用户态短板**：兄弟机型在 `userland/ui_compat18.c` 中仅重定向了 `lsd` 单用户容器路径以支撑镜像预置的 `/Applications` 系统应用，**均未适配 `installd`（`MICodeSigningVerifier` + `libmis.dylib`）第三方 `.ipa` 校验与安装链路**——直接调用 `installd` 安装标准 16 KiB 页签名无描述文件 `.ipa` 会因签名类型与 `application-identifier` 缺失被拒。`UniXNU-zizhan` 是同系首个完整打通机内 4K/16K `.ipa` 侧载安装的机型。 |
| **能否巨魔（TrollStore）？** | 1. **原版 TrollStore 不可直接用（也无需使用）**：原版巨魔依赖 Apple `CoreTrust` CMS 证书链解析漏洞（CVE-2022-26766 / CVE-2023-41991，仅存在于 iOS 14.0–16.6.1 与 17.0），iPadOS 18.7.10（`22H374`）早已修复该漏洞。<br>2. **本机已实现等价原生免签安装器（`IPA 安装器`）**：巨魔的唯一目的是在「未越狱原版内核」上绕过 AMFI；而在 UniXNU 中**内核由我们完全掌控**。我们通过 `installd` `MICodeSigningVerifier` 4K/16K 桥接 + 系统应用 `/Applications/ZizhanInstaller.app` 实现了与巨魔完全一致的体验：**永久安装不过期、无 7 天重签限制、无需 Apple ID 或电脑助手、局域网浏览器拖拽 `.ipa` 一键直装上桌面**。 |
| **是否需要签名？能否装未脱壳包？** | 1. **免开发者证书 / 免 Provisioning Profile**：任何已脱壳的标准第三方 `.ipa`（无论原包是 App Store 砸壳常见的 **`16 KiB` 页签名** `pageSize=14`、`4 KiB` 页签名 `pageSize=12`、还是仅用 `codesign -f -s -` 本地伪签的裸包）均可直接安装；`installd` 钩子自动完成 Mach-O SHA-256 结构校验并注入 `kMISValidationInfoSignerType=6`（映射 22H374 `MICodeSignerTypeAdHoc=5`）与 `ZIZHAN0000.<bundle-id>` 容器标识。<br>2. **唯一硬边界：必须已脱壳（`cryptid == 0`）**：未脱壳的 App Store 原包（`LC_ENCRYPTION_INFO_64.cryptid == 1`）代码段由 Apple A/M 系列芯片硬件 FairPlay GID 引擎加密，骁龙 8+ Gen 1 物理上不存在 Apple 硬件解密密钥，安装器会在预检阶段明确拦截并提示更换脱壳包。 |

### 2.2 落地实现与实机闭环（`ipa-cam-7..11`、`sens-hal-v22`）

- **新增组件**：
  - `userland/zizhan-ipa-install18.h` + `userland/ui_compat18.c`：纯 C 4K/16K Mach-O / Fat 二进制与 `LC_CODE_signature` / `LC_ENCRYPTION_INFO_64` 解析器（`tools/zizhan/tests/ipa-install18.py` ASan/UBSan 单测通过）及 `installd` `MICodeSigningVerifier` 方法调配。
  - `userland/installer/{ZizhanInstaller.m, ZizhanDemoApp.m, installer-entitlements.plist, build-apps.sh}`：内置局域网 HTTP 上传服务（`http://<手机Wi-Fi-IP>:8088`，支持电脑/另一台手机浏览器直接选择 `.ipa` 上传安装）与本地 `/var/mobile/Documents/IPAs`、`/pong/ipas` 一键扫描安装，调用系统 `libarchive.2.dylib` 解包、`MobileInstallation` `MIInstallerClient` XPC 落盘容器、`_LSContextInit` 刷新 SpringBoard 图标。
- **实机验证结果**：
  - 加密包负向拦截：`FairPlayEncryptedSample.ipa`（`cryptid=1`）精准拒绝（`REJECT ... rc=-5 cryptid=1`，`AUTOTEST_ENCRYPTED_GUARD expected_reject=1`）。
  - 16 KiB 第三方裸包安装与容器运行：`ZizhanDemoApp-16k.ipa`（`page_bytes=16384`，无 mobileprovision）通过 `MIInstallerClient` 安装成功（`ok=1 err=none`），SpringBoard 主屏与 Dock 正常展示「IPA 安装器」与「扫码相机验证」双图标，并从 `/private/var/containers/Bundle/Application/<UUID>/ZizhanDemoApp.app` 沙盒路径正常冷启动渲染。

---

## 3. Track 3（P0）：系统级相机 `AVCaptureSession` 可用（扫码支付 / 拍照 / 前后摄切换 / 视频通话底层）

- **内核按需双摄 MIPI CSI-2 实时流服务（`drivers/PongPower/ZizhanCamStream.h` + `ZizhanImx596Regs.h` + `0047` sysctl）**：
  - **后置超广角 `OV13B10`**：`CSIPHY1`（`0xace6000`）+ `CCI0 m1` `0x36`（芯片 ID `0x560d42`），4-lane `1120 Mbps` `1364×768` RAW10 → `VFE-lite0 WM0` 双缓冲重装填 → `640×480` BGRA8888。
  - **前置自拍镜头 `IMX596` 首次点亮**：`CSIPHY3`（`0xacea000`，`cam_cc_csi3phytimer` + `csiphy3`）+ `MCLK5` `19.2 MHz` + `PM8008 LDO5 1.8 V / LDO2 1.152 V` + `GPIO 97/105/127` + `CCI0 m1` `0x10`（实机读出芯片 ID `0x0596`），4-lane `1920×1080` RAW10 → `640×480` 自然镜像 BGRA8888（若极端工况未出帧自动平滑回退至镜像 `OV13B10`）。
  - **实时闭环 AE/AWB 软 ISP 与 3.5 秒崩溃安全租约**：`userland/zizhan-cam-proto.h` 实现纯整数黑电平扣除、灰度世界白平衡（AWB）、自动对比度与 `gamma 2.0` 曲线及阻尼自动曝光（AE，`tools/zizhan/tests/avcapture18.py` ASan/UBSan 通过）；通过 `kern.zizhan_cam_ctrl` 与 `kern.zizhan_cam_frame` 维持 `3.5 s` 租约，应用关闭或退出后自动逆序下电全部传感器/时钟/LDO/SMMU（`rails_off=1`，空闲 `0 mW`）。
- **用户态 `AVCaptureSession` 全栈桥接与 `CoreImage` 修复（`userland/zizhan-avcapture18.h` + `0047` 容器注入）**：
  - `0047-zizhan-app-inject-and-cam-sysctl.patch` 在 `zizhan_inject_apps=1` 时将 `/pong/libpong-ui18.dylib` 自动注入所有 `/var/containers/Bundle/Application/*.app` 第三方容器应用及系统 `Camera`、`FaceTime`、`MobileSafari`。
  - 完整接管 `AVCaptureDevice`（前后双摄枚举与权限免弹窗授权）、`AVCaptureDeviceInput`、`AVCaptureSession`、`PongZizhanPreviewLayer18 (: CALayer)`（规避 `AVCaptureVideoPreviewLayer` 空 `_internal` 指针崩溃）、`AVCaptureVideoDataOutput`（`CVPixelBuffer` + `CMSampleBuffer` 实时视频帧回调，供第三方视频通话/自定义扫码器消费）、`AVCapturePhotoOutput`（`ImageIO` 硬件帧编码 JPEG）与 `AVCaptureMetadataOutput`（`< 0.3 ms` 纯整数 ISO/IEC 18004 V1..V6 扫码快路径 + `CoreImage` `CI::SoftwareContext` 兜底）。
  - 逆向修复 22H374 `CoreImage` 在无 Apple Metal/EAGL 驱动下忽略 `kCIContextUseSoftwareRenderer` 触发 `CI::GLContext::init()` 空指针崩溃的问题，统一路由至 `+[CIContext internalSWContextWithOptions:]` + `-[CIContext _initWithInternalRepresentation:]`。
- **实机验证（`ipa-cam-11` 与 `soak-1127`）**：在已安装的 16K 第三方容器应用 `ZizhanDemoApp.app` 中，实机连续跑通后置 `OV13B10` 实时画面渲染、识别出二维码 `https://unixnu.org/zizhan-qr-ok`、导出后置 JPEG 照片、一键热切换至前置 `IMX596`（`1920×1080` `CSIPHY3`）实时取景并导出前置 JPEG 照片，结束后会话干净停止且双路 `rails_off=1`，全程 `0` 崩溃。

---

## 4. 15 分钟全子系统静默浸泡 `soak-1127` 与交付产物

- **`soak-1127`（最终内核 `build-play204` `772f1e8a3746c51b7c4bb1ef955d726da64bf1b72a113ef4987ae6c30e8a3334` + 已安装整盘回读校验 `sens-hal-v22` `UI e8a257433d2f…`，`890 s`）**：全开完成 **`5/5` 轮平台级深度休眠/唤醒**（每轮 `20,002 ms` 深睡，`wdt_pings=10`、`spurious=0`、`cpu_off_fail=0`）、Wi-Fi 全程不断连（DHCP 已联网，`8,605` 帧）、`3 Mbps` 蓝牙（`rx=64,517 B`、`tx=15,846 B`、`106` 次唤醒 `fail=0`）、7 路 SLPI 传感器（`32,961` 次加速度/陀螺、`3,172` 次磁力、`3,166` 次气压 `100,054 Pa`、转轴 `177.0°`、`5/5` 次休眠静默恢复）、`MPSS + QMI_LOC` 1 Hz 定位流（`310+` 次报告 + `pas_shutdown x0=0`）、唤醒后自动冷启动已安装的 16K 第三方应用 `ZizhanDemoApp`（后置 `OV13B10` `22/22` 帧 `100%` 出流、识别出 `https://unixnu.org/zizhan-qr-ok` 并完成 `rails_off=1` 断电回收）及 `4/4` 张完整 `160/160` 行锁屏截图，CPU 峰值 **`60.5 °C`**（GPU `51.7 °C`，限频 `0/69`），**`0` panic**，到期自动回 Android（电池温度 **`35.3 °C`**）。
- **在场一键验收与独立双启动产物（全部带 `ZIZHAN_USER_PRESENT=yes` 门禁，零分区写入）**：
  - 全功能主验收脚本：`ZIZHAN_USER_PRESENT=yes bash work/m1-firstsound/run-video-4.sh`（镜像 `out/video-4` `ff556cc4ead6…`，默认开启平台深度休眠、`IPA 安装器`、前后双摄真实光学 `AVCaptureSession`、Wi-Fi、增响双扬声器 + 主麦录音、外屏上滑、双开关电池页与 1 Hz GPS；`run-video-1.sh` 已同步指向 `run-video-4.sh`）。
  - M3 独立双启动包：已基于 `build-play204` 刷新 `work/m3-standalone-1127/pong-xnu-aprime.img`（`44f555f190d1263625630069acf5d286b36408928a2316f815a01eeaf89e6d5a`），离线全项与原厂 `boot_b` 备份校验 `M3_STANDALONE_PREFLIGHT: PASS`。
