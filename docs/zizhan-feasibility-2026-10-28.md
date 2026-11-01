# 小米 MIX Fold 2（zizhan）运行 iOS 可行性调研

调研日期：2026-10-28。结论：**有希望，而且起点比 K90 好**。同 SoC（SM8475）已有机型跑到 iOS／iPadOS 桌面，并实现了触摸和 Adreno 730 GPU 渲染。主要缺口在 MIX Fold 2 独有的部分：bootloader 解锁、双屏 command-mode 面板、ST FTS 触摸芯片，这几样目前没有现成实现。

## 1. 目标机硬件（来源：LineageOS 官方 zizhan 设备树）

| 项 | zizhan 实况 | 来源 |
|---|---|---|
| SoC | SM8475（骁龙 8+ Gen 1，DT 名 `cape`），msm-id 530/531/540 | `zizhan-sde-display-cape-mtp-overlay.dts` |
| GPU | Adreno 730（`qcom,adreno-gpu-gen7-4-0`，chipid 0x7030002，与 K60/Nothing Phone 2 同） | K60 实机 DT，同 SoC |
| 内屏 | `dsi_l18_38_0c_0a_prim_dsc_cmd`，DSI0，DSC command mode，1914×2160 | display dtsi；触摸坐标 19140×21600 |
| 外屏 | `dsi_l18_38_0c_0a_sec_dsc_cmd`，DSI1，DSC command mode，1080×2520 | display dtsi；触摸坐标 10800×25200 |
| 内屏触摸 | ST FTS（`st,spi-pri`），QUP SE4 SPI，IRQ GPIO21，RST GPIO20，AVDD=L7C，DVDD=L2C（RPMh） | `zizhan-sm8475.dtsi` L425 起 |
| 外屏触摸 | ST FTS（`st,spi`），QUP SE19 SPI，IRQ GPIO119，RST GPIO52，供电来自外置 LDO WL2866D（SE1 I2C 0x28），固件 `st_fts_l18_sec.ftb` | `zizhan-sm8475.dtsi` L376、L497 起 |
| 显示切换 | Android 按折叠状态在两块屏之间切换默认显示（display 129 内／130 外） | `display_layout_configuration.xml`、`input-port-associations.xml` |
| 系统版本 | LineageOS 24（Android 16）官方支持；专有固件基于 HyperOS `OS3.0.9.0.VLRCNXM` | `proprietary-firmware.txt` |
| 内核源码 | `LineageOS/android_kernel_xiaomi_sm8450` + `-devicetrees` + `-modules` | `lineage.dependencies` |

## 2. 现有三条技术路线

| 路线 | 代表仓库 | 启动方式 | 与 zizhan 最接近的成果 |
|---|---|---|---|
| A. HackiOS（oosexgang） | `oosexgang/hackios-xnu-kernel`、`iola1999/hackios-redmi-k90-annibale` | Mu-Silicium UEFI → xnuloader → XNU 8796 → iOS 16.5 | K90（SM8750）：GPU 加速桌面、触摸、按键；同 SoC 无实机成果 |
| B. ios-sweet（dipa-studio） | `dipa-studio/ios-sweet`、`XTOM0706/ios-sweet-k60`、`NicholasXiaoBai/mayfly-hackios` | ABL `fastboot boot` 裸机 shim → XNU → iOS 16.7.16 | **K60（SM8475）10-28 到锁屏**，触摸卡在读 RSC 整机死，GPU 停在 GMU 固件加载；小米 12S（SM8475）XNU 早期启动 |
| C. UniXNU | `UniXNU/UniXNU`、`Chengdong-Yu/UniXNU-pong`、`akshaynexus/iOSPlus10` | Mu-Silicium UEFI → DarwinBoot → XNU（Darwin 22/24/25） | **Nothing Phone 2（SM8475）**：iPadOS 18.7.10 桌面、壁纸、Goodix 触摸、A730 + Turnip + Metal 渲染、原生 UFS 可写 Data、按键事件已到达 SpringBoard；OnePlus 10 Pro（SM8450）进行中；Surface Duo 2（双屏）进行中 |

UEFI 前提已具备：Mu-Silicium 主线的 `Silicon/Qualcomm/PalimaPkg` 覆盖 SM8475（`SOC_TYPE 0`），已有 `Nothing/pongPkg`、`Xiaomi/marblePkg` 两个设备包可照抄。全网没有找到 zizhan 的 UEFI 或 iOS 移植。

## 3. 推荐路线：UniXNU + Pong 的 SM8475 层

- 同 SoC：CPU 拓扑（1+3+4）、GIC、PSCI、GPUCC/GMU 寄存器、SMMU、A730 固件流程都可以直接复用。K60 的 GPU 笔记逐条核对过，Pong 的 Cape GPUCC 时钟序列与 K60 实机 DT 一致。
- iPadOS 适合内屏：Pong 用的是 iPad7,11（10.2 英寸，2160×1620）的 iPadOS 18 用户态，zizhan 内屏 2160×1914 的比例比手机屏接近得多。K60 的教训是画布要按面板原生分辨率上报，不能伪装成别的 iPhone 几何。
- 构建链完整：board.toml 四层配置（ARM64／SM8475／OS／设备）、GitHub Actions macOS runner 编内核、固件压缩包注入。zizhan 可以先 fork `UniXNU-pong`，把设备层换掉。
- 路线 A 可作备选：K90 工作区的文档、备份和恢复流程规范，适合照搬操作纪律，但它与 zizhan 不同 SoC，需要自己补 SM8475 的平台层。

## 4. MIX Fold 2 独有的风险

| # | 风险 | 严重度 | 说明与对策 |
|---|---|---|---|
| 1 | bootloader 解锁 | 阻断 | 所有路线都要求已解锁。小米国行 HyperOS 解锁需通过社区申请，限制较严。**先确认手中机器是否已解锁** |
| 2 | 双屏 + command-mode 面板 | 高 | 显示走 bootloader／UEFI 留下的 framebuffer 交接，只有被点亮的那块屏可用。command mode 需要 MDP autorefresh 或手动 kickoff（K90 也是 cmd mode，已解决，可参考）。首轮只做一块屏，并实测 ABL／UEFI 在折叠／展开时点亮哪块屏；运行时折叠切屏属于远期目标 |
| 3 | ST FTS 触摸 | 高 | 两个生态都没有 ST FingerTip 驱动（现有：Goodix、Novatek、Synaptics S3910、FocalTech）。需要参考小米内核源码里的 fts 驱动新写一份 SPI 驱动。QUP GENI SPI 层可复用（`UniXNU/QcomGeniSpi`、K60 移植） |
| 4 | 触摸供电 | 中 | 内屏 DVDD=L2C 与 K60 的 IOVDD 是同一路 RPMh LDO，K60 卡在 RSC 访问；Pong 的触摸能用，先看它的供电方式（可能由 UEFI 预先打开）。外屏另需驱动 WL2866D I2C LDO，或由 UEFI 预先打开 |
| 5 | Wi-Fi／音频／相机／铰链传感器 | 低（首轮不做） | Pong 的 QCA6490 Wi-Fi 和 AudioReach 音频都尚未确认可用；zizhan 的 Wi-Fi 芯片未核对 |

## 5. 建议的推进顺序

1. 确认设备状态：是否已解锁、当前 HyperOS／LineageOS 版本、能否 root。完整备份分区（参照 K90 README 第 5 节）。
2. 只读采集：`/sys/firmware/fdt`、vendor_boot + dtbo、分区表、`/vendor/firmware`（A730 gmu/sqe/zap、触摸固件）。
3. UEFI：在 Mu-Silicium 中新建 `Xiaomi/zizhanPkg`（基于 pongPkg／PalimaPkg），生成 MemoryMap／ConfigurationMap，`fastboot boot` 验证能看到 UEFI 菜单，并确认是哪块屏亮。
4. 设备仓：fork `UniXNU-pong` → `UniXNU-zizhan`，替换 DTS、display、memory、power 配置，先关掉 GPU、触摸、Wi-Fi，以 CPU 软件渲染为目标跑到桌面。
5. 打开 A730（复用 PongAdreno + 本机固件）。
6. 写 ST FTS 触摸驱动（先做内屏）。
7. 远期：外屏、折叠切屏、背光、电池、音频、Wi-Fi。

构建条件：XNU 需要 macOS + Xcode（Mac mini 或 GitHub Actions macOS runner）；IPSW 下载和解包约 8 GB，本机磁盘不够，放到 Mac mini 上做。

参考节奏：K90 从 10-23 建工作区到 10-26 GPU 桌面，大约 4 天高强度工作；K60 约一周到锁屏。zizhan 有同 SoC 的 Pong 可以复用，单屏、软件渲染的桌面预计约 1 周可以到；加上触摸，预计 2～3 周，主要取决于 ST FTS 驱动和供电。以上都假设设备已解锁。

## 6. 参考链接

- K90 工作区：https://github.com/iola1999/hackios-redmi-k90-annibale
- HackiOS 上游：https://github.com/oosexgang/hackios-xnu-kernel 、https://github.com/oosexgang/hackios-xnu-userland
- UniXNU：https://github.com/UniXNU/UniXNU （文档 https://unixnu.com/porting/）
- Nothing Phone 2（SM8475）：https://github.com/Chengdong-Yu/UniXNU-pong 、https://github.com/Chengdong-Yu/uefi-pong
- Redmi K60（SM8475）：https://github.com/XTOM0706/ios-sweet-k60
- 小米 12S（SM8475）：https://github.com/NicholasXiaoBai/mayfly-hackios
- ios-sweet：https://github.com/dipa-studio/ios-sweet
- OnePlus 10 Pro（SM8450）：https://github.com/akshaynexus/iOSPlus10
- Surface Duo 2（双屏）：https://github.com/none24smu/UniXNU-zeta
- Mu-Silicium：https://github.com/Project-Silicium/Mu-Silicium （`Platforms/Nothing/pongPkg`、`Silicon/Qualcomm/PalimaPkg`）
- zizhan 设备树：https://github.com/LineageOS/android_device_xiaomi_zizhan 、https://github.com/LineageOS/android_kernel_xiaomi_sm8450-devicetrees （`qcom/zizhan-sm8475.dtsi`）
- 生态索引：https://github.com/spectre-oss/awesome-hackios （目前为空仓库）

注意：iOS 的许可只限于 Apple 硬件。以上项目都按研究用途进行，IPSW 和厂商固件由使用者从自己的来源获取，不随仓库分发。
