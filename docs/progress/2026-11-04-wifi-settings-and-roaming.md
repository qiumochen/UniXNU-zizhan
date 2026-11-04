# Wi-Fi：跨频段重连与原生设置页（2026-11-04）

接续 [原生接口修正](2026-11-04-wifi-native-repair.md)。用户明确授权无人值守专攻 Wi-Fi，使用已有截图、手势采样、UFS 日志和自动返回。全部轮次使用后部 USB-A、临时 `fastboot boot` 和 autoreset；完整分区备份保留，未刷写分区。

本文保留 g299–g333 的历史过程。后续 PrimaryTimeout、连接保持和原生开关验收见 [11 月 5 日生命周期记录](2026-11-05-wifi-lifecycle.md)；下文“周期断开待定位”不是最新状态。

## 跨频段重连

旧 g292/g294 的首次连接与主动续租通过，但跨频段重连在管理帧阶段触发固件 RDDM。核对固定 Linux v7.0.14 的 `ath11k/mac.c`：`ath11k_recalculate_mgmt_rate` 缓存 `ar->hw_rate_code`，`ath11k_start_vdev` 在后续启动时重新写入它。共享 legacy station 原本到收到关联响应后才通知 `BSS_CHANGED_BASIC_RATES`，无法在下一次跨频段认证之前更新缓存。

设备构建适配在生成的 station 源中，使用该 AP 的 Supported Rates IE 和目标 band 的速率表，在 `assign_vif_chanctx` 前计算基础速率并发送真实通知。未修改固定的共享仓库，未迁入别板固件或 PHY 参数。新增测试专用 `pong_wifi_band=2|5` 可固定首次频段；`pong_wifi_alt=1` 时要求相反频段和不同 BSSID，仍只匹配私有配置的同一 SSID。该控制只在 `pong_wifi_test=1` 时生效。

| 轮次 | 实际频率 | 结果 |
| --- | --- | --- |
| g304 | 5220 → 2452 MHz | 首次、主动续租、重连后 HTTPS 三次 200；断线清理通过，无 RDDM |
| g306 | 2452 → 5220 MHz | 固定首次 2.4 GHz，切到不同 BSSID；首次、续租、重连后 HTTPS 三次 200；network lifecycle PASS，无 RDDM |

g304/g306 的 pre-start 基础速率位图分别为 band 1 的 `0x15` 和 band 0 的 `0xf`。这两轮支持修正对本机当前 AP 的效果；自然 T1、不同 SSID/子网、长时间漫游和真实 MSI 中断仍需要独立验证。

## Wi-Fi 镜像显示池

g292–g295 均未取得 `/chosen/pong-panel-pool`，显示退回 splash 双缓冲；不能用这些轮次的黑屏断言原生设置界面失败原因。g299 保持同一个含 Wi-Fi 的大内核但关闭无线工作，仍没有显示池，却可打开设置页。这表明显存与系统服务阻塞是两个问题。

DarwinBoot24 原先在释放 `ReadFile` 的完整内核临时缓冲之前申请 96 MiB 低地址显示池；含 Wi-Fi 固件的内核比 GPU 内核大约 26 MiB。改为完成内核/ADT 拷贝并释放临时输入后、领取其余 RAM 和退出 boot services 前申请，仍只使用 UEFI 实际分配的页，不硬编码物理地址；失败继续记录 SKIP。

g300 仅更换 loader，获得实际 96 MiB 显示池，恢复零拷贝、完整 135×120 采样截图和设置页。g301 再开启无线工作后显示池仍可用，但主屏没有呈现，说明服务阻塞另有原因。新 loader 以既有 LLVM 18 工具链构建；saved FD 没有修改。

## 原生服务启动等待

- g301：SpringBoard 主线程和 SBWiFiManager 线程等待 `WiFiManagerClientCreate → wifi_manager_open` 的原生 Mach 回复。
- g302：wifid 主线程处于 NSRunLoop；工作线程等待 `__SCNetworkInterfaceCopyAllWithPreferences` 的 dispatch_once，另一线程进入 `SCDynamicStoreNotifyWait → __wait_for_IOKit_to_quiesce`。configd 已经打开网络栈。
- g303：configd 主线程和 `InterfaceNamer_thread` 都正常运行 CFRunLoop；没有证据说明它们死锁。
- g305：将临时启动时间延长到超过原生 IOKit 等待后，主屏正常呈现、设置 URL accepted=1；Preferences 在 uptime 约 336 秒有实际首屏。

参考 Apple [configd 固定源码](https://github.com/apple-oss-distributions/configd/tree/585b7f2fca293f4642d21d15c5daf187f63c4796)：`SCNetworkInterface.c::__wait_for_IOKit_to_quiesce` 等待 `Plugin:InterfaceNamer` 的 Quiet、Complete 或 Timeout；`ifnamer.c` 默认 WaitStackTimeout=300、WaitQuietTimeout=240 秒，允许 bundle Info.plist 配置这两个值。本机 22H374 configd 同样含配置键及 300/240 double 常量；原始 bundle 未覆盖它们。此前常用约 156 秒返回的界面轮次尚未等到原生回退。

镜像组装现从原系统的 InterfaceNamer bundle 元数据保留全部原键，指定原生 `WaitStackTimeout=30`、`WaitQuietTimeout=10`。由 configd 自己超时、命名和发布状态，没有写入假 Quiet，也没有替代 MobileWiFi 服务器。g307 日志实际记录 present=1、quiet=0、complete=1、timeout=1；Settings 显示原生 Wi-Fi 开关和网络列表，持续扫描已进入驱动。g307 的 UI 控制在目标网络不在当前可见部分时到达期限，没有进行加入；不能记作连接通过。

## 无人值守设置操作

`pong_wifi_ui_test=1` 只启用有界 UI 测试：读取本机测试镜像内权限受限的私有配置，滚动寻找精确 SSID 对应的真实单元格，走设置页现有选择回调、secure text field 和 Join 控件。不开启内核自动关联；同时设置两个测试开关会被 helper 拒绝。测试默认关闭，凭据、网络标识和测试镜像不提交。

UI 控制日志只有动作阶段，不能证明连接成功。g308 已滚动选中目标并填入密码，但未触发导航栏加入，到达期限；g309 改用 secure text field 的 insertText 与原生导航栏 Join action 后，实际收到 900 字节 Apple80211 SET（type 20，32 字节 PMK，cipher 6），握手 message 4 确认、密钥安装、系统 DHCP 与原生 HTTPS 200 / 577 字节通过。实时 ACK 的路由/DNS 补偿随后因原生配置接手而撤销。独立核对的这些结果才作为加入通过依据。测试镜像的 private fixture 在清理后才可推广到默认准备镜像。

## 凭据复用与重启自动加入

g312 的测试文件只含精确 SSID 和 `remembered_only=true`，没有密码；内核也没有网络凭据或自动关联开关。设置页选择该真实行后，未出现密码输入，stock wifid 从系统保存的凭据发出原生关联请求，WPA2、DHCP 和 HTTPS 200 / 577 字节通过。该结果证明本次保存凭据可复用，不等于重启会自动加入。旧 UI 控制器仍以等待密码/Join 为终点，最终打印了 control deadline；独立握手和 HTTPS 证据支持网络成功，不能把这个 UI 控制器终点写成 PASS。

g310、g313 和 g314 移除测试文件，实际无线和原生扫描正常，但没有关联或 HTTPS。g314 读取设置页同一 uid 501 上下文中的原生 SetupAssistant：NeedsToRun=0、HasCompletedInitialRun=1，AllowEnable=1；持续提交扫描请求，未提交 type 20。因此不能归因于 setup 未完成、凭据丢失或无线无法扫描。自动加入策略仍在继续定位。

g315 临时通过真实 MobileWiFi 属性启用 wifid 自己的文件日志，75 秒后取回本机诊断片段，并成功恢复四个原属性。日志明确显示保存网络通过 suitability 检查、没有 denylist，随后 auto-join failed with no network found；普通扫描则取得几十个结果。实验的日志打开/原始回传代码未保留在发布源中。

检查 [Apple80211 结构定义](https://github.com/OpenIntelWireless/itlwm/blob/53c51c2cdd6e4b69beb91f310d74c53422b0f8bd/include/Airport/apple80211_var.h#L487)：`asr_age=0` 表示本轮扫描结果，非零表示缓存。station 的 `age_ms` 却表示自收到真实帧后的毫秒数，包含扫完其余信道的时间；直接复制会把所有本轮结果也标为缓存。g316 实验记录实际请求时间，只对本轮确实观测到的首次枚举清零 age，仍未恢复自动加入；该实验已撤回。其原生 known-network 查询为 count=1、enabled=1、autojoin-pref=1、first-join-block=0、future-block=0。年龄语义目前不能认定为根因，后续继续检查原生扫描属性过滤。

## DMA 预算回退

g311 在关联前即失败于 128 MiB wired DMA 池申请，设置页开关关闭且不可用；它不能作为保存密码失败的证据。驱动现先申请实际 128 MiB，申请、prepare 或映射失败则释放描述符，再尝试实际 64 MiB。SMMU 页表、IOVA 范围和分配器边界全部使用真实申请长度；两个预算均失败仍停止无线初始化，不伪造资源。

g312 强制 64 MiB 后完成保存凭据复用及 HTTPS。g313/g314 继续使用同一预算，固件和扫描工作；g314 的周期诊断明确记录 `dma-mib=64`。这验证了较小预算可供本机当前固件工作；尚未人为制造 128 MiB 失败来单独验收自动回退分支，也不把短轮次算作长期压力验证。

## 11 月 5 日凌晨：自动加入决策链

g317 填入 AP 实际 Supported Rates 列表、g318 单独统一支持信道与扫描记录的 flags，均未恢复自动加入；这些单项实验已撤回。g319 的公开 OSLog 接口在该系统只支持当前进程日志，系统范围查询没有返回 store。g320 从真实 CoreWiFi 客户端读到候选 SSID 数 1、候选 BSS 数 2，没有低信号、禁用或关联错误；它没有证明已关联。系统日志枚举未完成返回，未将其记作日志采集通过。

后续本机临时观察库保留原函数和所有返回值，只记录已保存 SSID 的属性比较结果。环境变量方式的 g321 没有确认加载；g323 使用已有加载命令工具，在本地测试版 wifid 中保留原始 entitlement 后完成加载，日志实际确认 uid=0 和原生方法签名。测试二进制及库留在本机，必须恢复原始 wifid 才能作为最终镜像验收。

22H374 的扫描记录实际为 2264 字节，独立的 64 位 `TIMESTAMP` 位于 `0x8d0`。原适配只返回 1164 字节前缀，时间戳为零。g322 补完整记录，以真实帧的毫秒年龄和单调原始时钟换算时间，旧客户端继续接收旧前缀；该单项仍未恢复自动加入。g323 的进程内观察确认真实结果已经进入 CoreWiFi：时间戳约 43.481 秒，读取时约 44.696 秒，对应 age 1214 ms；SSID 和安全类型相符、ESS/PSK 为真，预匹配返回真，最终匹配仍返回假。

g324 首次因 USB 未枚举而没有启动；恢复到 Android 并重建日志标记后的 g324-retry 实际启动并自动返回，但时间戳与信道表示组合仍未恢复自动加入。g325 进一步确认全局和本轮上下文中的已保存网络：基础安全类型 `0x10`，有效类型 `0x50`（存在交集），Passpoint 均为假，非配置描述文件/企业网络，名称无歧义，BSS 历史有一项且匹配其中一个 AP。后续继续观察实际匹配对象及允许加入回调，不能仅从外层返回假就断言某个内部步骤没有执行。

g326 实际确认匹配对象已生成、已保存网络许可通过，而候选许可返回 false、error code=1。g327 恢复原始签名 wifid 后仍未自动加入，排除了观察版重签名作为主要原因。g328 取到外层通用错误，g329 再透传观察原生守护进程许可 block 的回复，得到具体原因 `Below minimum RSSI threshold`；候选实际信号约 −16 / −22 dBm。

22H374 在创建设备时通过 `RSSI_BOUNDS`（type 106，16 字节：version、min、max、threshold）初始化门槛。旧驱动返回不支持，设备门槛没有完成初始化，随后以 0 dBm 比较正常的负 dBm 读数。适配现从实际后端 `SIGNAL_DBM` 声明和 `ieee80211_rx_status.signal` 的有符号 8 位表示中取得范围。min/max 是 **表示范围，不是 RF 灵敏度或校准测量**；附加的旧式许可门槛设为表示下界，表达 station 本身没有额外 RSSI 准入过滤。CoreWiFi 原有约 −80 / −90 dBm 的质量策略继续执行，实际 RSSI 数据不变。精简后端没有完整 mac80211 的 `wiphy.signal_type` 初始化，因此单位校验直接读取 ath11k 实际设置的硬件声明。

g330 没有进入临时启动（USB 前置失败），不能记作 RSSI 设备结果。g331 的查询实际返回 0，守护进程许可转为 allowed=1，stock 原生 900 字节关联请求、WPA2 密钥安装和 HTTPS 200 / 577 字节通过。这是保存网络自动加入首次通过；镜像没有 UI 私有配置或内核网络凭据。但该轮出现后续主动断开及重新关联，稳定性尚未通过。

接口对照发现后端实际授权成功后只发布了 association/link 通知，缺少标准 `APPLE80211_M_RSN_HANDSHAKE_DONE`（20）。新通知仅在真实 `NW_RES_OK`、且该连接使用密钥时发送；后端在密钥安装和 STA_AUTHORIZED 成功后才报告该结果。g332 恢复原始 wifid，关闭设置页启动并使用实际默认 128 MiB DMA，自动加入和 HTTPS 200 再次通过，但仍主动断开并重新关联三次，通知补齐没有单独解决保持问题。原生日志采样一次未启动（USB 前置失败），一次在 initproc 共享缓存检查 ENOMEM 后真实 panic；均未算作 Wi-Fi 结果。后续使用已验证的 64 MiB 预算继续采集。

## 主机验证

- Wi-Fi、CI、GPU 三配置均完成构建，日志各自结束于 KERNEL_DONE。
- UI 动态库及网络 helper 独立编译、签名和 strict 签名检查通过。
- 既有 SMMU、WMI、DHCP、USB 回归全部通过；新增镜像组装回归覆盖完整 root/overlay、原生端口设置保留和错误 OS build 拒绝。
- 每次 OS delta 安装先核对实际 pinned extent、完整旧 manifest 和每个待写旧块，再按写入后的 SHA-256 读回。只改 OS 窗口，保留 Data/SystemOS。安装状态和旧准备镜像分开保存，以本轮实际安装 manifest 为准。

源码提交：loader `03363797d9ffb10fd35ad5823331e89b4bb3c624`、跨频段速率 `9cc393829e0cc631012de6f0c8c1a0aa54b8e840`、原生启动与 UI 验证 `92ef6d2de5419ce5c5aa8bfe36ab25a9fadeffb7`。g299–g309 是形成这些提交的本地快照；每轮改变项在上文分别说明。[脱敏证据](../evidence/2026-11-04-wifi-settings-and-roaming.txt) 保留实际阶段和失败轮次。保存凭据复用、自动加入及 HTTPS 已分别留证，持续连接仍在推进；上述结果不自动升级为全部 Wi-Fi 功能。后续源码提交：实际 DMA 预算 `fccaf40168747d4e3f815ec1ab6631622283682d`、保存凭据及策略观察 `89ac9a5c6591ddbfef6b10a786b0aaaa530d9be8`、首次请求诊断 `435a1926b364ef433859c8aac4e6b002430327f6`、原生 RSSI/扫描及认证通知 `718f73ce424d427c2ecdc757b9c64eb7cd4f3dca`。
