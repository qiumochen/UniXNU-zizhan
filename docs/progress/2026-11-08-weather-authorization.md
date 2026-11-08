# APP-01：天气、权限与定位诊断

## 目标与已纠正的前提

用户要求继续开发，区分天气无内容是否来自系统授权弹窗或 GPS。首个交付是取得可区分这些假设的实际页面、服务与等待栈证据；天气城市温度与预报仍须设备验收。

接续源码为 `791cc1398eb17e41d8b815d76729d9478d76d4d9`，入口工作树干净。当前 OS/UI 保持 OS370。未改内核、驱动、OS 或 Data。

- 重新查看本机 `pp-weather-v18.shot1.png`：页面已有“上海”和温度占位符 `--`，并有左上侧栏按钮。因此撤回“没有任何文字 / 最可能没有城市”的接续假设；先添加城市没有足够依据。该图仍不能区分保存城市与当前位置名称。
- 只读检查与当前安装 manifest 对齐的 OS 镜像：`weatherd`、`locationd`、`tccd` 的 launchd 缓存项和天气可执行文件存在，Weather Info.plist 包含定位用途声明。存在不代表服务请求、授权或定位成功。
- `pp128-v8r2` 的 `locationd` 与 `tccd` 有成功 spawn 记录；`weatherd` 无匹配日志只能说明已捕获文本中没有记录，不能据此证明没有网络请求。`applog_name` 选择进程日志，不能替代网络跟踪或完整统一日志。
- 历史 [电池诊断](2026-11-01-battery.md) 曾证实 `locationd` 被反复退出的 `fairplayd` 同步请求卡住；OS370 保留该服务，此假设需要新栈复核。不能直接推广旧根因。
- 通用 HTTPS 成功只证明当时的测试站点可达，Apple 天气接口尚未验证。定位硬件和系统权限弹窗也尚未取得成功或失败的直接证据。

## v22：保持运行时输入的诊断控制

状态：HOST_VERIFIED（打包、输入身份和 preflight）；设备采集 SKIP，未发送镜像。

假设：天气数据路径被共享服务依赖阻塞；控制为 `pp-weather-v18`；唯一实验变量为诊断配置（选定服务日志 + `locationd` 一次 1 秒线程栈采样），不改变 UI 动作与运行时代码。

- 内核 `4b9871bb004de04210f1893c895f36c86aeb495ae543d60308be249698034522`；loader `1f51ed2b52bee5c42ff6cbd6ec115c4fb7e6e28d67ce96206f97f96c7fb2b8e2`，与控制逐字节一致。
- image `f2c61388c9fd3250da19edcff26583672db7ec6743361762544a8a031b222110`；ADT `c761eb3282c3968c482c7ebfbeb3704bf7d203c22571778b08173b4b6a3f8a7b`，8 个 `cluster-type`。
- `applog_name=launchd,SpringBoard,Weather,weatherd,locationd,tccd,networkd,trustd,nsurlsessiond`；`gs=1000 gs_proc=locationd gs_all=2 gs_sum=1 gs_n=1`。
- 保持 `tsa=1118 pong_touch_synth=200 pong_shot=150 pong_shot_n=1 pong_shot_step=6`。命令行 979 字节。
- 返回：`pong_autoreset=300 pong_autoreset_sb=240`，保留真实 panic 与长按复位；后部 USB-A，临时 fastboot boot，不刷分区。主机监督器有期限、15 秒心跳与日志，结束核对 Android 完成。
- 验收：新鲜日志及有效页面、定位等待栈或明确缺失原因、天气/权限服务实际事件、Android 返回分别判定。无日志不记 PASS。若 CS4K 在用户态前失败，保留负证据，不将其当作权限或 GPS 结果。
- 回退：此轮只改变临时启动参数；OS370 和全部既有回退文件保持。详细 manifest、preflight 与监督信息保留仓库外。

### v22 结果与当前阻塞

21:45 完整 preflight PASS：实际 OS 全窗口 hash 与 OS370 一致，UI/helper、完整备份清单、root/klog 连续 extent 与不重叠、live manifest、温度及可控后部 USB-A 均通过。这里只重新检查备份存在性和既有错误清单，没有重新校验全部备份内容。

21:46 开始主机监督，Android 端成功派发 `adb reboot bootloader`，但 100 秒 USB attach 窗口内没有 fastboot 枚举，cycle 退出 2 / `NO_FASTBOOT`。因此：

| 项目 | 结果 |
| --- | --- |
| 打包与 preflight | PASS，仅主机与 Android 前置 |
| 临时镜像发送 / XNU 启动 | SKIP，未执行 |
| 新天气页面 / locationd 等待栈 / 权限事件 | SKIP，无新设备证据 |
| cycle / Android 返回 | FAIL / 未确认；退出后主机看不到 adb 或 fastboot |
| 常规 3 秒 USB VBUS 恢复 | attach 内一次、退出后两次均未恢复枚举 |
| 延长 12 秒 VBUS 恢复 + 25 秒观察 | FAIL，仍未枚举；未再派发镜像 |

失败保留原运行名与监督记录，不覆盖、不把 USB 失败记为内核、权限或定位失败。停止所有设备操作后，下一动作是重新插拔已验证后部 USB-A 路径；恢复时先确认 fastboot product，返回 Android 并等待 `boot_completed=1`，新一轮重新 preflight/arm，使用新运行名与同一个不可变候选。不要从未 arm 的 fastboot 直接启动候选。

## 对用户问题的当前回答与下一次验收

当前证据不足以在“整个系统授权弹窗失效”与“定位服务阻塞”间定案。`tccd` 运行仅证明服务启动；定位授权还依赖 CoreLocation/locationd 与 SpringBoard 的提示交互，不能仅凭 tccd spawn 认定授权正常。也不能用没有 GPS 修复来解释一切：手机有 GNSS 硬件，但当前设备层的 `drivers/`、`configs/`、`dts/` 未检出 GPS/GNSS 适配接口，功能清单定位仍为待确认；固定城市天气应独立于 GPS 取得数据。

后续按可区分的证据推进：

1. v22 栈若显示 locationd 的同步调用卡在 fairplayd 或其他服务，先处理该实际依赖并做同场景回归，不绕过授权或制造定位结果。
2. 若 locationd 正常，下一轮采 Weather 自身等待栈，并用已有侧栏按钮确认保存城市/当前位置和可见权限状态；此时再决定是否需要添加城市或触发原生定位授权。
3. 应用确实请求数据后，单独检查天气 XPC、DNS/TLS 与 Apple 服务返回。没有捕获到网络请求的日志不能替代这项验收。
4. 权限弹窗需有实际 request → 提示/错误 → 用户选择/状态变化证据；GPS 需有真实驱动与位置更新证据。这两项不互相替代。

没有修改或安装运行时代码。未修复天气、未验证 GPS 或全系统权限弹窗。此次交付为纠正错误前提、固定诊断候选，以及记录阻塞后的安全接续动作。知识图没有本仓库索引，结构检查已回退到限定源码范围读取与搜索；未从无索引推断代码不存在。

## v22r1：重插后的同候选采集

2026-11-08T21:54:56.306534+08:00 用户已重新插线并授权无人值守。fastboot product 核对为 zizhan，已回 Android 且 boot_completed=1。复用 v22 不可变候选，实验变量和验收条件不变；原 v22 未发送失败保留。源码为 `e763c40a39a6bc507fc612fb66bbe656e342f660`，本轮 dirty 仅文档；OS/UI/helper 不变。状态 RUNNING（主机前置），实际 XNU 派发以 cycle 日志为准。

### v22r1 结果与 v22r2 主机连接控制

2026-11-08T22:04:33.001708+08:00：v22r1 preflight PASS、温度31°C，但 Android→bootloader 后再次 NO_FASTBOOT，未发送镜像；退出后释放 adb 再做一次记录端口 VBUS 恢复仍未枚举。cycle FAIL、天气/定位采集 SKIP；单次 launchd 已移除，无遗留设备操作。用户随后直接重启，重新观察到 Android boot_completed=1、product=zizhan。

v22r2 状态 RUNNING（前置）：同一不可变 v22 镜像，唯一新的主机控制是 `adb reboot bootloader` 成功后立即 `adb kill-server`，随后正常目标核对与有界枚举/一次端口恢复；检验切换时 adb USB 句柄是否有关。原退出后的释放失败记录保留，不能当作此时序控制已试过。设备参数与诊断验收不变，30分钟监督、autoreset 300/240，不改OS/UI/Data、不flash。

2026-11-08T22:06:04.239094+08:00 v22r2 已实际启动：主机立即释放 adb 的控制下，fastboot attach PASS（reboot 派发后约10.4秒 ready），镜像 Sending/Booting 均 OKAY，22:05:08 临时 boot。当前仍 RUNNING，不能把 USB 单次通过推广为根因修复；新页面、栈与自动返回待采集。

## v22r2 结果与 v23 隔离对照设计

2026-11-08T22:12:21.686158+08:00 v22r2 采集完成并返回 Android：新鲜 deadline klog 2,118,109 bytes、128 MiB/5 slots、0 NONE FREE、9 次 IOSurface失败、无panic、无renderer jetsam；Wi-Fi state=6、DHCP完成，完整320行截图显示温度 `--`，本轮没有城市标题。不能从这一张图撤回v18曾显示“上海”的事实，也不能认定城市消失来自诊断参数。

`locationd` 1秒1000次采样、0丢失：主线程356与线程1102均1000/1000等待串行队列；线程729为1000/1000同步Mach接收，去ASLR后自己的调用位置为 `0x1003bd6e0 → 0x1003c8fec → 0x1003f13c8 → 0x100401a04 → 0x1003e9240 → 0x10052598c → 0x1004d9028`，与g212旧fairplay阻塞栈逐项一致。独立符号化使用同22H374 locationd与shared cache。当前证据是共享依赖阻塞，不是GPS更新失败或授权弹窗自身故障；服务端归属仍通过下一轮干预确认。weatherd无已捕获日志，不代表完整网络跟踪。

**v23 设计，尚未安装/启动**：只改变OS中的 `com.apple.fairplayd.A2` launchd缓存项与同名job plist（移除），不改程序、内核、UI/helper、Data、其他服务。假设：移除永远无人应答的保活端口后，locationd进入真实失败处理/继续初始化，原生授权和天气数据路径能前进。已有构建默认排除该job，但OS370因历史回填保留，见[旧记录](2026-11-02-gpu-astc-and-fairplayd.md)。

候选由OS370 APFS clone隔离准备，canonical prepared/layout不改；按原64MiB块散列比较，仅安装改变的OS块与描述符，执行完整live OS hash读回。安装前重验备份、root/klog extent、manifest、Android、温度与USB-A，监督/锁保证无并发。启动复用v22内核/loader/参数与同一截图/定位采样；v23不注入位置、不绕过授权、不伪造fairplay服务。回退使用完整OS370与原manifest，失败或未证实收益时恢复并核验。验收分列：locationd旧栈消失、主线程恢复、天气/权限可见变化、网络/显示与Android返回；旧栈消失不能单独宣称天气已修好。

2026-11-08T22:13:01.756823+08:00 v23主机准备PASS：APFS clone完成、缓存语义对比确认仅删除fairplayd job，其他服务/UI/helper保持，程序本体保留；OS候选SHA256 `fa99737b38035b387bc352e32862178a17b1c1a3ec11ff5a51fc058b0299cb26`，改变5个64MiB块，共320MiB。即将安装，安装与整窗读回后才可称已安装；canonical文件和原OS370保持。操作监督对写入设20分钟期限，前后验证各300秒、温度前置20分钟分别有界。

2026-11-08T22:15:09.934942+08:00 v23 OS-only安装与完整live OS读回PASS，候选OS身份已确认，内核尚未启动。下一轮复用v22逐字节相同boot image，只有已安装OS的fairplayd job存在性不同；RUNNING（启动前置）。

### v23 实际结果：安装通过，设备对照未启动

2026-11-08T22:19:44.203019+08:00：OS-only安装及完整live OS哈希读回PASS，手机实际OS为候选 `fa99737b38035b387bc352e32862178a17b1c1a3ec11ff5a51fc058b0299cb26`。随后v23 preflight再次PASS（31.7°C、备份/extent/manifest/UI/helper），但100秒attach窗口再次NO_FASTBOOT，镜像未发送。即使reboot后立即释放adb，仍不稳定，因此撤回任何“该操作修复USB”的推断，只保留v22r2的单次成功控制。v23 cycle FAIL，locationd/天气干预结果SKIP；无新XNU或天气证据。

当前adb和fastboot都不可见，实际运行状态未知；最后已确认Android在22:15 preflight，随后仅派发reboot bootloader。已退出并移除v23 launchd job，无cycle/installer/waiter/设备锁。候选OS已安装但未启动，canonical prepared/layout仍是原OS370，禁止混为同一状态。当前未能回退的具体前置为Android/root不可达；恢复Android后可通过保留的OS370、变化块反向清单和原manifest执行有界OS-only回退，再做完整live hash校验，不需要刷启动分区。

对问题的结论推进为：**有直接设备证据证明locationd的初始化/串行队列被同步IPC阻塞**；调用位置及MIG1203与旧故障一致，优先验证fairplayd依赖。尚不能宣称GPS是根因，也不能宣称全系统授权弹窗损坏。没有绕过权限、伪造位置或屏蔽真实错误。下一设备动作须先恢复可见连接并核对目标/已安装OS与日志arm状态，保留未发送负结果后执行新命名对照；不重复不变的Android→bootloader枚举失败。

文档检查与私有路径扫描通过。主机准备检查证明cache语义仅删除一个job、UI/helper与其他job保持；无内核/驱动源码变更，不重复内核构建。公共源码尚无新天气修复；默认构建原本就排除此job。全部原始日志、APFS候选、回退镜像、改变块和安装状态只保留本机。

## v23r1：用户再次重启后的无人值守连接控制

2026-11-08T22:24:43.968193+08:00 用户确认手机停在fastboot画面并直接重启，主机重新确认Android boot_completed=1。已安装仍为v23隔离候选（启动前重验manifest）。不重复原切换：由Android nohup脚本延迟7秒执行真实 `reboot bootloader`，主机在此之前释放adb服务；其余target检查、只用记录USB-A与单次端口恢复保持。此次唯一新变量是重启相对主机释放USB的时序，不改变镜像/OS/UI/诊断。30分钟单次launchd监督、15秒心跳、300/240秒内核返回，原失败保留，新run名v23r1。若成功则直接收集既定locationd干预对照；若连接仍失败，不把动作已派发记成天气结果。

### v23r1连接负结果与v23r2被动恢复

2026-11-08T22:32:53.651476+08:00：v23r1的延迟7秒重启/主机先释放adb仍NO_FASTBOOT，未发送镜像。端口readback为USB2 `0x0100`（仅供电，无连接），一次限定记录手机端口的标准hub PORT_RESET返回0但仍`0x0100`，15秒观察无枚举。结果排除了“只需重新打开fastboot客户端”的简单解释，不能据此归因手机或线缆的具体硬件故障。原job退出并移除。

v23r2改为单次被动恢复：15分钟内等待稳定、product核对通过的fastboot，不再派发Android重启或反复VBUS循环；连接恢复后直接临时boot同一不可变候选，再走既有autoreset/日志收集。输入前置取本次Android的实际候选manifest、extent与31°C检查，失败attach前已走klog arm且无XNU派发。恢复器不伪造arm marker，结束显式读pinned copy、保留ramoops，拒绝与v22r2字节相同的旧日志，并要求预期定位诊断payload；缺失则FAIL/SKIP，不提升功能资格。总监督30分钟、15分钟等待期限、日志/锁/launchd均留本机，超过期限停止，不无限等待。

22:34确认恢复器launchd进程存活、状态RUNNING/被动等待、未派发boot。已配置本线程有界Paseo心跳每3分钟检查，35分钟到期/最多12次；只在状态变化或可执行下一动作时处理，禁止并发设备操作。等待超时没有XNU即记SKIP并清理，不再盲重启。

### 为什么以前可跑、现在连续失败（只读复核）

2026-11-08T22:39:26.264381+08:00 按当天 `entered=true` 的USB attach记录复核：21:40接续前有26次PASS、6次NO_FASTBOOT；既有失败时刻为19:41、19:58、20:12、20:22、20:33、21:23，期间又多次成功。它们是attach尝试数，不等于设备功能轮次数或长期可靠性统计。此前最终跑完并不代表中间没有枚举故障；现在的变化是失败连续出现、既有恢复未生效。

21:48和21:56两次失败仍使用原OS370，22:05同OS又完整往返；v23删fairplay候选22:14后才安装，至今未boot，不能作为此次故障起点。受控usb-attach/usbkick/boot脚本与接续HEAD逐字节相同；本轮未更新platform-tools，当前可执行文件mtime为7月，不能据此推断更早的环境历史。主机近1小时内指定内核USB错误的统一日志查询无匹配，仅表示该日志源没给出额外证据，不证明没有底层USB错误。

手机显示fastboot与USB完成枚举是两步；用户已确认前者，主机在失败期间持续看不到后者且hub连接位为0。根因仍待定位，不能仅凭此断言线缆损坏、Mac系统回归或新OS引入故障。当前被动恢复器继续独占，未新增重启/端口动作。

### v23r2 终态审查：中断、未发送，USB已换身份

2026-11-08T22:54:14.969616+08:00 心跳复查：恢复器在22:47:46记录FAIL/`supervisor interrupted; inspect device before retry`，`phone_boot_dispatched=false`。这不是正常900秒超时；中断发起者未由本记录确定。主机复核时launchd job已不存在、监督进程退出、锁已释放，无活跃cycle/install/waiter。没有新XNU、定位栈或天气结果，干预验收SKIP；未修改原始失败记录。

本次只读端口复核出现新状态：USB2从仅供电变为highspeed/enable/connect，手机身份为`05c6:f000`，IORegistry只列出`Mass Storage`接口。主机adb最初探测8秒超时；有界恢复adb服务后，adb和fastboot列表仍均为空。现在不能再沿用“物理未检测连接”描述当前状态，也不能把此身份当作可用Android/root或fastboot。未发送模式切换、重启、boot或端口恢复命令。

实际已安装OS仍是v23隔离候选，未启动验收；canonical仍OS370。回退的未满足前置仍为可用Android/root通道，当前不能安全执行OS-only回退。下一动作是先恢复并核对Android adb或真正fastboot接口，再协调唯一owner与实际manifest/log arm状态；不重复已有的Android切换、VBUS或PORT_RESET负结果。有界Paseo心跳已删除，全部设备监督任务已结束，不再自动重复启动。

### 主机重启说明与恢复后检查

2026-11-08T22:57:46.717477+08:00 用户说明刚才重启了电脑；主机kern.boottime为22:48:05，与监督器22:47:46退出相吻合。此前“中断来源未知”据此补充为用户执行的主机重启上下文，不能当作手机自动返回或等待超时。重启主机并未自动恢复调试接口：再次只读核对仍为Xiaomi `05c6:f000`、仅Mass Storage，adb/fastboot列表均空。USB连接已成立，当前明确前置是手机提供真正的adb/fastboot接口，而不是继续重启电脑或重复端口复位。未发送任何phone写入/重启命令；候选安装状态与OS370回退保持。
