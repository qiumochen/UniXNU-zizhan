# 原生导入：时钟计算与实际全局初始化契约

日期：2026-11-14（UTC+8）。继续[真实UUID候选](2026-11-14-native-pac-layout.md)的312项缺口；本批主机分析不改变kernel、raw卷或手机。源码入口仍为`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`加dirty脚本，无commit/push/CI。没有原生XNU boot或系统/Data安装。

## 配对身份错误及更正（本节优先于下方历史计数）

**最新结论：显示修正版独立证据层仍有710项真实导入未获资格，研究候选图缺523项。** 两个数字含义不同；只有独立证据层可用于将来的重定位。raw卷/Metal/loader保持，新kernel已重新派生证据，仍没有可启动collection。

后续直接page-store、fatal ABI和字符串契约的结果见下文；714为资格层首次收口检查点，最新精确计数以台账和本机最新qualified receipt为准。下方312等历史结果均已被本节的纠错解释取代。

显示修正版的`display-closure-v2-qualified`已完整结束：新kernel对应149项类表、vnode及各组直接契约均重新生成，711项未资格/523项研究缺口。操作等价wrapper生产者改为读取独立qualified基础map，并在资格层再次要求每个mapped-callee/data递归闭合，不能借研究图的猜测自举；这解释了与旧708/510检查点的口径及输入差异。最终结果仍`QUALIFIED_IMPORTS_INCOMPLETE`，不生成collection或启动手机。

其后由26个完整类注册包装佐证真实公共metaclass构造ABI，并核对实际实现字段store；size输入寄存器改动的负控制拒绝。公共构造及这26个完整literal/super/size/authvptr包装仅作为最终行为契约。最新`display-closure-v4-meta-wrappers`为710项独立未资格/523项研究缺口，未执行注册或生成collection。

核对kernel_map实际使用者时，发现旧表把stock三参数`vm_upl_map(map,upl,addr)`绑定到了两参数`ubc_upl_map(upl,addr)`包装。原包装会加载kernel_map并调用三参数实现，该误配可能导致错误递归/参数错位。这是具体错误，原312/309及随后299等计数不能作为可启动性证据；已在各旧目录加独立rejection记录，保留原始结果，不静默改历史数字。没有输出collection或在手机启动该配对。

根因是普通RELEASE的调用图猜测先被name-reference导入成`reviewed`，而LTO参考实际上已有正确完整代码身份；处理顺序使正确候选无法替换先来的猜测。`native18-closure.py`现在只接受完整代码命名，多个exact参考若对实际source目标有冲突则拒绝；图/string/order候选仅进审计文件。关闭弱RELEASE顺序传递，清除图派生reference数据身份，避免其以收据或方法标签冒充独立审查。

实际四参考UPL回归以及exact/weak顺序互换、exact冲突、纯graph和缺失source名称六项控制通过。重新配对的`candidate-closure-v5-qualified-references`为628缺口，正确指向实际三参数`vm_upl_map`；数量增加反映不成立的身份被撤回，未更换kernel/raw卷或缩减真实依赖。

之后逐项恢复有独立实际契约的接口：

- 两参数UPL包装只接受完整参数搬运/单一global读取/已独立配对tail callee，且实际编译的`ubc_subr.c`必须明确声明调用`vm_upl_map(kernel_map,upl,dst_addr)`。从全部source短包装中核对唯一具体global角色后，联合确认`ubc_upl_map`和kernel_map。改stock参数搬运或把source读取改成ipc_kernel_map的两个负控制均拒绝，避免重现相同的参数/递归问题。
- CStringNoCopy工厂不从调用图名字直接恢复。检查有界strnlen/符号池lookup参数，实际完整PAC类证据的OSSymbol元类分配，以及紧接的无条件NoCopy/length字段和原字符串指针PAC store；不能仅用与复制工厂共有的查找prefix。NoCopy位和字符串认证discriminator两个改动负控制拒绝。
- 在该工厂实际契约成立后，三组具名初始化区域的literal→return-store恢复12个DT/IOService通知/platform action键槽位。该结果重新从更正后的配对生成，不复用旧“reviewed”标签作为证明。

新一轮仅使用以上独立证据和既有完整类/vnode契约，继续计算可信缺口。后续每个依赖旧map的包装/数据anchor都须按更正后的实际map重新生成；旧count不是恢复绑定的理由。

### 独立导入资格层

继续审查发现，原matcher即使移除了弱reference命名，运行时的普通字符串/调用图也会把内联了布尔转换的stock `thread_wakeup_prim`候选指向`thread_wakeup_nthreads_prim`。旧stock会把`one_thread`转换为1或UINT32_MAX，本机后者直接消费数量，二者不能互换。这证明“未解析项已少很多”不等于“其余配对可执行”。

新增`native18-qualified-imports.py`，将完整class/PAC、独立direct API/data契约和一致exact-code reference身份作为证据种子；后续仅接受完整checked指令、全部已证明调用/数据操作数的递归闭合。纯计算/操作等价实现只保留最终行为资格，不作为其他函数命名。其余graph/string/order提议继续保存在研究map里，但不进入可重定位导入表。

`native18-closure.py`已自动运行这层：研究map显式标记`PROVISIONAL_MATCHER_CANDIDATES_ONLY`，独立map和真实retained branch/fixup覆盖在`qualified/`。`candidate-closure-v11-qualified-gate`结果为`QUALIFIED_IMPORTS_INCOMPLETE`：3,830函数/386数据通过该层规则，714个实际引用目标未获资格；候选图统计510，不能填作合格闭包。page-geometry bootstrap的完整身份也尚未在该层通过，独立inventory直接枚举实际引用，但未绕过planner的布局/页几何生成前置。

对抗控制故意把UPL提议改回错误包装，独立证据纠正并记录冲突；把错误线程唤醒候选的方法标签伪装为`reviewed`，该候选仍被扣留。三个控制实际执行qualification工具，结果通过；kernel字节未修改。这一批用于修复证据链，未缩减真实依赖或用stub填洞。

### 从实际源代码恢复的计算与对象契约

`native18-bootargs-replay.py`从完整FALSE/TRUE包装唯一共同tail定位原版内部parser，执行实际stock/native A64及单独host编译的实际公开C实现。1,042项数值/字符串/布尔/缺失/截断用例的返回值和128字节输出完全一致；把实际native未找到返回flag改为成功的负控制拒绝。host回放仅抽象PAC指令/认证分支，所有回调仍跳入实际镜像代码，不模拟成功helper，不证明密钥。

`native18-bootargs-anchors.py`新增直接字段模式：独立扫描唯一的initialized/bootArgs/DT基址/长度六指令store块，确认传入boot_args参数，再用本次pe_init实际编译参数验证七项source `offsetof`断言。首次误加32位arm boot.h触发重定义失败，去掉额外头、保留架构正确的pexpert.h后通过；改变实际stock bootArgs字段偏移的负控制拒绝。该模式不依赖旧graph中的PE_init_platform命名，source helper、FALSE参数和内部parser共同核对后恢复相应字段/包装。

`native18-constructor-anchors.py`以完整已核对的类PAC表及registration为身份，核对stock固定count=1/cda1认证vptr store体，并执行实际source C2及全部基类构造链。15项、240组随机原始对象内容/对齐/被调用者保存寄存器输入通过；除最终实际类vptr和引用计数外的对象1024字节全部保持。把真实OSObject基类计数改2，15项全部拒绝。这些构造函数只作为final import行为，不反馈图命名。

### 页几何、fatal ABI及基础字节循环

`native18-page-anchors.py`直接在全部stock函数中寻找唯一的MOV14、CONST/writable两个32位store，要求APFS实际fixup消费第一个变量。source具名PAGE_SHIFT_CONST/page_shift_user32必须有完整MOV12/store对，并用本次arm_init编译参数检查ARM_PGSHIFT=12、runtime存储宽度和非16KiB配置。没有为映射数据而虚构arm_init函数身份。最初把runtime PAGE_SHIFT当常量断言失败，随后针对实际macro/变量语义修正；两个失败compile目录保留。改变stock store宽度为64位使检查拒绝，独立资格层记录direct-page-store资格。

`native18-panic-anchors.py`检查唯一完整变参panic wrapper及实际source同API的va_list、format、caller和零选项准备，由不同自报名字的fatal wrapper及真实stack_protector.c路径佐证。64组实际指令捕获只执行到真正fatal trap边界前；不运行/替换/返回/压制panic。改变source panic选项0→1拒绝整组anchors。栈保护失败路径核对相同file/line/格式、同一真实panic和无返回；stock末尾不可达UDF padding不作功能代码。两项仅final import资格。

`native18-string-anchors.py`核对完整strcmp/memcmp标量循环模板；post-index load与分离的指针递增只改变不再被调用者观察的参数寄存器。各65,536种首字节组合以及256项长前缀/长度场景实际执行stock/source，与独立byte模型一致（每API65,792项）。strcmp明确匹配本kernel实现的signed-char比较语义，不据此宣称userspace libc行为。strlen要求完整相同向量循环和32字节mask表，只去掉RETAB后、无控制边进入的UDF对齐；232项长度/16对齐/下一页unmapped边界通过。反转比较运算或修改mask字节的负控制分别拒绝，不以hash一致代替行为控制。

这些新增API进入独立资格层后重新统计真实引用；它们的高引用次数不等于许多不同API同时完成，也不授权在剩余未获资格导入上使用图猜测。

## 时钟三个接口

`native18-clock-anchors.py`针对三个具体接口核对完整指令契约：`clock_interval_to_deadline`、`clock_absolutetime_interval_to_deadline`、`ml_get_hwclock`。stock前两者把时间基址读取、换算和饱和加法内联；实际source保留真实helper调用及DEVELOPMENT诊断。早期写成“独立已配对RTClockData”的前置在后续重派生时没有成立，已按[后续记录](2026-11-14-native-log-format.md)改为完整模板的32位频率字段契约及KDK名称佐证，明确不绑定整份数据对象；命令须提供`--kdk-research`。原始失败和新负控制均保留。

平台计数器不是相同原始寄存器：Apple原版使用有序counter指令，SM8475的ARMv8.2实现为ISB＋CNTVCT。主机回放把这一次硬件读取和TPIDR线程指针接到明确fixture，其余算术/栈/调用执行实际stock/source A64指令，不提供替代helper函数。Unicorn只用于算术契约，PAC prologue/return被显式抽象，不代表密钥或timer IRQ硬件验证。

1,536组case覆盖多种频率（包含19.2/24MHz）、64位CPU base/counter、32位interval/scale及饱和溢出；两边真实代码结果均等于独立整数模型。source的正常awake路径保留原可选timebase validation，不抑制真正诊断panic。反转source饱和条件的负控制被实际整数结果拒绝；去掉ISB屏障的负控制被完整有序counter契约拒绝，均不输出anchors。

这三个绑定只在图推断结束后加入final imports；`candidate-closure-v3-clock`实际缺口由312降到309，仍INCOMPLETE、无collection。

## 字符串工厂到全局槽位

剩余BSS符号不能从邻居位置或某个GOT条目顺序推导。`native18-interned-global-anchors.py`改为追踪已经独立reviewed配对的实际初始化函数：同一字符串在x0中紧邻同一已配对intern/plane工厂调用，随后只允许不改x0的地址计算及其返回值store；必须在paired initializer内唯一，且source store目的地址对应真实唯一具名global。

跨初始化函数的证据若相互矛盾会拒绝，不以先见结果盖过后续差异。没有复制对象值或构造初始化成功状态，只确认原kext应该引用source初始化的哪一个实际槽位。

10项通过：DT phandle键、IOGeneralInterest、IOServicePublish/FirstMatch通知、IOAllCPUInitialized，以及Sleep/Quiesce/Active/HaltRestart/Panic五个platform action键。已知Wake键不重复计算。改变Sleep字符串首字节或把其实际global store从x0改为x1，分别使该项被拒绝、其余9项保持；证明检查约束了真实键和工厂返回值，而非仅靠初始化函数名。

下一步将这批有效数据身份加入真实候选的新配对，检查其是否帮助解出其他实际使用者。字段偏移仍需具体核对，unsupported状态仍保持；不通过allow-missing、通用stub或删除实际依赖生成collection。

本机证据：`root-candidate-volumes-v1/clock-anchors-v1.json`、`clock-negative-results-v1.json`、`candidate-closure-v3-clock`、`root-candidate-volumes-v1/interned-globals-v2.json`及`interned-global-negative-results-v1.json`。原机器码、完整输入地址/日志/异常控制镜像只留本机。
