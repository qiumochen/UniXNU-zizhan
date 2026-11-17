# 主线系统应用：上游修复方向与本机选择（2026-11-14）

范围为 main 的 iPad7,11 / 22H374：天气、Safari/WebKit 视频、网络内存、截图和日志。当前执行见 [STATUS](../progress/STATUS.md)，不切回 arm64e 迁移支线。原始 GitHub API/源码响应、失败请求及主机/设备收据留本机。研究只覆盖本次指定仓库和来源，不宣称已穷尽所有兄弟项目。

## 发现和可执行方向

| 问题 | 一手资料与实际边界 | 本机采用方向 |
| --- | --- | --- |
| locationd/fairplayd 同步等待 | Pong services188 的 stackshot 明确等 fairplayd.versioned intransit port，H2 服务反复退出；该记录还保留 SafariCore CloudTabsMigrator 的空 sandbox extension 崩溃，未证明天气/Safari 全部可用 | 与本机 A2 干预方向一致。保留已安装 v23；不重复同 MIG 等待调查，不复制暂缓 migrator/清空 Data；先取页面和真实请求 |
| Safari 视频无播放证据 | Sheng 9252d9 实现 VideoToolbox paravirtualization host→Iris decoder，记录 H.264 1280×720 的 60 张图像返回。后续 e89371/b9223a 修正 HEVC 编码尺寸、persist buffer 和 decode-order output，记录范围含 macOS27.3 | 借它的分层计数与输出布局方法：服务/会话建立、压缩输入、真实解码图片、显示分别验。SM8550 VPU3/固件/MMIO/ABI 不直接移植 SM8475/18；先测试本机原有媒体栈的公开非 DRM H.264 MP4 |
| 视频误判为 decoder 故障 | WebKit 官方 iOS 媒体策略区分用户手势、muted/无音轨、playsinline 和 play() Promise 拒绝；Apple 推荐静态 H.264 MP4，并提供 HTMLMediaElement 控制 | 先取 Safari 实际页面与错误，区分 autoplay 被拒、网络/Range、格式、解码、合成。使用真实播放/暂停，连续画面或真实呈现帧证据；不以 canPlayType、poster 或下载完成当视频成功 |
| 视频 YUV 显示 | 本仓库 Metal 前端已有 surface planes/YCbCr matrix 传递源码，注释针对 BT.601/709/2020 的差异；源码存在不是当前 OS 镜像实际加载或已验收的证明 | 若有 decoded frames 却黑屏/偏色，再核对实际 runtime、plane offsets/row bytes/matrix；不先整体升级 Metal/Turnip |
| Wi-Fi DMA 申请失败 | 本机 v24 明确默认 64MiB allocation 失败，arena 是普通页经 SMMU 形成 IOVA，不能解释成需要物理连续64MiB。Sheng 新内存 bank/GPU pool 修正针对别板和 macOS VM | 先重验真实网络和失败阶段。若复现，对 allocation、prepare、mapping、物理页与 kernel VA 输入逐层诊断，不照抄别板低 DRAM bank，不盲目增加预算 |
| 截图过早及缺列 | 本机 v24 的第一帧绝对时间已超过 shot 截止值；chunked PNG 完整性检查已能拒绝缺行/列/冲突数据 | 已实现以第一帧作本轮起点，interval 以实际 capture 时刻推进，溢出饱和；actual helper 的计数器偏移和边界测试、Display host、完整及精简内核通过，待设备对照。完整像素/新鲜日志与页面时间线共同验收 |

## 固定来源

- [Pong services188](https://github.com/xz1c/UniXNU-pong/blob/0ee4243a101eac10a77cbc8b7b8ef77ae8ec93d7/migration/ios18e/services188-location-migration-board-gate.md)。原作者地址本机先前不可用，恢复 fork 的固定提交已核对；不因默认分支变化直接采用。
- [Sheng H.264 实现](https://github.com/UniXNU/UniXNU-sheng/commit/9252d988c0b668bc1f68c3e482951c8b2b33abc1)；完整 HEAD [1108068537d5396edb2c63b2f8c6f5b97b0a4bbc](https://github.com/UniXNU/UniXNU-sheng/tree/1108068537d5396edb2c63b2f8c6f5b97b0a4bbc)，重点 porting.toml video area、VtHost.cpp、IrisSession/Buffers 和 source-lock。README 请求 404，改读实际源文件与能力记录，没有把缺 README 当缺代码。
- [WebKit iOS 媒体策略](https://webkit.org/blog/6784/new-video-policies-for-ios/)。它解释策略层，不证明移植设备具备 decoder。
- [Apple Safari 视频交付](https://developer.apple.com/documentation/webkit/delivering-video-content-for-safari)。本轮先选静态非 DRM 视频；HLS/DRM 与音频完整能力另行资格。

这批方向不是外部功能的本机继承。下一设备结果应分别标记：网页可见/导航操作、媒体请求/响应、session/decoder、画面推进、播放暂停、退出和 Android 返回。天气必须有固定城市实际温度与预报，不用伪造数据或 GPS 回答推动界面。

## 本轮基建增量与实际负结果

首轮旧运行时控制取得定位页：完整采样图与四次 UISwitch dump 均为全局定位 on=0/enabled=1。应用日志覆盖早期 kernel/Wi-Fi，网络记 UNKNOWN，不因空记录报 DMA 失败。新 `app-round-review.py` 将源日志 hash、采集新鲜度、实际截图完整性、设置控件及请求/错误摘要集中到一次命令；原 v24 真 DMA 失败与本轮缺早期证据的负对照通过，应用/视频默认仍 PENDING。

新截图内核首次 Safari 候选 59 秒后返回，fresh klog 未写，只有空 ramoops 描述符；它不是 Safari 功能测试。实际旧/新 ADT 对照找出 8 个 CPU 的 cluster-type 丢失（320 字节）；直接 prelink 调用漏掉 wrapper 的 AMP 环境。新打包器默认传递它，并在生成最终镜像前检查每个 CPU 的真实 E/P 属性，明确拒绝旧坏输入。actual encoder 四项控制通过；同 kernel/loader 恢复该输入的设备对照仍须按真实结果验收，不把静态根因判为设备修复成功。

`video-witness-server.py` 是另一项诊断输入：只服务显式 MP4、随机路径和实际媒体事件；有网络 Range、播放时间、帧回调、play() 拒绝、真实 pause/resume。MDN CC0 flower 样本主机 ffprobe 确认为 H.264 960×540/AAC，两个 Range 字节对照、事件记录及服务正常超时退出通过。它仍是主机基建，没有 Safari/解码资格；LAN 测试页与公网网页分别验收，不能用 host-smoke 或页面脚本计数代替视频画面。

本轮进一步确认一项“源码已提交但运行库未部署”的差异：main 的 `34332f3` 已有 Safari URL 自动化入口，而实际已安装 OS 中的 UI 库缺少 `pong_open=`。因此独立构建并签名当前 main UI，在 v23 的隔离 COW 副本中只替换该库；OS-only delta 后完整 live OS 读回通过。旧 v23/OS370 与旧 Data 保留，canonical 不推广。后续只有真实 URL 请求、Safari 页面及网络/播放结果才能证明自动化和应用资格，不能用新库中存在字符串替代运行验证。

更新后 UI 的默认行为还包含已提交的 bring-up 隔离策略：自动测试唤醒/解锁要求显式 `zizhan_bringup=1`。首次新库轮次停锁屏，两张完整图片不是网页结果。后续按该既有开关显式选择，不更改默认锁屏策略，也不把测试唤醒当真睡眠恢复资格。

大量诊断覆盖环形区的本机根因已具体到忙碌显示每64帧的多组统计。新增默认5秒输出间隔，`pong_scanout_log_ms=0`可显式恢复原窗口输出；64帧统计仍按原窗口计算，总计数和错误/截止汇总不变。这改变日志开销，不能把前后性能数字直接作优化收益。截图计划与begin/end短标记镜像到独立DIAG，像素数据仍以完整性核验为准。

进一步核对两个 Linux 媒体项目的固定源码说明：[iris-vaapi@9852479288b088699ae0c0b4c848dbda58bf7391](https://github.com/CFM880/iris-vaapi/tree/9852479288b088699ae0c0b4c848dbda58bf7391) 针对 SM8150/Iris1，记录 seek 后 stale frame、vaEndPicture 同步和浏览器黑帧控制；[qcom-va-driver@0980a1decf26d34c9aa71cd0a4a0d25703a7a17f](https://github.com/radxa-pkg/qcom-va-driver/tree/0980a1decf26d34c9aa71cd0a4a0d25703a7a17f) 针对 SC8280XP，明确不是任意 V4L2 设备通用后端。它们支持本轮“真实输入/解码图片/浏览器呈现/播放操作分别验”的方法，但没有提供可直接套用本机的 iPadOS18 decoder。默认分支源码响应和固定 commit 单独保存，不从搜索摘要采用硬件结论。

## 本轮实际视频结果

17:10 的 video1114a 已从研究推进到限定实机资格：实际 MobileSafari 请求 LAN 页和 MDN CC0 flower H.264（960×540、约5秒循环），真实媒体事件记录到 readyState4、数千帧回调、播放时间推进；20秒暂停后两次采样 currentTime 保持，24秒继续后帧与时间推进。两张完整原始记录/PNG分别显示暂停 overlay 的花朵帧和播放中不同花朵帧，288秒后自动返回fastboot、pinned新日志和Android完成通过。未宣称声音、硬件解码、DRM、所有网站或公网HTTPS已验收。

这轮还纠正了自动化结果解释：LSApplicationWorkspace 的三个同步打开方法都返回0，但真实Safari界面/资源请求/媒体/画面已完成。因此 API 返回值是请求层观察，不能单独判网页没有打开。由可见内容及真实网络/播放器行为决定实际结果。

当前追加的诊断候选只更新实际 UI 库及 Weather/MobileSafari 的显式库依赖，保留原 entitlements 并重新签名，OS-only 安装后完整 live OS 读回通过；诊断是默认关闭的 bounded read-only UI/WebKit 观察。URL 通用回退曾覆盖 Weather，已按注册 scheme 选正确应用，设备结果仍需独立确认。

HTTPS 错误现场有 Safari.wkbundle preflightAndReturnError；只读 SystemOS 的 Info 声明 Safari 可执行而无独立文件，同版 dyld cache 能提取到对应代码。这是待定位的缓存/Bundle加载问题，尚未证明单一因果；本轮未将提取代码放入系统、更未绕过 preflight。先采实际错误文字及页面/服务事实，再选择最小修复。

本轮追加 `app-preflight.py` 为可复用只读前置：显式 trial/UI/helper SHA、备份目录、固定文件范围与整 OS hash，不用 canonical 状态冒充当前安装；实际错误 UI 哈希在查询设备前拒绝。诊断输出的 UTF-8 分块在主机按字节偏移拼接，丢前缀/错偏移明确标为不完整，避免中文错误文案在 console 高位过滤或分块中被错误解释。

Safari 显式加载诊断库的候选 f 产生 SIGILL，未进入15秒页面采样；此结果已登记候选失败，不能用来解释旧UI的HTTPS错误页，也不能隐瞒诊断本身的影响。后续先只增加真实 PC/LR/寄存器、映像映射记录，并恢复默认 fatal 重新抛出；Weather 已能输出城市无数据的具体事实。系统定位模态在 SpringBoard 层，观察范围补到上层窗口/控制器文字，仍不改授权或服务回复。新 v3 库的 OS-only 安装与完整 live readback通过，设备功能继续独立判定。

## 实际工具回归定位：Safari入口被新增加载命令覆盖

真实 SIGILL PC减本进程slide后固定为0x100000548，原 MobileSafari 的 LC_MAIN 与4字节 `__text` 恰在该处。原 load-command末端1336、入口1352，仅16字节空隙；旧ipsw新增56字节命令将末端推至1392，覆盖入口。原入口`01000014`被写成命令内容。这个逐字节因果解释的是新直接注入候选的崩溃，不解释更早的HTTPS错误页。

`add-load-command.py` 现解析真实命令、sections和LC_MAIN：不足padding或现有重叠则在调用mutator前拒绝；候选在临时副本上修改/签名，原section与入口必须完全不变。App在真实bundle签名并核验，异常回滚原exe及资源seal。默认不丢metadata；显式诊断选项才移除SOURCE_VERSION/FUNCTION_STARTS/DATA_IN_CODE三个可选命令，以释放header空间，保留UUID/平台/entry/segment/section/import/export/fixup/signature。实际原Safari无该选项拒绝且SHA不变，损坏文件拒绝；显式候选所有payload字节与入口保持、严格bundle签名和幂等通过。安全候选的本机安装及整卷读回已完成，设备h仍需独立验收。

## 后续实际内存阻塞

Weather g 采样时重现 Wi-Fi 64MiB DMA allocation failure，因此不能从 weatherd 的 dispatch/workq 空闲栈推断天气网络正常。随后只增加失败观察的 0022 补丁，实际记录 16MiB 分配选中的 range5/KMA_DATA 大对象范围约773.6MiB，失败时 free1,767,936、largest1,765,376、39个覆盖条目；这是真实地址范围压力，不是 whole kernel_map 或物理RAM总量。

本机启动布局同时预留24GiB zones和约10GiB compressor VA，剩 kmem_data_range约884MiB。XNU已有 `vm_compression_limit` 页数参数；当前单轮控制设1,048,576个4KiB页（4GiB压缩池上限），观察实际DATA范围和分配，不取消压缩器、不关闭签名/安全范围、不返回假成功。它降低未来压缩容量，未做内存压力/长稳资格前不会默认推广。

新的失败范围统计仅在真实find-space失败后限次读取map，无策略改变；实际补丁helper ASan/UBSan覆盖裁剪/空图/稀疏/高地址，精简及完整内核链接通过。新网络观察器和默认关闭的真实HID Command-L/URL/Return输入候选已编译，均未因主机通过而宣称设备请求/输入成功。

Safari 长文滚动 m 已按真实页面/输入/画面闭环：RFC9110 height172229，单次有限竖向HID，read-only scrollY从0到1493并保持，两张完整截图显示不同段落，Android返回通过。同轮仍有Safari.wkbundle warning，所以不再追逐该warning作为假root。Weather的两类NSURLSession completion接口未命中时，只记接口覆盖边界；下一扩到delegate-task状态和原NSXPC服务/错误转发，回调/error/proxy实际同一对象的host sanitizer检查通过，尚不当服务成功。

后续实机 j 验证可选静止截图读取最后已锁存的pool buffer，完整PNG通过，0表面/范围失败；这不是新帧成功或性能资格。Weather的completion工厂未命中，因此扩到delegate task只读状态和原NSXPCConnection连接名/代理错误，仍转发原对象/回调，不合成服务。单批UI-only OS增量安装后完整live读回，旧Data及所有回退保留，下一具体请求证据决定修复。
