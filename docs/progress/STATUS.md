# 当前进度与接续入口

更新日期：2026-11-14（UTC+8）。这是持续维护的当前任务台账；历史实验仍保留在日期文档。管理规则见 [AGENTS.md](../../AGENTS.md#progress-management)，本机路径、安装状态及进程交接见仓库外 `$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`。

## 11-14 发布检查点与路线评估

- 用户要求先总结、特性分支提交推送并评估迁移价值；当前批次为发布整理，不启动新手机轮次。分支 `feature/native22g100-arm64e`，默认旧路线和安装状态不变。
- **最新正式结果：544 项准入未闭合 / 458 项研究缺口**，来自 imported-data v4 已完成固定点。213 API/14 data caller 证书已纳入；typed-caller boundary v2 扫描已结束，227 API/14 data 尚未合并，不提前减少缺口。
- 两次原生早期内核试验均完成有界返回；第二次只改 `kt_fixed_zones=120` 越过 allocator 断言，停在 C++ 注册故障边界。**无原生桌面、无完整 fileset、无新系统安装**。旧 v23、OS370、Data/备份保护。
- 两条路线都属于 iOS/iPadOS 18：本次转向是 ARM64/classic prelink → arm64e/PAC/kernel collection。短期日用修复不以此迁移为前置；建议保留实验分支，按完整集合→真实桌面→旧功能覆盖和可测收益决定默认推广。
- 交付、共享上游阻塞、条件式人日估算及继续投入门槛见[本次检查点与评估](2026-11-14-native-checkpoint-and-decision.md)。下方按时刻保存的 558/464 等为历史；本节为当前权威。
- 发布前验证：三组 host 回归、native selector 8 项控制、输入 3 项控制、94 路径语法/TOML/链接/隐私与 ownership 检查通过。已有同运行时代码精简/完整内核构建通过；未新增手机试验。提交/推送与本 SHA CI 尚待本批完成，不能继承旧 SHA 绿灯。未完成的桌面目标继续登记未达成；下一开发动作是核对 typed-caller v2 与现有证据兼容并生成一次新闭包，随后处理真实 retained 组件接线。

## 11-14 白天新目标：原生桌面

- **15:00接续**：全原厂/KDK匹配组件仅作caller参考，完整caller/API/注册类/typed allocator证据继续推进，最新实际准入**558项未闭合、研究464**。正确UPL/布尔唤醒保留；“identity优先于operational alias”的新规则曾被实际错误count负控制击穿，失败输出已明确REJECTED，修正原boolean契约的API身份分类后两个真实冲突控制均在输出前拒绝。新的caller/API种子用于一次完整体依赖闭合，仍禁止研究图/order准入。通俗解释、两次实机结果和交接见[首次原生启动](2026-11-14-native-first-boot.md)。
- **14:02修正验证**：同kernel只增加`kt_fixed_zones=120`的第二轮跨过旧allocator断言，推进到C++类注册后的static-region fault；可辨LR在`OSMetaClass::postModLoad`，与已知缺retained superclass依赖相符，位翻转日志不支持精确FAR/PC结论。60秒回fastboot、日志取回与Android返回通过。此source-only full诊断不再重复；下一回到完整集合/真实父类接线。KDK注册类表和typed allocation证据补齐了独立API/data身份，最新桌面候选仍**558项未闭合、研究464**。当天桌面目标仍ACTIVE，未达成。
- **13:50首次native实机证据**：单核threaded诊断实际进入Darwin24/4KiB VM/zone初始化，ramoops有新console和v92ready；在`kalloc_type_create_zones_fixed`的32槽断言停止。58秒返回fastboot，RamoopsDump及Android完整返回通过，原v23整窗/extent/manifest随后只读复核通过。没有桌面、平台/UFS/GPU/用户态资格，raw日志位翻转限制保留。实际1,172个fixed views＋原XNU policy ASan/UBSan复现默认260在48B类需33槽；已有`kt_fixed_zones=120`参数需13槽，断言保留。下一仅改此参数的一次有界诊断验证，系统卷不动。caller证书已固定点26项API，最新实际桌面集合准入**611项未闭合、研究518**，研究140/290等不是可执行闭包。
- **13:40接续**：已补齐`OSMetaClass`自身14槽及26组真实元类析构包装关系，D1/D2区别保留，最高引用D2通过；最新调用者导入首批准入**617项未闭合、研究518**。KDK独立kext具名导入提供531目标/290当前缺口线索，严格完整安全caller＋逐操作数＋两种caller/两组件证书先确认13项API、5项新增；七类错误API/指令/位置/数据负控制拒绝。第二轮继续，研究线索不自动进入启动绑定。原candidate kernel/raw/loader未改。
- 单独的早期诊断候选已按同对象/档案进行`threaded-starts`重链接：112字节真实入口表、无chained-fixup命令，原chained对照除UUID外全字节一致；实际loader 12项ASan/UBSan控制通过。它不带stock组件或root/Data，只验证原生entry/PAC/arm_vm/ramoops/bootstrap；已知retained superclass绝对依赖缺失，不能当桌面候选。最新只读手机/备份/extent/当前v23整窗hash/USB-A前置通过，下一为一次有界temporary boot，真实返回依赖EFI交接的60秒watchdog，不能假定平台45秒deadline已启动。实机结果尚未观察，日志/返回分别验收，见[同日对照](2026-11-14-upstream-control-day.md#调用者导入与独立早期诊断)。
- **12:50批量结果**：完整KDK参考的原版Pong full/zizhan full研究缺口均197、全部共享；这个数字含顺序推断，不能启动。新增完整操作数核对器以1,530项独立语义函数和386项数据契约作依赖种子，4,759个双向唯一安全函数体中947项闭合，395项补充旧表名称、26项直接命中缺口；实际新闭包**652→625**、研究521，无新增缺口。错误callee/global以及显式review-only函数/数据均拒绝，selector8项控制通过。下一步对已存在的四份source RELEASE参考运行同一完整体/操作数核对，比较可信名称增量；不重复编译。仍无collection/native boot/install，主机结果不等于桌面。详见[同日对照](2026-11-14-upstream-control-day.md#完整操作数批量核对)。
- 用户已授权全天无人值守和手机操作，旧09:00设备限制解除；目标为今天zizhan原生22G100/arm64e实际进入桌面，见[当天执行计划](../plan/2026-11-14-native-desktop-day.md)。23:59收口，保留旧Data/v23/OS370/完整回退，有界临时启动。
- [原版工具对照实测](2026-11-14-upstream-control-day.md)：未改mapper同stock27组件，core791/full792、共享790；相同名字/PAC seeds后core695/full696、共享694。传播参考虽降519却复现vm_upl_map→ubc_upl_map错误，拒绝boot。真实Pongstock固件/CNT/固定基带源已补齐，原版功能代码full内核实际链接通过；同参考原Pongfull639/zizhanfull640，639项全部共同，只有1项zizhan额外缺口。原版RELEASE参考研究532但复现bool/count唤醒错配，拒绝boot；尚未复现原版零缺口。旧Apple原厂符号参考批量闭包已完成，独立缺口717→652、研究545，65项新增通过且无新增缺口，已知UPL/布尔wakeup目标保持正确；dependent wrappers复跑0新增，独立仍652。
- 当前桌面候选仍PREPARED_INCOMPLETE；本段当时为558项准入缺口，最新见顶部发布检查点；此前565/576/602/611/617/625/652/717/716及拒绝解释保留为各批历史。早期诊断实机状态、当前手机和运行进程以最新上方条目及本机hand-off为准。

## 11-14 09:00后主机接续

- [OSSymbol证据修正](2026-11-14-native-symbol-pool-correction.md)：上一批findSymbol/hash/equality证据未闭合完整函数体和SMR/allocator/对象依赖，已明确拒绝并撤回可执行绑定；当前权威恢复为strnlen-only，独立缺717/研究600，NoCopy与12个全局槽位继续 withheld。

- [OSSymbol 字符串池依赖](2026-11-14-native-symbol-pool.md)：`_strnlen` 1,672组实际代码边界回放、真实 NoCopy→`findSymbol` API contract 和 hash/equality 512组真实回放已纳入；`logfmt-closure-v8-symbol-leaves` 当前独立缺716/研究600，NoCopy factory新增一项最终导入证明。findSymbol 的SMR/allocator/try-retain/锁仍未宣称完整运行时资格，try_get保持review-only，12个全局槽位因initializer尚未独立资格继续 withheld。
- [实际日志格式器修正](2026-11-14-native-log-format.md)：LLONG_MIN原先触发真实UBSan trap，现由设备层unsigned幅值补丁修复并保留sanitizer/fatal。新完整`root-candidate-logfmt-v1`链接、836格式器向量、149/175类表、placement/15loader控制和精简/完整默认内核通过；新闭包**独立缺718/研究缺600**。NoCopy factory的strnlen/findSymbol依赖未闭合，撤下其13项旧证明；当前先补这两个具体契约。无collection/boot/手机操作。
- [同版KDK与独立函数体检索](2026-11-14-native-kdk-and-proof-search.md)：实际Apple签名的24G84 KDK与目标同为XNU11417.140.69；UUID匹配dSYM提供44,650个定义地址，命中154个未资格导入的研究名称线索，不能直接用于绑定。闭包入口拒绝研究reference，异UUID负控制拒绝。
- 资格层改为检索实际完整函数体并逐操作数闭合，消除研究提议选择对可发现证明的影响；空提议/四个错误reviewed目标的完整资格表对照相同。布尔唤醒768组边界回放与vnode_mount512组实际代码/字段/PAC负控制通过后，**当前显示修正版v8独立缺705项、研究图缺529项**。仍无collection/native boot；kernel/raw/loader未改，夜间监督不恢复，09:00后没有手机操作。
- MIG-01仍IN_PROGRESS。当前下一单项是新kernel的strnlen/findSymbol真实契约和NoCopy依赖闭合；IOLog/printf共享日志路径继续保留未资格。以下705/529、09:00与更早数字均为各批历史状态，不覆盖新输入718/600；Apple计数器/timebase硬件差异仍未资格。

## 11-14 09:00无人值守收口

- [晨间记录](2026-11-14-native-morning.md)：夜间批次已安全收口，**完整原生迁移未完成**。显示修正版完整板级内核/独立raw卷/loader/Metal用户态主机通过，最终仍710个真实导入未获独立资格（研究图523）；无collection、无native XNU boot/系统Data安装。
- 09:00最终只读观察为Android/zizhan/boot_completed=1、同boot_id、99%/28.2°C，无device writer或迁移挂载。owned Paseo心跳与主机保活已撤销并验证。原v23/OS370/Data/备份回退保持；源码累计dirty未commit/push/CI，MIG-01仍IN_PROGRESS。

- [显示消费者配对](2026-11-14-native-display-handoff.md)：补齐原生MiniFramebuffer回落CPU前调用维护scanout的真实owner回切；2/3/未知GPU结果与quarantine禁止CPU写入，不虚报latch。实际控制块10场景ASanUBSan/原版负控制、真实UUID完整native链接及placement通过；新kernel证据已重建，独立资格仍缺711项、研究图缺523项，无collection/native boot。
- [导入身份与资格门槛](2026-11-14-native-import-contracts.md)：旧参考图猜测错配UPL，普通字符串图又错配布尔/数量线程唤醒；原312/309/299等旧map解释已撤销，未boot。独立资格层强制接入闭包入口，操作等价callee/data依赖也须闭合；当前显示修正版**711项真实导入未获资格，研究候选图缺523项**。实际UPL/OSSymbol/global、parser/构造链/页几何/fatal ABI/字符串/metacast直接契约与负控制均已积累；对抗性错误目标/伪reviewed标签不能进入资格表，无可启动集合。
- [PAC/布局批次](2026-11-14-native-pac-layout.md)：外部PWF2解决完整内核嵌入固件的实际layout空间拒绝，reader测试/213设备hash/本机OS读回通过；独立raw OS/SystemOS/空白Data实际UUID/逐窗hash已验证，真实UUID完整内核19.2MB链接及placement通过，新配对仍312缺口。Android临时ELF实测correct-only7阶段PAC通过，错误modifier认证进程SIGILL负控制、boot_id不变；CPU0/1/2/3/4/6通过，5/7 affinity拒绝SKIP。无原生XNU boot/系统Data写入。
- [板级最小完整链接](2026-11-14-native-board-link.md)：维护中的zizhan原生ath11k/网络44对象kernel arm64e ABI、六网络类PAC、电池169父类PAC通过；真实Wi-Fi和ADSP/双电池/背光与UFS/AES/GPU/FST2/有界返回已完整source链接。新完整kernel已重新派生证据，闭包317缺口；操作等价实现仅最终绑定，不反馈身份推断。原生Wi-Fi helper/实际手机只读hash验证的ADSP固件也已本机OS staging/readonly读回。仍host-only UUID、无kext集合，无手机启动或写入。
- [原生应用批次](2026-11-14-native-app-seed.md)：实际bootstrap种子reader拒绝上游原生应用source路径，现已在显式实验选项下修复；ASan/UBSan及原版负控制、完整15,701项/853,669,631字节真实资源复制读回通过。229项原生目录保留188个builtins和41个可移除应用/原生迁移；独立OS扩容、失败恢复及完整readonly核验已通过，无手机/Data写入。
- 新GPU/FST2内核配对已收口；完整真实包装函数的指令/操作数/PAC核查及调用目标偏移负控制通过后，导入缺口经包装与10个元类指针别名核对由374降到352，仍无可启动集合。原生Metal桥/Turnip/维护presenter协议插件已编译及strict签名通过，继续本机OS staging及剩余真实导入识别，未进行手机启动或写入。详见[第三批结果](2026-11-14-native-os-gpu.md)。
- [第三批结果](2026-11-14-native-os-gpu.md)：独立本机OS已stage原生bootstrap/launchd/UI/服务与248项Data种子，并readonly重挂读回hash/owner/mode/strict签名通过；旧Data未使用。维护中的zizhan GPU+共享SMMU按原生编译，连同真实FST2选择的root-gpu-input-v3内核已链接；仍host-onlyUUID/无kext集合，不boot。旧root-v5配对仍缺374项；新GPU内核正重新派生ABI/配对，不沿用旧绝对地址。目标继续active。
- [第二批实际结果](2026-11-14-native-root-loader.md)：zizhan UFS＋独立软件AES＋root/Data/SystemOS hooks的DEVELOPMENT arm64e链接通过；保留本机pool/RAM/自动返回的原生格式loader完整EFI编译、9项parser sanitizer回放通过。原生bootstrap/UI编译/strict4KiB签名与同版服务/namespace overlay已准备。stock-kext导入已降至435项（正验证3项精确vnode anchors），无可启动集合；返回链root-link-v5及原生71个boot/518个on-demand服务overlay已通过host阶段，继续具体导入识别和隔离用户态准备，不停止整夜目标。
- 用户要求设定有挑战性的整夜目标并开始无人值守，已建立活动目标及[执行/验收计划](../plan/2026-11-14-native-overnight.md)：原生最小完整内核→stock-kext闭包/loader→隔离OS/Data→临时启动桌面→显示/触控/GPU/Wi-Fi/亮度/电池/保存与反馈修复。core或单驱动通过不提前结束；目标未达不能标DONE。
- Paseo同一对话每5分钟心跳已启动，持续接续到晨间期限；机器细节/唯一owner/任务日志由仓库外handoff管理。09:00停止派发新设备轮次，完成安全返回并交接；禁止flash、破坏旧Data/回退、复制其他板硬件常量。当前先推进原生存储/软件AES/平台资源最小完整链接。

## 2026-11-14 当前接续：MIG-01第一批主机交付

- 用户要求继续向Pong原生路线靠拢，当前工程交付为MIG-01。[本批记录](2026-11-14-native-core-and-inputs.md)：22G100原生kernelcache/DeviceTree/trustcache及OS/SystemOS/AppOS已准备，大小/CRC和本地hash收据保留，OS/SystemOS解密与只读版本检查通过且已卸载。此前“缺22G100输入”不再是当前阻塞。
- 新的设备层脚本可在隔离overlay上完成Apple silicon host的arm64e DEVELOPMENT core链接；约18MB、`bootable=false`，不是板级候选。真实zizhan UFS源码按原生arm64e编译并匹配190个继承PAC槽位，原生AES181/GPU168个父类接口签名亦匹配；均为host证据，无root挂载、图形、密钥或持久化新资格。
- 首次原配方的Intel-only guard失败、UFS漏EXTERNAL_HEADERS失败均保留；新wrapper仅在生成目录作明确host适配，未改上游固定源码或默认22H374运行时代码。3项范围下载控制、4项CLI拒绝控制通过；当前脚本/配置/文档未提交未push，无新CI。
- 下一最小交付：拆分zizhan原生存储/软件AES/平台资源的完整链接路径，处理Pong新电源配方中Nothing电池/TFA9873/Glyph/基带与本机的差异，再接本机电源与原生用户态。不能直接使用Pong full配方产物上机。原生kext闭包、zizhan loader和隔离Data/回退未闭合前不boot；APP-01 v25仍未派发、POWER-02前置保持。

### 原生源码可获取性更正（00:17）

- 前项所述“原生主仓库404，因此完整源码无法取得”判断不充分。旧地址及稳定ID现在确实返回404，但从公开fork网络已恢复精确原提交`0ee4243a101eac10a77cbc8b7b8ef77ae8ec93d7`；550项tree与11-13 API快照逐项一致，构建入口、patch series、board和原生迁移说明已核实。本机恢复位置与文件hash在私有hand-off及`logs/pong-native-source-recovery-20261114.json`。
- 当前MIG-01不再因源码不可获取而BLOCKED，15个原生驱动submodule也已按gitlink取得，工作树干净。剩余为额外公开源/22G100 Apple输入与zizhan适配闭包；源码获取不等于完整构建或启动候选，无新构建或手机操作。

## 2026-11-14 原生路线前置验证

- 用户连接手机并授权尝试向Pong主线靠拢；本轮不再受上一轮纯调研限制，已完成[设备与主机构建门槛验证](2026-11-14-native-alignment-gates.md)。真实Android/root可达，28.1°C，八核报告paca/pacg；v23实机整窗OS、root/klog extent和manifest匹配，135项备份文件在位，后部USB-A路径确认。
- arm64e最小编译生成真实PAC指令；当前生产loader回放明确拒绝新格式。可取得上游独立loader固定版本，其原始parser在ASan/UBSan下6个合成格式回放符合预期。均不是完整原生kernel/EFI构建或设备PAC执行资格。
- MIG-01被完整原生主仓库404及22G100输入缺失阻塞，未准备可启动原生候选。本轮未arm/boot/安装/写系统，手机保持Android；v25仍未发送，APP-01旧下一动作不因迁移话题自动执行，POWER-02前置保持。

## 2026-11-13 接手调查

- 用户要求先充分调查，暂不实施。已完成 REF-02 [上游增量与接手研究](../research/2026-11-13-upstream-resume-survey.md)：核对32个远端仓库的HEAD或实际依赖版本，重点为Pong原生22G100定位/X65、Sheng空闲负载/内存/视频、AdrenoMetal缓存，以及本轮新发现的同SM8475 diting Linux恢复/CS35L41经验。外部记录、本机已有代码与待验证方向分开，未移植、构建或操作手机。
- 主仓库入口为已发布`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`、main工作树干净；11-09发布/CI尾项已完成。本机11-10补充已纠正UEFI旧判断，用户fork文件已公开并完成本地对齐，不能继续列作“27项未公开”。
- APP-01及POWER-02原资格和设备顺序保留。研究后仍先恢复真实通道并取得定位页；本轮不执行v25，也不启动睡眠试验。进程检查未见cycle/installer/waiter或内核构建，未读手机接口/屏幕，实时设备运行态未知。
- REF-01迁移验证仍TODO。后续首选TSENS新鲜度/失效语义；POWER-02解阻后补后台恢复与交互唤醒契约；GPU先审cache-hit回收/同device shader-cache差异；后续音频增加diting CS35L41参考、蜂窝增加Pong IPA5.1/GSI3.0参考。具体来源、前置和停止条件见研究，未改变硬件能力记录。

## 当前接续（2026-11-09 交接）

- **发布已收口（09:45）**：原28个累计提交已快进并推送main；GCC测试类型修正`de1ce13`的实际自动检查通过，手动云端精简内核42544865898亦SUCCESS，disk-probe SKIP。本地双配置内核及隔离UI编译/签名通过；各SHA/首次失败和最终结果分列于[发布记录](2026-11-09-publication.md)。文档收口不重复运行时构建，源码发布不等于推广已安装OS候选，独立UEFI/旧mirror待办仍留本机交接。

- **APP-01 主线**：天气仍无温度/预报。v23r5同运行时仅移除fairplayd任务后，locationd旧MIG1203阻塞解除、weatherd按需启动，用户+部分截图确认定位提示；用户误点取消。全局开关、授权成功、GPS、天气数据源均未验收。
- **实际安装为隔离v23候选，canonical仍OS370**，未默认推广，回退完整保留。v24r1定位设置导航accepted1且Preferences启动，但截图因绝对时钟偏移过早，开关状态SKIP；该轮WiFi arena失败，不能验天气网络。
- **唯一下一设备交付**：重新核对真实接口、输入与日志准备后，以新run执行已备v25双时点截图包，取得原生定位页面状态。v25从未启动（NO_FASTBOOT、未发送）；无活跃设备任务或心跳。隔夜旧arm/温度不得直接沿用。
- **09:00交接只读观察**：adb/fastboot均空；USB后续核对为05c6:f000关机充电接口。当前屏幕/实际运行状态未直接观察，不能写成Android或fastboot。恢复真实调试接口是设备前置。
- **POWER-02保持受控支线**：须同时满足`ZIZHAN_DSI_DMA: buffer ready`与128MiB布局下显示域仍`IdentityObserved`才重新进场；此前不执行panel off、锁屏或真睡眠，不继续无结果的诊断审计轮。
- 汇总和证据边界见[本次交接](2026-11-09-weather-handoff.md)，完整轮次见[天气技术记录](2026-11-08-weather-authorization.md)及[panelpool记录](2026-11-08-panelpool-128.md)。本机工件/命令/回退在仓库外`work/CURRENT_HANDOFF.md`。这些记录不另建任务队列。

## 当前开发方向与验收安排

11-06 用户要求：面向日用重新规划，人工验收先积累。阶段设计见[日用路线](../plan/2026-11-06-daily-use-roadmap.md)。下方 11-07 各条保留当时授权与记录，不再构成接续顺序。

- 先补内屏 Wi-Fi 日用基础：温控、启动/数据可靠性、锁屏恢复/待机、网络与常用应用；随后音频及折叠，最终蜂窝与相机。完整手机资格仍需通话、短信等独立闭环。
- 11-07 用户再次授权按优先级继续无人值守。THERM-01 已完成默认关闭、有界一次的 XNU TSENS 观察及 g396 两组32路有效寄存器读数；下一步为有界多时点/新鲜度语义及真实热策略调查，仍无温控。POWER-02 已有分阶段恢复契约，BASE-01 完整主机候选保留。热负载、真睡眠、出声或折叠仍按各自前置和集中验收安排推进。UX-01 保留调查进度，优先级为 P2。
- 11-07 最新要求转入POWER-02完整调研设计：覆盖原生锁屏/自动息屏、各设备runtime PM、CPU/SoC深驻留及可靠唤醒；[完整方案](../plan/2026-11-07-lockscreen-suspend-design.md)已形成。主线建议为原生策略＋设备低功耗＋分层CPU idle，SYSTEM_SUSPEND作为需查询验证的备选；本轮只有源码、DT和Android只读调查，无电源态写入或睡眠试验。后续先做原生显示请求/bring-up隔离、事务owner模型和固件/wake/TCS只读清单；THERM-01继续作为独立前置。
- 用户随后授权按方案无人值守实施，人工验证留最后；当前[首批实现](2026-11-07-power-first-implementation.md)推进bring-up隔离、display power观察、事务模型和固件/板级只读清单。UI隔离候选编译/签名及6项实际CF主机回放已通过，未安装；其余项逐项闭环，不把候选、模型或只读查询升级为硬件休眠资格。
- 11-07 用户再次要求以完成 POWER-02 全部内容为目标无人值守推进，尽可能自闭环，人工验证积累。已按[本批记录](2026-11-07-power-unattended.md)从真实显示关闭/恢复事务继续，随后逐设备 PM、CPU/共享域与唤醒；既有基线与首次硬件试验前置不变。
- 人工与长时场景验收统一留在下方“待集中验收”表，尚无执行日期。主机检查和构建随开发完成；候选按源/输入/产物身份隔离保存，设备验证前不推广默认基线。
- POWER-01 聚焦电池与原生管理状态；原任务中的真正睡眠/唤醒拆为 POWER-02，保留既有调查和未通过结论。新增任务均为规划项，不能视为已有实现。
- 11-07 用户新增待办：后续先做一轮系统应用实际情况检查，再根据检查结果决定应用适配顺序，归入 APP-01。逐应用区分启动、内容显示、核心操作与联网能力；系统 HTTPS 通过不能替代 Safari 网页验收。此次仅登记待办，执行时间待安排。
- 11-07 用户新增待办 PERSIST-01：后续探索脱离电脑的持久启动方案，以及重启/异常退出后的数据保存可靠性。当前系统镜像已落盘，但仍用临时 `fastboot boot`，不能把镜像存在等同于独立开机能力。此次只登记，保留现有开发主线和不刷写启动分区的约束。
- 11-07 APP-01/PERSIST-01 首轮无人值守检查已完成[记录](2026-11-07-app-persistence.md)：天气 `com.apple.weather` 前台会话有多次 resumed 证据，但本轮无有效截图，且出现一次 canvas slot/IOSurface 内存不足；天气内容、核心操作和联网仍未通过，Safari/设置/时钟/照片本轮未完成。Android A/B 启动输入、userdata 内 pinned root、OS/SystemOS/Data 窗口和回退边界已只读核对；持久化探索方案形成，独立开机和 Data 跨重启可靠性未验证。
- 11-08 00:31–00:44 已存在两个未上机候选 `out/app01-panelpool-128-device-v3` / `-v5`（pool 96→128 MiB；loader 已重建且不含撤回的 `pong-dsi-command` 字符串）。v5 身份：image `94d04d5f…a4d7`、loader `1f51ed2b…b8e2`、kernel `66553850…08e3`、ADT `c11cebc3…772f`；`stage/EFI/UniXNU/pong-boot-manifest.json` 仍为 APP-01 天气参数（`pong_shot=100`、`tsa=1118`）。未启动、未记录；启动前须复核 loader 确为隔离构建（本文档记录 `dc607a1d…dbb` 为 11-07 的干净 loader，v5 使用的是 11-08 重新构建的 EFI，二者不可混用）、Wi-Fi arena 预算与回退。见[本轮记录](2026-11-08-panelpool-128.md)。
- 11-07 用户要求对照 UniXNU 设备目录寻找类似设备，先形成一轮待验证方向。已完成[首轮参考调查](../research/2026-11-07-device-reference-directions.md)，登记 REF-01；R1–R10 分别接温控、显示恢复、共享供电/IRQ、CPU idle、持久启动/Data、启动分配、折叠、音频、应用/GPU及蓝牙/蜂窝。先借 SM-T733 的热策略失效语义、Pong 的显示资源生命周期、Sheng 的共享 owner 做主机差异审计；POWER-02 主实验与现有验收前置保持。方案尚未移植/设备验证，目录及继承记录不能作为本机新资格。

## 已验证基线

- 最近完成硬件验收及四项 CI 的发布提交：`de0d977d654f054da22896de41596f4ba5610a89`。这是功能基线，不要求后续纯文档提交与该 SHA 相同。
- 默认 `configs/zizhan_ipados18_gpu_defconfig` 同时开启 GPU、Wi-Fi 和内屏亮度；旧 Wi-Fi 配置名为相对符号链接。默认测量探针、合成触摸、自动 UI 动作关闭。精简 CI 显式选择自己的配置。
- 原生 Settings 选网加入、保存凭据自动加入、开关后第二次 DHCP/HTTPS、断线路由/DNS 会话清理以及 2.4↔5 GHz 不同 BSSID 重连通过。当前接收仍为 polling，短时通过不等于长稳完成。
- L18 内屏手动亮度可用：原生滑条 25%/65%/50% 对应实际 DCS 512/1331/1024，HID 拖动有匹配硬件读回并恢复。正常 60/120 Hz 启动、完整截图和 HTTPS 通过；没有光度校准或睡眠恢复结论。
- 面板槽真实缓存映射生命周期已加固。g342 历史映射失败仍是实际故障，未稳定复现；多轮回归通过不能保证永不再现。
- display-link 实际 UIKit 类名修复有效，g298 合成 moving 95.3 fps，基线约 57 fps；不是全局 120 fps。主屏静止触摸首帧仍约 80 ms。

## 已纠正结论与撤销实验

- g369/g371 的旧 fence “差一笔”不能证明 GPU 长停。g374：等待快照 763/762，实际提交/完成 763/763；g375：等待快照 664/663，实际 665/665。已修正诊断，原 ring 协议未改变。
- g365 提前 CA flush 候选使首帧中位数从 g363 的 79.76 ms 退步至 301.68 ms，已完全撤销。没有接受新的首帧优化。
- IB-list 边界协议候选仅做过主机研究，未上机、未发布。不得把它写成已生效的修复。
- 旧 g292–g295 的 Settings/跨频段失败、旧默认 Wi-Fi 关闭、旧准备态 g298/g342 不是当前基线。

## 当前工作队列

状态：`TODO` 待开始，`IN_PROGRESS` 进行中，`BLOCKED` 有明确外部前置条件，`HOST_VERIFIED` 仅主机通过，`DEVICE_VERIFIED` 对应设备条件通过，`DONE` 本项验收完成，`REJECTED` 候选撤销。11 月 6 日获授权开始四项无人值守工作，设计及边界见[本轮记录](2026-11-06-overnight.md)；实际设备启动单独登记。

| ID | 优先级 / 状态 | 范围与验收条件 | 下一步 / 证据 |
| --- | --- | --- | --- |
| MIG-01 | P1 / IN_PROGRESS | 原生迁移主线，夜间设备批次已09:00收口。向Pong原生22G100/arm64e路线建立独立zizhan候选；完整主机构建与独立ABI闭包后，按隔离Data/回退/有界返回设计临时启动；现有能力必须逐项复验。 | [新原生内核](2026-11-14-native-log-format.md)修复LLONG_MIN真实UBSan trap，836格式器向量/完整板级链接/149类表/placement/loader及双配置内核主机通过，当前718项未资格（研究图600），NoCopy依赖13项旧证明撤下。下一单项补strnlen与findSymbol契约后重审工厂/槽位；无collection/native boot，09:00后不启动设备轮次。 |
| APP-01 | P1 / IN_PROGRESS | 保留中的应用验收；MIG-01主机批次期间不自动派发旧设备动作。逐应用记录启动、内容显示、核心操作、联网、输入及前后台表现；覆盖Safari、设置、时钟、天气、照片。 | v23r5解除旧locationd阻塞并显示定位提示，用户取消，天气仍`--`。v24r1导航accepted1/Preferences启动但截图过早，Wi-Fi arena失败；v25未发送。隔离候选、OS370回退完整，无owner/心跳。原天气下一动作保留为合适批次下取得真实定位页；不是MIG-01默认启动候选，见[诊断](2026-11-08-weather-authorization.md)。 |
| POWER-02 | P3 / BLOCKED | 完整原生锁屏/自动与手动熄屏、按后台任务进入真实低功耗并可靠恢复；静默、联网、后台任务与功耗分场景验收。支线 MVP 仍为原生显示请求→内屏真实 off→wake→可见首帧。 | 11-08 起降级为受控支线，不再产出新诊断模型或只读审计轮。解阻条件（须同时满足）：启动日志出现 `ZIZHAN_DSI_DMA: buffer ready`；128 MiB 布局下显示域仍 `IdentityObserved`。在此之前不执行面板 off/锁屏/睡眠。既有结果保留：v5 只读快照 PASS（SMR3=0x84022800、S2CR3=3、CB3 SCTLR=0xe0、FSR=0x400、11 reads 0 write）；FIFO-v2 正常 8 包 461µs、CP1836/1836；memory-v2 因 ADT `pong-dsi-command` 字段早期 FAIL 且该字段已撤回；长包 DMA 需 IOVA 而非物理地址。见[本批记录](2026-11-07-power-unattended.md)。 |
| THERM-01 | P1 / IN_PROGRESS | 本机温度传感器、真实热策略与 CPU 限频/恢复；输入来源、单位、失效处理和负载响应可核对。 | g395温度SKIP保留；按v2 VALID纠正后，g396两组2.8/32路有效 last-temperature 读取PASS（35.5–46.9°C），34项host及完整/精简内核通过。原supervisor因Android未完成记FAIL；46.224秒后有界复核boot_completed=1，返回单独PASS，未改旧记录。下一步有界多时点/更新与过期语义、映射交叉核对及vendor热策略输入；无温控或限频资格。见[本轮记录](2026-11-07-tsens-readonly.md)。 |
| BASE-01 | P2 / HOST_VERIFIED | 以现有 Darwin24/UFS 链路得到可追踪、可重建和可回退的本地日用候选；源码、OS/UI/helper、服务策略和 manifest 对齐。 | `os370-full-candidate-v3` 已按显式 OS370 服务存在性策略完整 prepare/apply/finish；source/input/prepared image/policy hash 固定，APFS readback、Data roundtrip、24项服务/runtime审计通过。完整镜像只在仓库外；未安装/启动，OS370 仍是设备基线。验收暂存；设备批次执行前重新preflight并登记变量/回退。默认策略审计仍指出 fairplayd 漂移。见[本批记录](2026-11-06-daily-use-development.md)。 |
| USB-01 | P1 / DEVICE_VERIFIED | 后部 USB-A 有界稳定枚举、目标检查、临时启动、autoreset、ramoops 取回及 Android 返回。 | g390 单轮通过，包含两次等待期记录端口 VBUS 恢复；只证明此条件的一次完整往返，不代表长期失败率或 USB-C。见[g390与下一项设计](2026-11-06-daytime-input-power.md)。 |
| UX-01 | P2 / IN_PROGRESS | 降低主屏静止首帧延迟；保留相同输入和可见内容控制，报告有效样本数、median/p90，确认改善且无内容/稳定性退步。历史控制 17 次，79.76/87.87 ms；11-06 当前 OS 控制 16 次，73.608/86.020 ms，不当作优化收益。 | g379 已取得三个真正重叠 GPU trace 的静止样本，主要延迟在提交前；下一步用有界 CA commit/编码观察细分前端，候选仍需至少 15 样本、median 改善 ≥10%、p90 不退步及独立画面验收。保留调查、排在日用基础之后；见[本轮记录](2026-11-06-overnight.md)。 |
| GPU-01 | P1 / DEVICE_VERIFIED | 罕见冷启动画布失败的持续回归与复现；每轮完整画面、映射结果、真实 CP 完成及退出状态可核对。多轮成功仅登记覆盖，故障根因另行验收。 | g380/g381 两轮有界正常启动覆盖完成，CP1843/1843、1777/1777，各四张完整主屏采样、HTTPS及返回；02:02 原始日志/PNG逐像素和macOS独立解码推翻此前残图误判，驱动诊断候选已撤回。仅本轮覆盖通过，g342 根因未解决；见[本轮记录](2026-11-06-overnight.md)。 |
| BOOT-01 | P1 / IN_PROGRESS | 诊断真实共享缓存签名存储分配失败；保留错误处理，在相同资源输入下取得可解释的分配与完整启动证据。 | 失败路径 kernel_map size/free/largest 诊断已实现，精简CI和完整默认主机内核构建通过。g393完整profile启动/返回通过，但 fresh klog 未复现CS4K失败，也没有 map summary；根因仍未知，不重复同条件。见[后续结果](2026-11-06-boot-release-followup.md)。 |
| PERSIST-01 | P2 / HOST_VERIFIED | 探索 iPadOS 持久化方案，分别覆盖脱离电脑的冷启动/重启进入、Android 共存与故障恢复，以及 Data 在正常重启和异常退出后的完整性。探索阶段验收为形成基于本机启动链与存储约束的方案比较、前置条件、风险、回退及分阶段验证计划；实际能力须另有设备证据。 | 已完成 Android A/B 启动输入、userdata/pinned root/klog extent、OS/SystemOS/Data layout 和当前临时 `fastboot boot` 边界的只读核对，形成方案比较与分阶段验证。当前不刷写启动分区；下一步先做主机 boot image 设计审计，再用临时启动做 Data sentinel 正常/异常返回测试。独立开机、Android 共存和 Data 跨重启可靠性未验证。见[本批记录](2026-11-07-app-persistence.md)。 |
| NET-01 | P2 / IN_PROGRESS | 不同 SSID/子网切换、自然 T1、长时运行分别验收；旧 session/路由/DNS 清理，新 DHCP 租约和 HTTPS 都有证据。 | g387同网有界窗口（启用限频原生HTTPS witness）28次成功，跨度27.89分钟、末端≤64.3秒、0见证失败/链路丢失、三张晚期图通过；不是无探针功耗或长期资格。helper-off对照未上机：g388/g389均传输前NO_FASTBOOT；已恢复OS370。自然T1、不同SSID/子网及helper-off行为仍待验。见[本轮记录](2026-11-06-overnight.md)。 |
| POWER-01 | P2 / IN_PROGRESS | 电池/充电真实状态、原生管理状态及 ADSP 开销；健康页修复独立判定，睡眠/唤醒已拆为 POWER-02。 | g391 ADSP 阶段1–13、电池发布、Settings 健康页诊断完成；`getBatteryHealthServiceState=-1`、`getManagementState=0`、spinner仍在，健康状态未修复。先追真实服务输入；人工页面/充电器核对留集中验收。见[本轮设计](2026-11-06-daytime-input-power.md)。 |
| IO-01 | P2 / IN_PROGRESS | 本轮选定音量实体键，验收真实 PMIC→HID/通知→原生音量动作、短按/长按/连按与释放。蓝牙、其他传感器另行资格。 | g391 已确认内核按键识别、HID 服务和 `SBVolumeControl` 路由 ready；没有物理按键事件，短按/长按/连按/锁屏留集中验收。见[本轮结果](2026-11-06-daytime-input-power.md)。 |
| AUDIO-01 | P2 / TODO | 本机 CS35L41 音频链；板级输入、启动、播放/停止/静音及恢复分段验收，麦克风输入另建链。 | 复核 live DT；先准备隔离只读身份探针，再核对固件/调音及 ADSP 路由，不能复制 Pong TFA9873 常量。见[音频审查](2026-11-03-audio-board-audit.md)。 |
| BT-01 | P3 / TODO | 本机蓝牙传输/固件与原生栈，发现/配对、HID、音频分别建链及验收。 | 先做只读资源和接口盘点，再选择有界探针；音频与待机恢复分别列依赖。当前无本轮蓝牙资格。 |
| CAM-01 | P3 / TODO | 本机相机；传感器/ISP/固件、单摄预览、拍照与 Photos 保存，随后录像和其他镜头。 | 先盘点板级输入、缓冲及原生接口，选择最小闭环；新规划项，无本轮实现或设备结果。 |
| DISP-01 | P2 / TODO | 自动环境光、光度校准、睡眠恢复和运行中刷新率切换分别验收。 | 先明确电源/传感器前置条件；当前仅承认手动亮度和两种刷新率正常启动。 |
| FOLD-01 | P2 / BLOCKED | 外屏独立 60 Hz 测试图、真实帧推进及熄屏恢复；随后外触控、手动切屏、Hall 自动切屏和失败恢复。 | 11-06 主机核对确认 DSI1、SE19 外触控及 Hall GPIO62；真机前置为人工固定姿态、采集交接和观察出光/触控，集中验收尚未安排；继续独立适配准备。见[调查](2026-11-06-fold-power-audit.md)。 |
| GPU-02 | P3 / TODO | IOSurfaceSharedEvent listener、动态壁纸及 ASTC/多任务/Photos 等更广应用覆盖。 | 单场景截图与真实完成验收；保留动态壁纸当前限制及 fairplayd 安装/全量重建差异。见 [porting.toml](../../porting.toml)。 |
| MODEM-01 | P3 / TODO | 蜂窝数据与完整手机能力；固件/SIM/注册/数据、通话、短信和待机恢复分别验收，当前未实现。 | 先做本机资源/驱动链调查及独立计划；通话依赖音频及电源恢复，不从 Wi-Fi 或注册成功推断。 |
| NET-02 | P3 / TODO | 真实 GIC MSI 接收、企业认证、SAE、6 GHz 各自独立资格。 | IRQ驱动收包/真实wake/WoW子能力先作为POWER-02的P1联网深睡前置推进；认证/SAE/6GHz仍按本项后续扩展。先审查硬件/固件及环境，不能从WPA2或polling成功推断IRQ与睡眠支持。 |
| REF-01 | P2 / TODO | 验证首轮设备参考 R1–R10 的可迁移性并筛选最小候选；每个方向记录固定上游版本、本机已有/新增/不适用部分、假设、前置、单变量验收及回退。实现与设备结果归对应子系统，不从外部记录提升本机资格。 | [首轮方案与来源](../research/2026-11-07-device-reference-directions.md)及[11-13增量调查](../research/2026-11-13-upstream-resume-survey.md)已形成，迁移验证未开始。先 R1→THERM-01 的读数新鲜度/失效回放；R2/R3在POWER-02前置解阻后接显示恢复、共享owner/IRQ。diting Linux补恢复/CS35L41输入，Pong原生X65补蜂窝参考；R4–R10仍按各主任务前置进入批次。已有Pong派生代码先查差异，不重复移植；APP-01主线和POWER-02受控支线保持，无新增合格双屏或可直接采用的XNU/SM8475深睡实现。 |
| RELEASE-01 | P3 / BLOCKED | 接入上游统一 package 模板与 action，`--check` 和完整本地构建通过。 | 已加入上游原样 `scripts/package.sh` 和 22H374→GPU defconfig 映射。当前工作区 UniXNU 缺少 `release` 命令；含该命令的审查 checkout 不含 22H374 profile，且通用 build/pack 步骤不调用 Darwin24/UFS 专用 kernel/prelink/boot 入口。外部前置为上游支持 Darwin24 自定义 recipe，随后继续接入 UEFI/local inputs、workflow 并完成真实构建；现有链可复现性由 BASE-01 独立推进。见[后续准备](2026-11-06-boot-release-followup.md)。 |

既有 `IN_PROGRESS` 表示保留中的开发/调查，不表示手机正在运行试验，也不代表新主线已开工。只保留一项设备主实验；下一次设备试验前记录实验设计和验收阈值，尚未启动的轮次不写 `RUNNING`。

## 待集中验收

本表是验收债务与前置条件，不代表每项都有可直接上机的修复包。人工操作和长时场景先积累，不再绑定 11-06 晚间；设备、候选身份和批次在执行前重新核对。

| 任务 | 当前就绪程度 | 待验内容 / 前置 |
| --- | --- | --- |
| IO-01 | 已有识别及路由 ready 证据，未有实体事件 | 加减短按、长按释放、交替连按、锁屏；与原生音量动作对应，声音联动待 AUDIO-01。 |
| POWER-01 | 读数及健康页诊断可观察，健康页尚未修复 | 页面与充电器拔插核对；管理/服务状态修复后另验，不把现状核对记为修复通过。 |
| NET-01 | 有限单网有探针窗口通过，其他场景未通过 | helper-off、不同 SSID/子网、自然 T1、分级长稳；先核对真实租约和测试网络条件。 |
| BOOT-01 / GPU-01 | 有诊断或有限启动覆盖，无根因修复候选 | 随有新输入的开发轮次保留故障现场及完整内容/完成证据，不重复不变轮次等故障。 |
| FOLD-01 / POWER-02 / AUDIO-01 | 尚有实现或前置缺口，不能直接联合验收 | 分别准备显示交接、恢复/独立返回、板级输入/音频链；首次出光/折叠、真睡眠和实际声音需人在场。 |
| THERM-01 | 默认关闭的一次 XNU 寄存器观察已通过 g396，热控制未实现 | 待多时点读数更新/过期、映射交叉核对与真实热策略；Android 4项其他温区 ERROR 保留。仅一次VALID读数，不代表校准、新鲜度、持续监控或热控制验收。 |
| BASE-01 | 完整本地主机候选已验证，未安装/启动 | 设备功能验收暂存；按集中批次冻结候选/hash/OS370 回退和步骤。原默认策略与显式候选差异继续可见。 |
| APP-01 | 天气已显示城市名与温度占位符，内容/定位/授权/天气联网尚未完成资格 | v23r5解除旧定位阻塞并显示提示，用户取消；v24定位页截图过早且Wi-Fi arena失败；v25未启动。恢复真实通道后先取得原生定位页和开关状态，再分验网络/天气。Safari仍须实际HTTPS页面、地址输入与滚动；缺前置的锁屏恢复单列SKIP。 |
| UX-01 | 既有调查阶段，未新增可验候选 | 产出后登记身份、主机验证和批次依赖，再做同候选联合日用验收。 |

## 已完成项目索引

| ID | 状态与验收范围 | 证据 |
| --- | --- | --- |
| REF-02 | DONE：接手当前交接并完成11-07之后的上游增量调查，补充此前遗漏的同硬件Linux参考；产出固定版本、证据限制、可迁移性判断及下一交付。仅研究任务完成。 | [11-13研究](../research/2026-11-13-upstream-resume-survey.md)；32仓库初筛及重点源码/作者设备记录，相关本机源码只读核对。没有移植、构建、新设备或CI资格；REF-01仍待迁移验证。 |
| NET-BASE | DONE：原生设置加入、自动加入、跨频段不同 BSSID、原生开关第二次连接及短时连接保持。 | [设置与跨频段](2026-11-04-wifi-settings-and-roaming.md)、[生命周期](2026-11-05-wifi-lifecycle.md)。 |
| CFG-BASE | DONE：本地默认构建和打包统一 GPU/Wi-Fi；当前默认也含亮度。 | [统一配置](2026-11-05-default-gpu-wifi.md)、[最终显示准备态](2026-11-05-display-and-backlight.md#最终准备态与验证范围)。 |
| DISP-BL | DONE：实际内屏 DCS 读写/恢复、原生滑条和 HID 拖动读回、正常 60/120 Hz 回归。 | [显示记录](2026-11-05-display-and-backlight.md)、[脱敏证据](../evidence/2026-11-05-display-and-backlight.txt)。 |
| GPU-MAP | DONE：保留真实面板映射、受限子描述符租用与失败槽排除的加固；长期资格归 GPU-01。 | 同上；源码 `88e5106dca99c03b9be1af02ceae5b041480a83d`。 |
| GPU-DIAG | DONE：等待快照与真实 CP 完成分开报告，有界真实 outstanding 观测及 60/120 Hz 对照。 | 同上；源码 `9d7afd134e56d14dcdd2d0862f3a18f49427b49a`。 |
| UX-FLUSH | REJECTED：提前 CA flush 候选性能退步，代码已移除。 | g363/g365 同场景控制；后续不得默认重新启用。 |
| DOC-01 | DONE：建立进度管理规则、唯一当前台账和本机交接入口，纠正旧导航中的过时状态。 | [AGENTS.md](../../AGENTS.md)、本文及仓库外本机 handoff；相对文件链接、任务 ID 唯一性、公开路径和既有四项 CI 引用核对通过，`git diff --check` 通过。本项未改变硬件资格。 |
| DOC-02 | DONE：按已有真机记录整理面向用户的功能清单，只描述可用情况与使用限制。 | [功能适配清单](../FEATURES.zh.md)，README 和进度索引已加入入口；38 项功能区分已可用、部分可用、待确认及暂不支持，保留高刷、Wi-Fi 长稳、电池健康、锁屏待机及应用覆盖边界。相对链接、状态用词、任务 ID 和 `git diff --check` 通过；仅文档整理，无新设备验证或硬件资格变更。 |
| CI-01 | DONE：CI 触发策略调整，本机双配置要求保留；记录检查自动、快速回归按相关路径运行，macOS 内核与磁盘诊断分别手动运行。 | [CI 效率调整](2026-11-07-ci-efficiency.md)；含同期新增面板 / ADSP 过滤，40 个路径场景、63 个输入覆盖、actionlint 与增量记录检查通过。`80c8deb` 四项自动检查全成功，总等待 24 秒；文档 `cc10ff9` 仅记录检查、9 秒成功；面板过滤提交 `bb3bab5` 日用回归 14 秒成功，Porting record 首次失败后已按要求补证据，复核提交 `4c21486` 记录检查 8 秒成功。均未触发 macOS，无内核或设备改动。 |

## 发布与 CI 基线

当前触发策略见 [CI-01](2026-11-07-ci-efficiency.md)：记录检查保持自动，三组快速回归按相关路径触发，macOS 内核 / 磁盘诊断只手动运行。下列旧发布记录保留当时实际触发范围；后续只跟踪本次 SHA 实际触发的工作流，未触发项不记 PASS。内核 / 驱动变更仍在本机验证精简 CI 和完整设备配置。

以下均是 **`de0d977d654f054da22896de41596f4ba5610a89`** 的已完成结果，记录于 2026-11-05 19:41（UTC+8）。后续提交要核对自己的运行，不继承该绿色状态。

| 工作流 | 运行 ID | 结论 |
| --- | --- | --- |
| Display host regressions | 42017140568 | SUCCESS |
| Porting record | 42017140516 | SUCCESS |
| Wi-Fi host regressions | 42017140486 | SUCCESS |
| zizhan macOS build | 42017140373 | SUCCESS，disk/kernel |

本地完整默认和精简 CI 内核均构建通过，显示/Wi-Fi 主机回归通过；记录结构及相对本轮改动前发布提交 `3b3c52fb5ba11b0a185703d7832fd567c94e770e` 的 area 检查通过。云端精简内核不代表完整设备镜像的硬件能力。

后续文档提交 `6d074ef09e5777cecfa23612e626ce2c3878153e` 的四项也已 SUCCESS：Porting record `42053769068`、Wi-Fi host `42053768932`、Display host `42053768861`、macOS build `42053768684`；最后一项于 2026-11-05 23:43:58（UTC+8）完成。

11-07 用户要求整理并发布 11-06/07 既有内容，见[本次发布记录](2026-11-07-publication.md)。主提交 `45c7037b5d42c203b2a624f83437bf49f29792a0` 已推送；Daily-use host 的私有DTB依赖已由 `314744f33cb10303edc78e9cbb66b4aa4409c949` 修正，该提交Daily-use、Display、Wi-Fi云端SUCCESS，但Porting record因旧network area认领全部测试而FAIL，正在收口ownership。电源测试已改人工最小FDT，隔离回放及本机67项日用host通过；修正后的发布SHA仍须单独跟踪五项CI。运行时代码与g397冻结清单一致，已有完整默认/精简内核构建通过，不重复相同构建。g397已覆盖有界Android完成等待的正常返回，没有新的睡眠cycle资格。

最终发布验证更正：`477417e2d8daf9efbb74e78dc98f40a55f8fbe96` 的 Porting record、Daily-use host、Display、Wi-Fi 与 macOS build 五项云端 CI 全部 SUCCESS，运行ID见[本次发布记录](2026-11-07-publication.md)。电源清单回归已改用人工最小FDT，本机日用host共67项通过；运行时代码与g397冻结清单一致，无新的睡眠cycle资格。

## 每轮更新与交接清单

1. 启动：记录任务 ID、baseline/dirty state、假设、控制、唯一变量、验收条件和回退；检查既有进程和真实安装状态。
2. 验证：记录命令、完整源 SHA、输入身份、实际结果和证据位置；主机、设备、CI 分列。缺失证据不填成功或零耗时。
3. 收口：更新此台账、对应日期记录及受影响的 porting areas；记录拒绝的实验和未覆盖条件。
4. 交接：本机 handoff 写明准备/已安装/已启动三种状态、回退、活跃进程、提交/推送/CI、下一条具体动作；长运行必须有 deadline 和日志入口。
5. 发布：审查公开 diff，正常提交；授权推送后跟踪相同 SHA 的所有触发工作流。纯文档变更核对链接、状态一致性和 diff，无需重跑设备或本地内核。
