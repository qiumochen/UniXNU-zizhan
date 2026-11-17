# 系统应用无人值守：本批交付与历史（2026-11-14）

当前状态统一见 [STATUS](STATUS.md)。用户目标为天气真实固定城市温度/预报/刷新，以及Safari正常网页、输入/滚动和视频播放。本批沿main/22H374，不接回native22G100支线。上游/兄弟修复取舍见[调研](../research/2026-11-14-main-app-repair-directions.md)，接续目标见[系统应用方案](2026-11-14-system-app-resume.md)。

## 已验证范围

| 交付 | 实际证据 | 限制 |
| --- | --- | --- |
| Safari静态H.264静音视频 | actualSafari页面、HTTP Range/真实媒体事件、数千帧回调、pause稳定currentTime/resume推进、两张完整不同视频帧、正常返回 | LAN非DRM960×540样本，不等声音/硬件解码/所有站点 |
| Safari公网HTTPS文档 | 3次真实WKWebView URL/title/正文为ExampleDomain，与站点内容对照；正常返回 | 初期把站点说明误读为错误页，已明确撤回，不继承旧错误解释 |
| Safari长文正常滚动 | 实际RFC9110，单次有限HID手势，scrollY0→1493保持，文档height172229，2张完整不同段落截图 | 真实地址键入尚未完成；短示例页回弹那轮不算长文资格 |
| 普通定位权限UI | 总开关正常HID后画面变绿；Weather新提示已原生选择“使用App时允许”，提示消失且列表2项→3项 | 非GNSS资格；天气仍`--`，没有预报 |
| 分配范围诊断与候选参数 | 实际KMA_DATA大对象范围773MiB只剩1.7MiB，16MiB失败；已有compressor4GiB上限控制扩为3052MiB，后轮无该分配失败 | 压缩容量降低的长期/压力资格未过，不默认推广 |
| 截图/日志/打包基建 | 第一帧相对时钟、稀疏统计、DIAG边界、完整像素/逐张校验、可选只读last-latched静止截图、显式installed-trial全OS前置、AMP/参数长度拒绝 | 主机/实际执行与应用资格分别记录，缺图不补造 |
| Mach-O工具修复 | fatalPC定位Safari入口被新增命令覆盖；guarded修改、原section/entry字节不变、真实包签名/幂等和主机拒绝控制；设备恢复无SIGILL并读正文 | 可选metadata回收仅显式诊断候选，保留UUID/平台/入口/导入/重定位 |

本批未改启动分区，不清空Data；原v23/OS370、旧候选和完整备份/回退链保留。所有镜像、Apple二进制、原始日志和精确安装收据留仓库外。源基点为`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`加按轮固定的dirty源码/hash；未将本批候选推广默认，尚无本批commit/push/CI。

## 21:27 接续对齐与下一单变量

q/o已终结并返回Android；q完整图为上海仍`--`。当前已安装keyboard-v2（UI慢速HID/最高层8个窗口只读观察），OS-only前后整卷核验通过，未离线改Data。Safari s保持已验证内核/EFI、4GiB VA参数和自动返回，初始example.com，正常HID点地址栏后键入example.org；必须实际地址和内容改变才通过。旧netdiag-v4/OS370回退保留。主机包首次缺显式template被拒，未发设备；新显式本机template/DTB打包1020字节通过，保留失败记录。

## 按批次保留的历史

以下条目按当时记录理解，不覆盖STATUS顶部当前安装和下一任务。它们保留失败、负控制与解释纠正，不构成多个任务队列。

## APP-01 无人值守（11-14 16:09 起）

- **20:18 应用授权下一步**：m高分辨率裁剪明确原生三选项（允许一次/使用App时允许/不允许），当前n按正常HID选择“使用App时允许”，冷却/单设备有界前置保持。不会注入授权结果；选择后的权限/请求另验，未证明GNSS。


- **20:09 正常权限流程推进**：全局定位开启后Weather出现新的应用位置权限三选项模态，城市水平手势被模态截获，未切换。下一高分辨率按钮裁剪后按原生UI选择适当“使用App时”权限；不离线写TCC/坐标，不将全局/应用授权等同GNSS可用。当前m轮裁剪前置冷却中。


- **20:00 原生定位开关UI操作完成**：location d用正常HID点全局开关，点击后的完整画面显示绿色开启和应用列表；前四次控件采样在点击前仍on0，时间边界分别保留。未离线改授权数据库，未证明GNSS可用。下一Weather l单次正常水平城市切换，核对是否是当前位置页/保存城市页和主预报请求。
- 地址输入n仅Command-L聚焦成功，目标URL未导航；完整图/实际URL仍示例站，输入未验收。准备把聚焦与键入分开并降低按键速率；不会用API导航替代真实输入通过。


- **19:45 Weather真实网络结果**：k轮实际地图元数据HTTP200/18343字节，两条bag任务state3/status200，原XPC连接名可见；未看到主预报数据请求，不能继续归为通用Wi-Fi/TLS失败。下一正常Settings全局定位开关UI对照及Weather请求停点；不注入权限/位置/认证数据。当前实际netdiag-v4保持，地址键盘输入n轮冷却中。


- **19:40 当前安装/请求观测**：actual app01-netdiag-v4 OS310322ce…/UI3beefd21…，UI-only候选全卷读回通过。Weather completion接口未命中后，新增delegate task只读状态及原NSXPC服务/错误转发（不代答）；host原对象/错误回调sanitizer控制通过，正在k轮实机观察。地址键盘输入候选n已准备，尚未验收；长文滚动与视频限定结果保留。


- **19:35 长页面滚动通过**：Safari m真实加载RFC9110 HTTPS长文，单次有限竖向HID后scrollY 0→1493并保持，文档height172229；2张完整图显示不同段落，196秒/Android返回通过。`Safari.wkbundle`警告与正常页面/滚动同时存在，不能单独定为根因。地址键盘输入仍待验，Weather仍无数据。
- 实际安装目前 app01-netdiag-v3（OS46be8a25…/UIf1364cdd…），网络completion API已启用但未命中，不等于无网络；新delegate task/XPC原调用观察器主机构建/转发控制通过，安装前按温度门槛冷却。默认关闭的真实HID地址输入单轮候选已准备，未提前提升设备能力。


- **19:17 VA布局单参数结果**：已有压缩池限制4GiB的i轮实测DATA范围3488MiB/大对象3052MiB，原控制分别884/773MiB；本轮无range申请失败、无MiniIOSurface no-memory、Wi-Fi64MiB成功、无CS4K，正常返回。仅一轮支持该方向，不提升长稳/内存压力资格，不推广默认。静止屏幕无新帧导致定时截图缺失单列SKIP；准备可选只读last-latched池缓冲截图，GPU in-flight/flip/poison时跳过。
- 新网络观察安装首次因35.9°C前置拒绝且未写入；改为有界冷却后再实际整卷核验，仍通过原备份/extent/manifest/温度门槛，不绕过失败。下一网络元数据、键盘输入候选默认关闭，设备范围另验。


- **19:13 失败范围已定位/单参数验证中**：新失败日志实际16MiB请求落在KMA_DATA大对象范围约773.6MiB，仅剩约1.7MiB空洞。Wi-Fi/CS4K尚未在同范围失败现场各自证明，但已解释本轮IOSurface分配。保持安全隔离/真实错误，正在用已有`vm_compression_limit=1048576`（4GiB压缩池上限）做单轮VA布局控制；压缩器模式不改，后续压缩容量降低须单独资格，未推广默认。


- **18:49 应用交互进展**：Weather f使用真实HID点击定位提示“取消”，完整截图确认模态消失但北京仍`--`，不能把无数据继续归因提示遮挡。下一g仅增加weatherd触点后一秒全线程采样。Safari j已实际执行1次有限竖向HID，2张完整图/3次正常HTTPS文档，但短页面位移可能回弹，暂不提升长页滚动资格。后续用长页和实际scrollY验证。
- 预备的NSURLSession观察器仅记录真实task/completion元数据，host ASan/UBSan检查请求/task/data/response/error和无回调路径原样转发；新构建/签名通过，尚未部署该版本。它不修改URL/状态码/证书/认证或天气数据。只在具体请求现场取证需要时装隔离候选。


- **18:29 HTTPS解释纠正与实际进展**：Safari h安全入口修复后无SIGILL，3次只读WKWebView观察均为https://example.com/、标题Example Domain和真实多语言正文，与同站主机HTML/脚本行为相符。此前把e的站点说明页读成Safari错误页是误判，明确撤回；保留当时原图/日志。公网HTTPS文档加载有实机证据，地址输入/滚动仍待验；本轮像素日志不完整，不能记新完整截图。v4actualOS/回退保持。


- **18:24 新Safari退出根因与修正候选**：真实fatal PC去slide后均为0x100000548，恰是MobileSafari入口；原20命令末端1336/入口1352，旧add-load-command工具新增命令末端1392覆盖4字节入口。已明确新诊断候选SIGILL来自工具覆盖，不是原HTTPS根因。工具现先检查padding/已有重叠、临时副本、原section/入口逐字节不变及实际bundle签名；空间不足默认拒绝，显式可回收3类可选metadata命令，保留UUID/平台/入口/导入/重定位。实际好/坏文件拒绝与原SHA不变、真实App安全候选严格签名和幂等控制通过。
- 当前实际安装 app01-appdiag-v4：OS fd147cd0…，UI32cf不变；只修Safari损坏入口及签名资源，OS-only全卷读回通过。正在Safari h单轮验证，不提前宣称修复实机成功；旧v3等回退完整保留。


- **18:04 真实启动故障**：Weather d在应用前触发CS4K存储分配`kr=3/KERN_NO_SPACE`（申请1,949,696字节），随后launchd共享缓存未载入而panic；新鲜panic日志/Android返回保留，应用验收SKIP，整轮设备功能FAIL。全kernel_map free743,714,815/largest301,559,808不等于KMA_DATA目标range可用空间，未以“RAM不够”定因、不删签名/断言/错误。
- 当前唯一Safari g为独立的新fatal PC/LR取证，非相同Weather失败重跑；实际安装仍v3，单轮210/150返回。若同前置再次CS4K失败，先修启动分配/目标range问题而非继续应用重试。


- **18:03 当前唯一轮次**：实际安装已为 app01-appdiag-v3（OS5926e755…/UI32cfad2f…），OS-only全卷读回通过，旧Data及全部回退保留。Weather只读文字确认北京为第1/2个保存城市，仍`--`和系统定位提示；v3增加SpringBoard上层窗口提示文字及真实fatal PC/LR后重新抛出。当前Weather d在跑，后续Safari g按新fatal证据定位；不可沿用“v23仍实际安装”的历史段落。
- Safari f加入直接诊断库后多次SIGILL退出，明确是新候选实际FAIL，不与旧UI下可播放LAN视频/HTTPS错误页混同。新观测不得吞真实fatal。截图完整性现在同时给逐张结果和整体结果，缺一张时整体仍FAIL，已完整的另一张可单独核验；真实旧e负对照通过。


- **17:41 当前实际安装更新**：隔离 app01-appdiag-v2 已OS-only安装及整卷读回通过，UI135d869f…，旧Data/原UI-open-v1/v23/OS370回退保留；canonical不推广。只读Weather/Safari视图/WKWebView观察默认关闭，本轮显式启用；应用显式load command及原entitlement签名核验通过。weather:// 的fallback曾错误打开Safari，现按真实scheme回Weather，正在有界验证。
- 公网HTTPS e实际进入Safari错误页，原因尚未定；WebContent报告Safari.wkbundle preflight失败，SystemOS无独立Safari可执行但同版dyld cache确含该代码，因此不直接用缺文件推定缺组件或复制替代代码。先采可读错误/文档与请求。Weather b被旧fallback覆盖，未获固定城市内容资格；不把那轮当天气网络结论。


- **17:10 本轮视频资格通过（限定范围）**：实际Safari加载LAN诊断页与MDN公开CC0 H.264 960×540样本，服务端记录真实Range/浏览器媒体事件、数千帧回调；pause后currentTime保持、resume后继续推进，2张完整实际画面分别为暂停overlay与播放中不同视频帧。288秒返回fastboot、fresh日志和Android返回通过。只证明该非DRM视频静音播放，不证明声音/硬件解码/所有网站；公网HTTPS、地址输入/滚动、天气仍待验。
- 本轮URL打开API返回0却实际进入Safari，已经由HTTP/页面/视频/截图交叉证实；不得将返回布尔值单独当失败。新稀疏常规日志和DIAG截图标记生效，截图计划/两张记录保留；下一公网HTTPS与天气分别推进，返回38.5°C后先按既定温度条件冷却。


- **17:00 新库设备结果**：Safari1114d 单轮289秒/Android返回通过，2张完整采样图均为锁屏，未验网页。当前UI的自动唤醒/测试解锁已改为显式`zizhan_bringup=1`，本轮未带；下一显式带既有bring-up参数，不能把停锁屏误判网页故障。
- 日志丢失继续定位到120fps下每64帧大量常规汇总覆盖环形区。现将常规汇总默认约5秒一次（`pong_scanout_log_ms=0`保留逐64窗口诊断），错误/最终计数保留；截图计划与开始/结束写独立DIAG。最新source双配置及Display host在重验。视频LAN观察页准备中，真实MP4、Range、浏览器事件和画面分别验，公网HTTPS资格仍未完成。


- **16:40 Safari控制结果**：补齐AMP后同kernel运行约288秒，fresh pinned日志/Android返回通过。Wi-Fi state6、64MiB DMA、DHCP和路由/DNS应用记录恢复；仍无Safari网页验收。截图1/3完整、2缺列，实际画面为桌面。只读已安装OS确认UI库c389不含源码已提供的pong_open入口，因此URL动作未部署；不能把spawn/prewarm当网页已打开。
- 下一隔离v23 COW候选只更新UI库，编译/4KiB签名/实际挂载字节与URL marker核验通过；原v23/OS370/Data保持，OS-only改变5个64MiB块，16:51 已完成OS-only安装及整卷读回，当前实际安装为 app01-ui-open-v1（UI f295ef4f…，旧 v23 仍保留回退，canonical仍OS370）。其余服务策略/卷几何不改。正在验证更新后的真实Safari URL入口；网页/视频尚未通过。


- **16:20 首轮结果**：location1114c 实际启动/约290秒返回fastboot、pinned日志2,113,179字节、Android完成通过；无panic。一张完整截图及原生Settings UI dump四次一致显示“定位服务” UISwitch on=0/enabled=1，页面仍有定位相关模态。定位开关此前未知已消除，但天气/授权/GPS未通过。早期kernel/Wi-Fi记录被环形应用日志覆盖，网络判UNKNOWN，不能按无日志记失败。
- **16:35 输入纠偏**：Safari1114b 约59秒早期返回，fresh klog state0/ramoops169B，应用验收SKIP、采集FAIL、Android返回PASS。实际ADT逐属性对照发现8个CPU丢失AMP所需cluster-type；不是已证实的截图代码故障。打包入口现默认传递AMP输入并在最终镜像前检验实际CPU E/P属性；旧好ADT通过、新坏ADT拒绝。新Safari1114c保持相同kernel/loader，仅恢复这项已知必要输入并刷新epoch/nonce，正在单轮验证，不重复原坏包。
- 已实现第一帧相对截图起点/间隔及起点截止日志，actual helper offset/overflow ASan/UBSan、Display host、精简/完整本地内核通过；新app-round-review在实际v24与本轮日志上分别保持DMA失败/缺启动证据，不自动判应用/视频PASS。下一Safari example.com独立页面，减少launchd等日志并采用相对截图；新prepared候选尚未上机，见[调研](../research/2026-11-14-main-app-repair-directions.md)。


- 用户授权目标：天气固定城市真实温度/预报/刷新；Safari 实际 HTTPS 页面、输入/滚动及一个公开视频播放/暂停、连续画面。声音和硬件解码单独资格。同步完善截图时序、完整日志、请求诊断与有界轮次；原生迁移支线保持封存。
- 监督窗口本批 24 小时，单设备轮次最长 30 分钟，deadline/唯一 owner/输入 hash/自动返回和旧 Data/OS370/备份保持。每轮真实 panic 和失败保留，不重复无变化失败，不以网页下载或进程启动代替视频播放。
- 首轮控制已完成实际 v23 OS 整窗、135 项备份存在性/错误清单、root/klog extent、manifest、UI/helper及 USB-A 前置。保持旧 v25 的 kernel/loader/OS/UI 与双时点截图，重打包新鲜 epoch；两次主机打包前置失败保留（缺显式硬件 DTB、旧 build 摘要不含 kernel 字段），修正输入后新候选 1004 字节参数通过。
- 下一为一次有界定位设置页/网络控制；先验有效页面和真实网络，再进入天气数据链和 Safari。上游媒体适配只作为研究，不直接采用异板硬件常量。

