# 同版 Apple KDK 与独立函数体检索

2026-11-14，09:00 夜间批次收口后的主机工作。MIG-01 仍 IN_PROGRESS；本批没有设备操作，也没有恢复夜间心跳。当前队列见 [STATUS](STATUS.md)，09:00 的实际设备观察和回退状态见 [晨间记录](2026-11-14-native-morning.md)。

## 交付与验收边界

保持显示修正版完整内核、三份冻结 raw 卷和 loader 不变；只推进实际导入资格。整合后的 `display-closure-v8-vnode-mount` 为 `QUALIFIED_IMPORTS_INCOMPLETE`：**705 个实际引用目标未获独立资格，研究候选图缺 529 个**。没有生成 collection，没有 native XNU boot 或安装资格。研究计数变化不能代替独立资格。

| 本批增量 | 实际验收 | 尚未证明 |
| --- | --- | --- |
| 布尔线程唤醒 | 完整 stock 216B 与 source 布尔包装/数量 helper/hash helper 模板；768 组实际指令调用边界回放与独立公开 hash/数量模型；错误数量 API、布尔条件变异负控制拒绝 | 没有运行 waitq 修改、创建线程或验收调度唤醒 |
| 独立完整函数体检索 | 从实际 code 构建候选池，继续核对完整控制流和每个外部调用/data 操作数；不再只检查研究图挑中的目标 | 等效实现不获得 API 身份；未知指令、未闭合依赖或硬件行为不获资格 |
| 资格层对照 | 清空全部研究函数提议；另把四个 UPL/唤醒目标改错并标为 reviewed。两种对照与原始输入生成的完整函数/data 资格表及真实导入清单完全一致，矛盾记录保留 | 不能从 reviewed 标签、图猜测或一次下降的数字推定 ABI 正确 |
| 同版 KDK | 下载镜像大小/SHA-256 与发布摘要一致；包签名为 Apple Software；只读挂载并解包，未执行安装器；内核与 dSYM 的 CPU/subtype/UUID、已有导出名逐项一致 | macOS 配置、页大小、硬件寄存器与 iOS/zizhan 不因此兼容 |
| vnode_mount | 两边完整字段/空值/PAC/编译器检查模板，512 组实际代码对象/返回/ABI 回放；字段偏移和 PAC discriminator 变异均拒绝 | 没有 vnode 生命周期、root 挂载或硬件 PAC 成功资格 |

## 修正资格检索对研究提议的依赖

前一显示输入的布尔唤醒直接契约使独立缺口降为 709。以该独立 map 派生 `_wakeup`、`_IOLockWakeup` 的完整包装后，研究阶段原先错选的 provisional 目标由最终契约覆盖，并记录矛盾；新复跑却出现 710 个未获资格目标。

逐项比较发现，资格层的递归操作数检查虽不接受研究标签，却仍只尝试研究图提供的那个 source 函数体。提议变化后，有些已经能证明的等效函数体不再被检查。新实现按实际完整 instruction shape 建池、确定性排序，对所有候选继续进行原有操作数证明。纯计算 leaf 同样从实际 image 建池。完整等效实现标记 `semantic_identity=false`，不会把它们的函数名当作新的身份推断。

`display-qualified-v7-body-search` 和整合后的 v7 均为 706 个未获资格项。相对 v5 没有新增未通过目标；新增通过的是两个真实唤醒包装及 `_uiomove`。空提议/四个伪 reviewed 目标对照的完整资格表都为 4,512 个函数和 398 项 data，逐项相同。六项现有 reference-selector/真实 UPL 冲突回归仍通过。

## Apple 编译的同版参考确实存在

来源为 [Dortania KdkSupportPkg](https://github.com/dortania/KdkSupportPkg)，[24G84 发布](https://github.com/dortania/KdkSupportPkg/releases/tag/24G84)。仓库 README 给出的 manifest 在 `gh-pages` 分支；最初猜测的 `main/manifest.json` 返回 404 不代表发布不存在。

`Kernel_Debug_Kit_15.6_build_24G84.dmg` 为 924,011,441 字节，SHA-256 为 `3bb6e83abbd5514c2b0ff3be56915ab6c7b18ed358a0db00063581c192b86d4d`。真实 T8103/T8132/VMAPPLE RELEASE 内核字符串都是 Darwin 24.6.0 / `xnu-11417.140.69~1`，与目标 iOS 的 XNU 版本一致。T8103 kernel 的定义符号地址为 6,649 个，配套 UUID 匹配的 dSYM 为 44,650 个。

新 `native18-kdk-reference.py` 从实际完整函数体取得 4,763 个唯一双向 PAC-shell 形状及相同字符串集候选。其中 154 个命中当时 706 项独立缺口，152 个不在此前 exact-reference 名称表中。包括 IOLog、typed operator new、printf、锁和时间接口。这些是名称线索，**不是 154 项通过**；输出明确为 `names-only-research`，闭包入口拒绝把它当作 exact-reference。另一个真实同版 dSYM 的 UUID 不同，负控制在扫描代码前拒绝。

时间接口说明边界为何必要：stock `mach_absolute_time` 的完整代码读取 Apple 专有计数寄存器及线程字段，而当前 zizhan source 有自己的计数器/单调性路径。名称和版本一致不能证明数值原点、timebase 或迁移语义，当前仍未纳入资格。

## 从线索落实一个真实接口

stock `_vnode_mount` 完整 44B；当前 source 完整 60B。stock 是字段加载再计算签名槽位地址，source 使用同偏移的 load-with-writeback，并保留编译器额外认证检查。两边都读 `vnode+0xd8`，使用 DA key、address diversity 和 discriminator `0xd6ef`；公开 `kpi_vfs.c` 的该函数直接返回 `vp->v_mount`，Apple KDK 完整唯一函数体佐证名称。

新 `native18-vnode-accessor-anchors.py` 固定核对两份完整模板；512 个有效对象 fixture 覆盖空/非空 mount 字段和不同对齐，真实代码返回值与直接字段模型一致，对象字节、SP 与 callee-saved 寄存器保持。host 抽象仅跳过 PAC 运算，原始 guest 认证与失败检查指令仍必须存在；未伪造认证成功。字段偏移或 PAC 类型改错均不生成 anchor。

首次 helper 对未过滤的 function-start 表读到无后备地址，失败记录保留。修正为只读取实际 file-backed executable 范围的完整函数后通过；不是忽略读取错误继续生成证据。加入这一独立契约后 v8 为 **705/529**，仍无可启动集合。

## 复现入口与下一交付

维护脚本为 `native18-wakeup-anchors.py`、`native18-qualified-imports.py`、`native18-kdk-reference.py`、`native18-vnode-accessor-anchors.py` 和 `native18-closure.py`。所有实际输入 hash、命令、原始输出、对照及未通过记录保留在仓库外工作树；Apple 二进制、地址表和 fixture 不进入公开仓库。公开脚本只读输入、输出分析证据，不操作手机。

下一动作从 KDK 已命名的高引用缺口中选一个具体 API，优先核对 IOLog/printf 共享日志路径或 typed allocator 的参数、对象大小、回调和真实依赖，形成完整直接契约及负控制，再用同一实际输入重跑资格闭包。时间接口保留独立硬件/timebase 前置。不得直接导入研究 map、复制 Mac 地址或硬件常量，也不因主机缺口下降自动启动设备实验。

本批属于分析脚本与主机回放，kernel/raw/EFI bytes 没有改变，不重复内核编译。未 commit、push 或触发 CI。原安装 v23、canonical OS370、旧 Data 和全部回退链保持；09:00 后实时手机状态未重新观察。
