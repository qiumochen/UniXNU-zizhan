# 白天原版 Pong 对照：共享缺口与完整配方输入

用户已授权当天持续推进原生桌面，并允许全天手机操作；原09:00禁设备轮次已解除，执行与验收见[当天计划](../plan/2026-11-14-native-desktop-day.md)。本批没有写手机或派发启动。下面统计是未修改上游工具的研究输出，不能与独立导入资格混用。

## 同一工具、同一 stock、同一组件集

控制源码为恢复的 Pong `0ee4243a101eac10a77cbc8b7b8ef77ae8ec93d7`；XnuPrelink 为 `440589b0c31625f17edf363e24b3c82c51c70bb9`。Apple 输入同为22G100，工具的默认闭包同为27个组件。上游core实际产物此前只适配两个Apple silicon主机guard，kernel/驱动源码仍使用上游配置；zizhan为当前完整板级native内核。

| 条件 | 上游 core | zizhan full | 共同未配对目标 |
| --- | ---: | ---: | ---: |
| 未修改 mapper、无额外 parent/name/anchor | 791 | 792 | 790 |
| 相同实际 stock PAC类表名称、相同精确名称参考；最终地址各自从真实内核解析 | 695 | 696 | 694 |

两组都只有上游独有1个、zizhan独有2个目标。这个结果不能把core冒充原版full，但表明大量缺口在两种构建中共享；不能把几百项都归因于zizhan驱动和板级差异。下一步是复现共享绑定输入与明确失败点。

另一个对照向原版mapper提供其可选的传播名称参考，研究缺口变为519，但实际把三参数 `vm_upl_map` 接成两参数 `ubc_upl_map`，并标成 reviewed。此结果明确拒绝用于启动。它证明既要继承上游工具，也必须保留已发现的具体错配控制，不能以零计数代替正确绑定。

## 原版 full 配方的实际复现

上游 `check-native-source.py` 对440个公开source条目与固定driver submodule通过。全套构建在独立overlay执行，只修正主机guard、实际本机路径和引用对象的真实XNU EXTERNAL_HEADERS搜索路径，没有修改Pong内核/外设实现。已实际编过通用平台、CPU、原Pong UFS和原版ath11k。

随后 full builder 拒绝缺失的 `local-inputs.json`：它要求从真实 Pong 手机以 REC 只读捕获的当前 modem slot 固件、product/slot/partition 与目录证明。本机zizhan固件不能更名为Pong捕获清单。此外原版音频builder需要真实Nothing双TFA9873 speaker/receiver CNT校准输入，本机CS35L41资料也不能替代。

这曾是原版full复现的具体输入缺口，后来已通过真实公开原厂输入补齐，full成功链接见下方结果；完整零缺口闭包仍未复现。保留各轮路径/头文件/toolchain错误和修正结果，不把输入修复说成硬件能力。公共源码中mapper接受的显式anchor、参考map为CLI本地输入，本轮限定公开checkout范围没有找到成品22G100 reviewed map；上游记录称其在本机services174做过113→0的语义复核，但未提供可直接用于本机新kernel的完整命令/输出表。

## 当前可继续的共享工作

已从旧原厂22H374内核提取ARM64 slice，仅作Apple编译名称参考；它保留6,116个符号地址。同一上游工具的完整形状/PAC-shell名称比较取得815个匹配，其中301个不在此前精确名称表、65个命中当前717项缺口。它不复制旧系统地址、硬件常量或二进制实现；正在按名字在实际新zizhan内核重新解析，并通过冲突、已知UPL/唤醒和实际闭包检查。实际批量闭包已完成：独立缺口717→652，研究缺口545；新增通过65项，没有新增未通过目标。UPL保持正确vm_upl_map三参、布尔wakeup也保持正确目标。当前继续从这份新map复跑dependent wrappers，不绕过最终closure/boot检查。

## 手机与监督

本批新鲜只读检查为Android/zizhan、root可达、USB99%、28.3°C；原已装隔离v23整窗hash、135项备份清单、root/klog extent、manifest和受控USB-A前置通过。尚未安装native系统或启动native XNU。canonical OS370、旧Data、原安装和完整回退保留。

当天5分钟心跳和有期限主机保活已建立，23:59收口。机器任务ID、日志、实际输入与下一动作保留仓库外handoff。当前新可信map为Apple名称参考批量结果，独立652项未资格；候选仍不可启动，未生成collection。无commit/push/云端CI。

## 私有输入的公开原厂来源补充

已从公开Pong vendor dump固定提交取得真实`tfa98xx.cnt`，未修改的上游音频解析器确认双功放与speaker/receiver配置；另一份stereo容器缺speaker配置而拒绝。正在取得同机型原厂modem固件供主机full对照，不把公共dump标成REC捕获，也不将Nothing校准发给zizhan。

## 完整Pong内核复现结果（12:17后）

真实Pong输入已补齐：公开同机型stock modem镜像只读提取200项QCA6490文件，manifest保留公开包来源和每文件hash，不伪装REC捕获；真实Pong vendor CNT通过双功放speaker/receiver解析；基带公开源码固定`dd0199cf3300764ed52b23f4a47761f91fab21a2`。generated host overlay只增加路径/头文件搜索目录、Apple silicon host guard和明确host-only stock来源检查，不改变内核/外设功能实现。原始Pong checkout保持干净。

原版full DEVELOPMENT/arm64e/PAC/chained/Os内核已实际链接成功，含Pong UFS、原Wi-Fi/ath11k、电源/音频/基带和软件AES；这是参考控制产物，外板常量不能在zizhan运行。

对两个完整内核使用同一未改mapper、同一PAC表/精确名字/旧Apple名字参考，原Pong full缺639，zizhan full缺640，**639项全部共同，zizhan仅额外1项**，27组件闭包一致。原版full无seeds时缺791。因而“几百项缺口来自zizhan改动”的判断被实际full对照否定；当前焦点是公共配方的参考输入与显式绑定重现。

原版可选RELEASE参考路径实测研究缺532，但它把布尔线程唤醒指向数量接口；该研究map拒绝用于boot。Pong资料记录113→0的本机语义复核尚未被当前公开输入/命令复现；不能把full编译通过报告成闭包或桌面成功。当前正用原厂Apple symbol-bearing内核作为原版reference-map输入继续定位批量映射差异。

## 完整操作数批量核对

### 15:20 当前准入口径

在上面的早期结果之后，调用者导入证书又扩展到全部同版原厂组件。KDK独立kext的undefined/import-stub名字、完整安全caller、注册类/PAC方法身份和typed allocator边界共同确认了204个typed/class API身份；调用者和完整操作数闭合把实际准入推进到**558项未闭合、研究464项**。其中typed allocator的198个注册类工厂只证明`_OSObject_typed_operator_new`这个API入口和参数描述符，不宣称allocator运行时、zone预算或对象构造已经在设备上通过。所有研究/order/string候选仍不进入发射集合。

全组件扫描中，部分KDK组件使用跨页4字节链和非零bind addend；读取器现保留显式`(name, addend)`，拒绝把带偏移导入当作普通API。AppleT8140ANEHAL的实际跨页指针、4种addend和坏ordinal/reserved/stride控制共8项通过；一次错误的“遇到跨页就直接停止”尝试保留为失败边界，不能用部分扫描结果冒充全组件成功。对完整输入重跑前，读取器会保留每个组件的实际hash和元数据范围。

资格层的实测回归也已修正：明确API身份可以替代同一目标的操作等价实现，但不能覆盖已有的真实布尔唤醒契约。故意将`_thread_wakeup_prim`换成数量接口、将三参数UPL换成两参数包装的锚点均在生成资格输出前拒绝；失败的旧分类输出保留为`REJECTED`，不作为通过证据。

同XNU的24G84 KDK实际内核与UUID匹配dSYM提供完整符号参考。原版optional-reference流程对实际Pong full与zizhan full均得到197项研究缺口，197项全部共同。已知UPL/布尔唤醒目标正确，2,329项独立名称对照和新PAC类方法对照未见矛盾，但其中9,487项仍来自reference-order，不能因缺口减少就用于启动。

新 `scripts/native18-reference-operands.py` 对实际stock与符号参考的完整函数体进行批量核对。它使用原上游PAC normalizer，并明确说明其会规范化编译器认证检查；输出不修改实际代码。双向唯一性覆盖被安全检查拒绝的函数体，不能因排除一个候选制造唯一匹配。所有外部调用、ADR/ADRP及消费者位置必须对应独立语义函数/数据契约、相同完整字符串，或此前已经闭合的完整函数体；未解析ADRP、literal load、函数外条件分支仍拒绝。旧exact-name、order/graph、仅操作等价的API别名不作本批依赖种子。

实际1,530项独立语义函数和386项数据种子、4,759项安全唯一提议，经872/72/3/0轮闭合947项；其中395项补充旧名称表，26项直接命中当前缺口。错误调用目标、错误数据地址均被完整操作数检查拒绝。通过名称重新解析实际zizhan源符号后，新map/27组件planner/准入结果为**625项未闭合、研究521项**，比此前652/545减少27/24；没有新增缺口。它仍继承此前参考表与契约，不能把947项新证明报告成整个映射都已证明。

本批还修复了导入入口对显式 `review_only` / `binding_authorized=false` 条目的拒绝。实际准入入口注入故意错误的函数与数据提议后，两个条目均拒绝，完整函数/数据资格表与未注入时逐项相同。真实selector8项控制通过，包括完整操作数参考、新旧参考冲突、错误UPL及显式withheld拒绝。新参考绑定检查stock/native输入hash，不能跨候选重用。

下一批复用现有四份source RELEASE参考运行相同核对，不重新盲编译、不按顺序补表。kernel/raw/loader未改，手机最新只读仍Android/root/boot_completed=1、USB99%/28.7°C；无collection/native boot/安装，旧系统与回退保持。新增源码与记录尚未commit/push，无新云端CI。

## 调用者导入与独立早期诊断

四份source参考及十份KDK配置的完整操作数核对已经完成，新增命中有限，停止扩大相似参考。`OSMetaClass`自身被生成元类同形表混淆的问题由实际已验证C2在首个call前写入的认证vptr解决；compiled object、真实source与stock的14槽PAC/extent一致，类表由149增到150组。另26组完整类型化元类D1实现与真实native BTI/tail→D2包装交叉印证，实际stock完整指令和所有callee/data操作数一致，确认159个内联等价析构实现。不能把base vtable的D1 trap代替D2。错误vptr地址点、PAC槽位与callee负控制拒绝；实际准入625→623→622。

`paseo-committee`只读复核发现了此前未使用的直接名源：KDK独立kext保留未定义导入、间接符号表和认证stub。新 `native18-kext-import-names.py` 在对应组件内比较完整caller，在同一规范化调用位置读取真实import名。首阶段恢复531个目标名称线索，覆盖当时622缺口中的290项；这不是可执行绑定。源码exports与真实native同址符号明确消除锁别名、memcpy/memmove、bzero/___bzero差异，不使用模糊名称规则。

严格子集另要求完整safe双向唯一caller、所有非候选callee有独立API身份、每个字符串引用在同规范化PC/producer/宽度具有同值，并由两种完整caller形状和两个组件支持。未解析ADRP/literal load/函数外条件分支与未闭合数据仍拒绝；API身份不要求整个allocator内部指令等价。首批1,158份caller证书确认13项API，其中5项新增（IOLog、IOLogv、kprintf、Block assign/dispose），实际准入622→617、研究518。错误API（布尔/数量、Data/default allocator）、指令、调用位置、data与相同字符串集合换位置等七项控制均拒绝。新证据不会覆盖已有UPL/唤醒直接契约。下一轮使用这些新独立API身份扩展caller覆盖。

调用者实际导入名还纠正了一个猜测：三个高引用目标是`IOMallocData`、`IOMallocZeroData`、`IOFreeData`，而不是default heap接口；源码exports把前两者指向对应`_external`定义。尚未满足证书条件前仍不绑定，不能沿用旧猜名。

早期诊断单独准备。原完整kernel的format7 chained fixups不能裸启动，loader接受MH_EXECUTE不意味着会消费其链。保留冻结kernel，以相同实际对象、链接filelist、库与保存的link flags作对照，除UUID外原产物所有字节一致；仅按上游公开链接模式切换ld-classic/threaded-starts，得到非空112字节入口表、没有chained-fixup命令的独立诊断kernel。真实loader parser的12项ASan/UBSan控制通过；启动容器使用维护的zizhan UEFI、硬件DTB和native EFI，单CPU、无root或Data、UFS写关闭，参数345字节。

诊断只验early entry/PAC、arm_vm、ramoops和bootstrap，不验桌面。完整kernel仍含指向未装retained组件的绝对父类地址，C++初始化是预测失败边界；地址落在loader owned window并不使其成为有效类，不能保证具体panic PC。由于预计尚未到ArmDTPlatform.configure，平台45秒deadline/guard可能根本未运行，实际返回依赖native EFI在ExitBootServices后设置的60秒watchdog和bootloader标记。一次有界临时启动后先取ramoops再回Android；不arm旧pinned klog，不重试同一已知缺组件失败。最新手机/备份/当前v23整窗hash/root+klog extent/manifest/USB-A前置已通过；此段记录准备和实验设计，尚无实机结果，不称boot或desktop PASS。

### 首次早期native实测与真实分配器断言

13:48–13:50的一轮诊断实际取得新Darwin24、9728MiB/4KiB managed memory、pmap/VM bootstrap与zone初始化console，并有v92诊断ready标记。58秒后回fastboot，RamoopsDump成功，Android `boot_completed=1` 返回通过；旧v23整窗hash、root/klog extent、manifest和完整备份清单随后只读复核通过。没有系统/root/Data安装或pinned klog arm。真实日志来自该次输入，保留原始hash；watchdog DRAM ramoops有明显位翻转，因此不宣称精确全部寄存器/PC，也不以修补文本冒充原始日志。

实际失败比预期的缺retained父类更早：断言字符串`n_zones_sig + n_zones_type + 1 <= 32`与新kernel的`kalloc_type_create_zones_fixed`符号范围对应。新 `native18-kalloc-budget.py` 解析冻结chained kernel内真实`__kalloc_type`、size/signature/name/flags及29种size class，排除data-only/VM视图，得到1,172项fixed views；以源码同一排序/前缀签名分组，编译实际预算policy运行ASan/UBSan。默认260使48B class的signature/type zones各16，加共享槽共33，确实超过固定32元素数组。这个host复现改变了下一设备变量，不需要删断言。

源码已提供`kt_fixed_zones` tunable；同一实际输入预算120需13槽。该参数仍创建真实类型隔离zones并保留全部assert，不是假成功。预算安全集不一定连续，不能用最小/最大值推导任意中间值；完整fileset增加真实组件views后也须重新核对，不能把source-only安全参数称为桌面修复。下一轮仅增加`kt_fixed_zones=120`，同kernel/loader/板级输入、无系统卷写入、有界返回，验证旧断言是否解除并取得下一个真实边界。

caller-origin证书迭代至固定点26项真实API，实际准入617→616→615→611，研究518；26并非611项都已通过。API身份允许合法不同实现，运行行为另验；研究顺序图仍不用于发射。七项新负控制中的boolean/count与Data/default标签实际是对首个memcpy caller改变请求实现来证明名字不符会拒绝，不能替代已有真实UPL/唤醒回归。两位只读顾问已对threaded诊断合法性与此身份/行为边界收敛；其“研究140残余”只是调查线索，不能放宽当前准入。

### 预算修正实机结果

第二轮于14:00:51临时启动，同kernel只增加`kt_fixed_zones=120`，60秒返回fastboot，14:02:24 Android完成。新7,651字节ramoops中旧zone断言不再出现，日志继续到security/log stream/firehose/timeslice及C++初始化；随后static-region fault，可辨LR位于本次符号表`OSMetaClass::postModLoad`范围，与预先指出的缺retained组件父类依赖一致。原始日志仍有位翻转，不能把不确定PC/FAR或寄存器当精确故障地址。它证明预算参数解除这一早期断言、让执行推进，不证明缺父类已修、平台/UFS/显示启动，更不是桌面。无root、Data或系统安装；不再重复相同source-only full诊断。

继续主机绑定前，KDK kext类表调查发现macOS保留的`RESERVED`方法槽在iOS被移除，另有少量平台方法差异。强行要求两表完全同extent得到零类表，不能把macOS槽偏移直接用于iOS。以独立class-name/metaclass注册、getMetaClass、共同base PAC及完整stock类型覆盖核对，251组表提供351个现有方法名，全部与当前证据一致，零新增；这种partial correspondence仍标为review-only，不当作完整ABI表准入。其可用价值是具体类/元类身份，下一用于caller数据操作数的独立核对，而非重新导入已知方法。
