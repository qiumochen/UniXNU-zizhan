# 兄弟项目适配对照：GPU、Wi-Fi、内外屏（2026-11-04）

本次以公开主分支、PR、设备记录和本机已取回的日志为准。外部真机结果是项目作者记录，未在本机复跑；本机以 `5edee45` 的 g287 记录为 Wi-Fi 基线。接续 [11-02 GPU 调研](2026-11-02-sibling-gpu-update.md)；旧设备目录的摘要滞后于设备仓库及本地记录。

实施接续：[原生 Wi-Fi g292–g295](../progress/2026-11-04-wifi-native-repair.md) 已修正原生绑定、等待期限与 manager 端口，但跨频段 RDDM 和 Settings 入网尚未通过；按用户授权转入 [GPU g296–g298](../progress/2026-11-04-displaylink-device.md)，实际 UIKit 动画类名修正已获真机效果。下文 g287 的开关与验证边界是调研时的历史基线，不代表当前全部实施状态。

## 哪些项目值得跟

| 项目 | 已有真机证据 / 主要限制 | 对 zizhan 最有用的部分 |
| --- | --- | --- |
| [Nothing Phone (2) / Pong](https://github.com/Chengdong-Yu/UniXNU-pong/pull/11) | **同 SM8475、A730、QCA6490、22H374**。桌面 GPU、壁纸、亮度、Wi-Fi 扫描和连接有设备记录；120 Hz 是请求值，仍需核对实际刷新；壁纸仍可排队 41–267 ms。11-03 [PR #12](https://github.com/Chengdong-Yu/UniXNU-pong/pull/12) 拆出驱动仓库。 | 首选对照：`pong-wifi` 的 Apple80211 桥、`pong-power` 的亮度服务、`pong-adreno` / `pong-adreno-metal` 的 ABI1、fence/IOSurface 所有权和缓存批处理。 |
| [小米平板 6S Pro / sheng](https://unixnu.com/devices/sheng) | SM8550 / A740，原生 XNU GPU 内核驱动；上层用 Metal → metal2vulkan → Turnip 合成，按进程接入。WCN7850 的 Wi-Fi 开关、扫描、WPA2、DHCP、流量和 join 回报已有设备记录。 | 同框架的 GPU trace、BO 生命周期、调度与 Wi-Fi 端到端验收。参考接口及生命周期；A740 的双环/抢占与 ath12k 硬件路径不整套移植。 |
| [Redmi K90 / annibale](https://github.com/iola1999/hackios-redmi-k90-annibale/blob/d0cd74d49d7d2ccdb00cb0fad565feaf7ad487f5/docs/progress/2026-11-01-wifi-ath12k-driver.md) | iOS 26，WCN7850 / ath12k。wd28 DHCP 有地址、NSURLSession -1009；wd31 设置页加入后 configd 自己跑 DHCP，主接口、DNS、HTTP/HTTPS 都成立。5 GHz 首次握手偶发失败曾出现，wd32 未复现。 | 最贴近 g287 的系统服务对照；参考由 wifid 驱动加入、InterfaceNamer 建服务的时序。K90 的 Apple80211 ABI 和驱动是异版本参考。 |
| [POCO X5 Pro / HackiOS redwood](https://github.com/oosexgang/hakios-xnu-kernel/commit/7caa4576e6e1be6d63f963b106b73480d6539a9b)、[Metal CA](https://github.com/oosexgang/hakios-xnu-kernel/commit/f693d9d) | GLES 与实验 Metal CA 是不同配置；同镜像的 display-link A/B，右滑 78→115 fps，设置滚动基本不变。Metal CA 也已跑通；应用 Metal 覆盖不能从 CA 的结果推断。 | 对付回弹 30 fps：只影响已定位的 UIKit 动画 display link；跟踪待机唤醒和无收益场景。 |
| [小米 17 Pro / data-a-live](https://github.com/yechenan/data-a-live/blob/6547194/PLAN.md) | A840、120 Hz、DPU 直读 GPU buffer；r2q 合成滑动约 113 fps，受控 alias 去掉了回拷。旧仓库归档并指向这个续库；项目记录未显示 Wi-Fi 或背屏完成。 | 用拷贝计数及 padding 像素测试证明零拷贝收益。主屏尺寸 2160×1916 本身满足 16×4，仍需测具体 IOSurface、stride 和附件，不能凭 A840 帧率预估 A730。 |
| [OnePlus 10 Pro / iOSPlus10](https://github.com/akshaynexus/iOSPlus10/blob/146e889/docs/gpu-comparison-2026-10-27.md)、[Pixel 4 / ios-flame](https://github.com/NWylynko/ios-flame) | 前者 SM8450 / 同 A730，报告 GPU 忙 12–26% 而 77% 的帧晚到；主项目已暂停该机。后者 A640，Metal 合成及 App 渲染、自带 Settings Wi-Fi 与 DHCP、USB 以太网/SSH 有真机记录，滑动约 20–26 fps。 | 前者交叉核对 A730 慢帧和 DSI 命令发送；后者参考固定逻辑画布与物理屏缩放、原生网络的用户可见验收，以及将调试 USB 和 Wi-Fi 分开取证。 |

## Wi-Fi：让系统接管网络服务

本机 [g287](../evidence/2026-11-04-g287-https.txt) 证实 QCA6490 扫描、WPA2、DHCP 后，基于当轮实时 ACK 补齐 en0 服务状态、路由和 DNS，原生 NSURLSession 得到 HTTP 200 / 577 字节。当前 GPU 配置默认关闭 Wi-Fi；实验镜像仍需 `pong_wifi_lease_repair=1` 才验证了这条公网路径。续租、跨 AP 重连、Settings 和实际 GIC MSI 到达均没有随这轮验证，详见 [11-04 记录](../progress/2026-11-04-wifi-usb.md)。

1. 先用同 OS 的 [Pong Apple80211 桥](https://github.com/Chengdong-Yu/pong-wifi) 对照扫描、当前网络、加入结果、Link 状态和接口命名。在现有 DHCP 回归之外，记录 en0 命名、`Setup:/Network/Service`、服务顺序、DHCP 开始、`State:/Network/Global/IPv4` 和 DNS 的先后。g281 已试会话服务并看到 ACK 含路由/DNS，但服务缺 Router；单纯再造一个 DHCP 服务没有足够证据。
2. 以 K90 wd28→wd31 为对照，实验 `InterfaceNamer` 的 `ConfigureNewInterfaces` 与 Settings 主导加入。Apple [InterfaceNamer 源码](https://github.com/apple-oss-distributions/configd/blob/main/Plugins/InterfaceNamer/ifnamer.c) 可解释配置路径，但不能代替 22H374 实测。保留 g287 作为可回退对照，分别验证 Wi-Fi 测试镜像的补偿关/开；不同时切换无线驱动、认证和 DHCP 策略。
3. 验收分开：首次 Settings 扫描/加入、2.4/5 GHz、同 AP 断连恢复、换 AP 后清除旧路由/DNS、续租、DNS + HTTPS、断链后状态回收；MSI 单独取证。若原生服务接手，确认测试轮不再依赖 ACK 补偿后再讨论默认配置。

Sheng 11-03 的 [Ath12kWifi 改动](https://github.com/UniXNU/Ath12kWifi/commit/7a70ef0) 与 [板级接入](https://github.com/UniXNU/UniXNU-sheng/commit/790232ebc176f1804d60b4f04f1f806ea4316800) 用 cfg80211/mac80211、用户态 wpa_supplicant、port authorization 后完成 join、明确取消与超时，以及有界 DMA 复用。短期借 join 生命周期与测试方式，继续用已验证的 QCA6490/ath11k、zizhan 实机固件与板级数据。

## GPU：先把首帧、稳态和像素正确性分开

本机内屏 DPU 直接翻页、零拷贝三缓冲已跑通。g155/g158 面板约 120.2 Hz，固定合成滑动 54.7/53.8 fps；异步 latch 版约 51.0/50.8 fps，没有默认开启。g245 的 72.0 fps 来自不同实验条件，不是 display-link 或 ASTC 的提升证据。参见 [面板与节拍](../progress/2026-11-01-120hz-and-cadence.md)、[ASTC 与 fairplayd](../progress/2026-11-02-gpu-astc-and-fairplayd.md)。

1. **先做同镜像 A/B。** `IOMFBDisplayRefresh` 已存在，display-link hook 已编进本机准备镜像。先确认设备日志确实加载该库，然后用 `pong_dl_full=0` 对默认，同手势、同温度，分别测触摸到首帧、回弹段、设置滚动、p50/p90 上屏间隔、迟于两拍的比例和静止唤醒。把 CA 交帧、编码、GPU 排队/执行、fence 和 DPU latch 分开。截图用独立轮次，参见 [主机构建记录](../progress/2026-11-03-displaylink-host.md)。
2. **跟踪 Pong 的资源交接。** [services167](https://github.com/Chengdong-Yu/UniXNU-pong/blob/e0ee6df/migration/ipados18/services167-frame-ownership-and-pac.md) 同为 A730 单环、ABI1：30,216 个导入范围合并成 1,793 次请求，多数交接约 0.3–0.4 ms；仍观察到壁纸排队。先量本机发布、完成后的 cache 维护和重复导入，再对照 method 20 的能力协商及 method 19 回退。保留实际完成 fence 与扫描缓冲持有条件。
3. **根据 trace 选优化。** 编码/编译占大头，试 sheng 的渲染线程优先级、共享着色器缓存及避免在交互时预热；等待和分配占大头，试有预算的 BO 缓存、锁外释放、分阶段 retire；单个 pass 贵，再做 GMEM/sysmem 和着色器 A/B。对照 [AdrenoMetal 11-02/03](https://github.com/UniXNU/AdrenoMetal/commits/main/)、[A740 驱动说明](https://unixnu.com/drivers/adreno740) 和 [Linux A730 catalog](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/msm/adreno/a6xx_catalog.c)。A740 环调度只借鉴测法；先用 A730 单环对照队列堵点。
4. **内容优先。** dada 风格的压缩纹理布局已是本机 WIP，不重复移植；核对 Weather、设置、Photos、多任务、壁纸的图像和提交。g264 消除了 Photos 零尺寸断言，不代表 ASTC 全覆盖或共享事件已修好。动态壁纸开放前，先测其排队、内存和对 backboardd 的影响。

## 显示：先调光、再单独点外屏、最后切换

内屏已经接管 INTF_1；外屏 1080×2520、外屏触控及 iPadOS 亮度仍未验证。已有外屏缓冲迹象不能证明外屏会扫描。板级与固件输入见 [porting.toml](../../porting.toml)。

1. **内屏调光。** [Pong 已确认亮度](https://github.com/Chengdong-Yu/UniXNU-pong)，本机有 [PongBacklight 参考实现](../../drivers/PongPower/README.backlight.md) 和 [DSI FIFO 发送路径](../../drivers/PongAdreno/PongAdrenoScanout.cpp)。先从本机 DT/原厂资料核对控制器、面板 DCS、字节序和范围；实机先安全回读，再试几个非零档位及恢复，成功后接入 CoreBrightness。与 60/120 Hz 切换共用命令串行化，不把 Pong 的面板亮度表当 zizhan 参数。
2. **外屏单独出图。** 在 Android 对照折叠/展开的实际面板 ID、DSI/DPU 路由和 TE，再用原厂 FV/DT 确认匹配的面板初始化序列、供电/reset、DSC PPS、stride 与时钟。在可验证的固件交接或明确的初始化探针下，先做到外屏 60 Hz 测试图、真实 frame counter、熄屏和返回；之后单独接外屏触摸。首版可像 Pixel 4 一样使用固定逻辑画布，按物理面板等比缩放/留边；触控做对应的逆变换。
3. **折叠切换。** [同机 LineageOS 屏幕布局](https://github.com/LineageOS/android_device_xiaomi_zizhan/blob/8474ba82bc10844e4ddce0925333d081fb5a471c/display/display_layout_configuration.xml) 在普通折叠/展开态各选一块默认屏；[触控端口表](https://github.com/LineageOS/android_device_xiaomi_zizhan/blob/8474ba82bc10844e4ddce0925333d081fb5a471c/display/input-port-associations.xml) 分别绑定 fts-pri / fts-sec；[FoldStatusNotifier](https://github.com/LineageOS/android_device_xiaomi_zizhan/blob/8474ba82bc10844e4ddce0925333d081fb5a471c/sensors/FoldStatusNotifier.cpp) 监听 `xiaomi.sensor.fold_status` 后通知触控。实机 DT 里也有 Hall 线索，先观测它与真实折叠态是否对应。建议先手动切屏，再加去抖与自动切换：排空在途帧，切显示和触控，等新屏首帧/TE，失败恢复原屏。尚无可作为基线的 iPadOS 自动内外屏切换实机记录。

建议顺序：**先复用 g287 做原生 Wi-Fi 服务 A/B；同时准备内屏亮度和 GPU display-link 的独立试验；外屏先争取单屏静态出图。** 三条线各自保留临时启动与自动返回，避免同一镜像同时改变 Wi-Fi 服务、渲染和面板。
