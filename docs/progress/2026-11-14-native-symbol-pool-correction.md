# OSSymbol 证据降级与当前闭包修正

本次交接修正上一批解释：`findSymbol` 的调用参数和 source API 文本核对，不能单独证明完整函数的 SMR、allocator、try-retain、锁和对象生命周期；`OSSymbol_obj_hash`/`OSSymbol_obj_equ` 的 512 组 host fixture 也没有覆盖完整函数体的所有外部依赖和跨 image 操作数。因此这些证据不能进入可执行导入表。fixture 本身曾执行真实代码并通过模型，但不足以支持整个 KPI 的身份/重定位资格。

上一批的 `candidate-review-manifest-v6-nocopy.json`、`logfmt-closure-v7-nocopy`、`logfmt-closure-v8-symbol-leaves` 保留作拒绝记录，详细收据为 `work/native18-20261114/premature-symbol-pool-qualification-rejection-v1.json`。撤回：`findSymbol` API anchor、NoCopy factory 的后续绑定、hash/equality leaf 的后续绑定以及由此产生的 716 计数。最后那次 symbol leaf 回放实际包含 512 个长度/seed 字符串样例及 equality mismatch，但 map 中没有任何一个对应的 stock target 新资格；报告将该次 716 描述为一次新增 qualified-function 变化是不准确的，也由本修订明确撤销。

当前权威回退到 `logfmt-closure-v4-strnlen-final` 和 `candidate-review-manifest-v7-corrected.json`：`_strnlen` 的 1,672 组实际 bounded/对齐/guard-page 回放仍有效，独立缺口为 **717**，研究图缺 600。NoCopy factory、`findSymbol`、try-retain 和 12 个全局槽位均继续 withheld。

本修正没有改变 kernel/raw/loader/userland、已安装 v23、canonical OS370、旧 Data 或回退链；没有 collection、native boot、手机写入或新的设备观察。下一步应先从完整函数体和每个外部操作数建立 findSymbol 的递归闭包，再考虑恢复 NoCopy 与 initializer，不能把调用边界或少量 fixture 作为替代。
