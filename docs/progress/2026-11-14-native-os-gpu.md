# 整夜迁移第三批：本机OS staging、Data种子及GPU/FST2链接

日期：2026-11-14（UTC+8）。接[根卷、loader与导入分析](2026-11-14-native-root-loader.md)，目标仍按[整夜计划](../plan/2026-11-14-native-overnight.md)进行。本批没有启动手机或写入手机OS/Data，所有镜像变动仅发生于独立本机候选。源码入口为`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`加本轮dirty脚本，完整身份/日志在私有收据；未commit/push，无新CI。

## 文件系统和原生用户态

`native18-data-seed.py`从匹配22G100原始restore的实际inode owner/mode生成248项Data种子，文件总计637,239字节，manifest30,431字节、SHA-256 `f4137eb57a84ea245ce84c65122bdb1ee611c587feed3f608e11c23c182c8b80`。没有导入旧Data或合成`com.apple.migration.plist`成功状态；DataMigration仍负责其实际结果。种子当前位于OS内`/pong/var-seed`，由原生bootstrap只在独立Data挂载后按O_EXCL语义复制缺失项，尚无设备运行证据。

首次提取遇到同一镜像重复readonly attach：macOS复用了已有挂载，参考reader却假设新mountpoint，导致路径不存在。没有因此伪造空模板。新入口验证指定镜像与实际mountpoint的对应、WritableVolume/Media=false和owners=true后，直接使用该真实只读挂载的native inode接口，避免重复attach。重新提取通过、挂载已卸载。

`native18-stage-local-os.py`只接受显式candidate目录里的独立`OS.sparseimage`，拒绝symlink/原restore同文件/公开仓库内输出；本机root权限仅用于该候选APFS所有权，不提供ADB/fastboot入口。原生bootstrap、原版launchd结构化task配置、71个boot/518个on-demand服务目录、13个cryptex链接、UI和Data种子已写入该本机OS副本。旧服务配置保存在候选内`/pong/stock-config`。

写入后重新独立readonly attach，检查四个二进制的hash、root owner、0755及strict signature，种子manifest和launchd catalog也匹配。该本机OS仍缺完整原生catalog/适配服务和Metal，不称为可启动或可日用候选。独立空白Data副本UUID已另行确认，未将原系统Data交给新系统迁移；本批没有写该Data副本或分配手机pinned镜像。

## 维护中的zizhan GPU移向原生PAC

`native18-gpu-abi.py`使用当前设备层的PongAdreno源码及本机提取的A730固件，在隔离copy中选22G100服务属性和匹配的168方法IOAcceleratorES PAC祖先。保留本机DPU/DSI、17MiB槽位、128MiB pool生命周期和源映射所有权；不复制Pong的面板/内存常量或新scanout实现。

参考原生builder多选的`NativeAdrenoGpuContext`不属于当前维护配方，其Mmio helper在本机也不存在，首轮编译因此拒绝；新生成配方恢复本机实际对象选择，并单独编译实际共用的7个ArmSmmu4K helper文件，未补假函数。归档结果：

| 产物 | 字节 / SHA-256 | 资格 |
| --- | --- | --- |
| zizhan原生GPU archive | 483,952 / `0df6e7bbe4f09739026975d16aadd5e160cf0c27a0bde489b1d09e72ffb4fe3a` | arm64e/PAC父类编译通过，未执行MMIO/DMA。 |
| 共享SMMU helper | 31,176 / `7257ae7252c1a75be605120f9e21405fc7465bad8cdf52e492d3fb310f86e790` | 同本机source API的C helper编译通过，无设备资格。 |

独立root/GPU/input主机链接把这两份真实归档接入原生DEVELOPMENT内核，同时显式选择zizhan FST2 source，替换foreign Goodix的启动调用。FST2来自当前设备patch的真实SPI/GPIO/rail/坐标实现；仅省略本阶段不存在后端的旧手势诊断sampler消费者，不伪造触点或硬件资源。

首轮生成patch因缺上下文拒绝，修正上下文后第二轮链接发现参考内核新增的`pong_scanout_idle`和`native_gpu_trace_record`消费者与本机维护presenter ABI不配套。第三轮明确不选择新增idle通知和frame-trace两个patch，保留本机真实awake scanout协议；没有提供noop/stub，也没有声称锁屏/恢复资格。该协议选择还须与后续原生Metal plugin/frontend成对核对。

root-gpu-input-v3实际DEVELOPMENT/arm64e4KiB/size/chained编译链接返回0。strip18,516,232字节、SHA-256 `da2ed00df447f5c8ed5ec46a836d27681272bcbfd3100f03ec96f3b5574fcd7f`；unstripped22,316,640字节、SHA-256 `3d2cc1069234ccd483d13096019b7b5b446de3f7ad09bcf4b726c74209aba8ba`。它仍用host-only UUID，未生成stock-kext集合/boot包，未证明GPU完成、显示内容或真实触控事件。

## 原生Metal用户态主机构建

新增`native18-metal-layout.py`从匹配22G100 SystemOS的实际Metal镜像`__objc_classlist`发现唯一`_MTLDevice`，并调用固定源metadata reader检查600字节实例、598个自身方法。没有复用22H374或别机地址，读取完成后readonly镜像已卸载。

`native18-metal.py`对维护中的Mesa源码及脏补丁逐文件snapshot，保持zizhan共享地址空间与提交ABI头，生成独立arm64e/18.0/4KiB配方。首轮合法内部CI symlink被拒绝，修正为仅保留source内部相对链接后，Turnip编译、链接及strict签名通过：18,213,024字节，SHA-256 `0cf90fd212480dc40bf1a87e84ce6270cb6e463ec9cff70d1aa1305e24f2c209`。原参考/默认缓存未改，无硬件执行资格。

原生Pong frontend/objects与本机旧translator首次Rust编译出现45处接口差异，失败原样保留。按上游core `e2fdae4312d0acc3d8a52bc457101e7918095e6a`实际构建入口恢复translator `624bab3918de5b978e1101517abca2b4fa426565`和AdrenoMetal `6416a76e0a287d85afbcddda21192388460e155c`的完整30项运行时补丁，固定于native配置。原5420引用是本地merge，改用其公开上游来源；没有用公网HEAD冒充匹配依赖。

在隔离snapshot按上游顺序应用30patch，再将Pong原有有界stage summary精确适配到上游stage过滤之后，保留过滤及16,384槽环，生成diff/hash留证；没有补stub或删除缺失接口消费者。随后真实arm64e `build-std`、Rust/C/ObjC++桥、PAC边界重写、4KiB签名/strict验证均通过。桥的完整身份在本机receipt；下一步以维护中的zizhan token/evict/target presenter协议构建discovery bundle，不能直接替换为上游新generation/idle协议。

配套discovery bundle已按维护中的PongMetalScanout/PongScanoutWire/GpuScanoutPresenter构建：首轮漏copy实际wire header而拒绝，补齐后原生插件、桥和Turnip三件均arm64e、strict签名通过，仍不代表硬件可用。桥为21,296,096字节，SHA-256 `8248ae97c228dfa7c7ce83926aefd7422c077394cdeb812b0e7edb276a29873e`。上游PAC边界重写处理10,136个BLR和2,068个BR；保留的一个BR x30已核对为本函数ADR基址加无符号字节索引的内部跳转表，不是来自外部的签名回调。该检查是主机代码证据，未执行guest。

`native18-stage-metal.py`随后将匹配bundle和显式discovery策略加入独立本机OS。第一次只读前置因未将codesign实际生成的`_CodeSignature/CodeResources`列入许可清单而拒绝，尚未挂载/写入；核实封套并保留后，本机写入及重新readonly attach通过全部文件hash、root owner、mode、三件二进制及整个bundle的strict签名，先前bootstrap/UI和Data种子manifest均未变化。最终状态`LOCAL_STAGED_READBACK_VERIFIED`，镜像已卸载，无phone/Data写入。发布marker仍依赖真实CP、600B ABI、Vulkan设备创建等运行时门槛，不代表GPU渲染通过。

## 导入分析及下一步

对root-link-v5，62个vnode描述符经过唯一vdesc_name、完整scalars/五参数偏移/终止vp_offsets/NULLtransport核对，三项默认vnode函数经过完整指令和PAC契约核对，闭包从435降到377。修改一个vnop_create flags的本地负控制使该项被明确拒绝，其他对象仍独立可判，不把相似结构都接受。

进一步从真实kernel `.cpo`扫描175个简单命名类，149个完整PAC方法表与原版的精确类名/元类注册匹配；26项歧义或不匹配保持拒绝。首次stock function-starts尾部12项溢出地址使扫描失败，适配层仅扫描真实backed executable范围，和现有matcher规则一致；没有猜测溢出地址。类表主要是已有匹配的独立验证，最终root-link-v5闭包仍缺374项（343函数、31数据），改善有限。

新GPU/input内核已重新派生149项类表、vnode证据并生成新鲜配对，首版仍缺374项。没有复用v5绝对地址或让旧收据冒充新输入。

`native18-equivalent-imports.py`继续核对完整真实包装函数：要求归一化后的全部指令相同、每个外部调用/数据操作数已有独立配对或相同字符串、内部条件分支几何相同，并拒绝未消费的ADRP和未解析literal load。类型化PAC契约必须匹配；原stock普通GOT的IA/diversity-zero/address-diverse记录单独保留，不把其当作缺失的C类型签名。严格要求source也有普通GOT槽的中间控制得到0项，这反映source未通过GOT引用这些直接调用函数，不构成函数类型不兼容。

最终12个完整包装函数通过（含真实kfree/线程等待/缓存/时钟/IOKit包装），不生成替代实现；其中若干是多份完全相同且所有操作数已解析的source定义，全部候选均记录。将已配对`kfree_ext`目标故意偏移一条指令的负控制，使两项相关包装被拒绝，另10项独立保持。新`gpu-closure-v2-equivalent`闭包缺口降到362，仍INCOMPLETE，不emit集合；早期宽松v1和严格控制v2均保留，但只有完成上述核查的v3证据供后续使用。

进一步核对实际编译的常量`metaClass`指针别名：每项要求完整类PAC/注册、独立`gMetaClass`配对和真实source具名alias的完整8字节fixup三方一致。10项通过，修改IOService元类pointee偏移8字节的负控制拒绝该项而保留9项。新`gpu-closure-v3-metaclass-aliases`降至352个未解析导入，仍无可启动集合。12包装加入新map后的第二次级联得到0项，不继续重复相同分析。

此前待确认的`PE_parse_boot_arg_str`假设已明确排除：stock包装实际传FALSE，且CommandLine偏移为0x6c，不能绑定到传TRUE的str版本；尚无独立stock PE_state数据身份，因此本批未以猜测添加此项。

下一工作是：

1. 检查新内核的新鲜ABI证据与未解析导入，按实际代码/数据消费者和类型契约继续识别，缺项不emit、不allow-missing、不造stub。
2. 继续成对适配原生Metal用户态、FST2输入消费和真实板级电源/网络；本机现有OS/Data/回退仍保持。
3. 在完整stock-kext闭包、真实候选UUID/manifest、zizhan loader/有界返回和隔离Data安装方案成立前，不临时boot。整夜目标未完成。

本机细单：`local-os-stage-v1/receipt.json`、`logs/native18-local-os-readback-20261114.json`、`data-seed-v2/receipt.json`、`zizhan-gpu-native-v3-shared/receipt.json`、`root-gpu-input-v3/host-core.json`、`root-closure-v9-all-classes/imports.json`及各失败目录。该公开记录描述host结果；完整机器路径、UUID和进程/挂载归私有handoff。
