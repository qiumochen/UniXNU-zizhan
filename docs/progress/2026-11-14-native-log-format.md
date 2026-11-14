# 原生日志格式器的实际边界缺陷

2026-11-14，MIG-01 主机接续。没有手机操作；夜间设备轮次已 09:00 收口。当前 source 候选与导入状态见 [STATUS](STATUS.md)，本批在同版 KDK 线索之后核对真实 IOLog/printf 路径。

## 已复现的问题

`tools/zizhan/tests/native18-log-format.py` 直接执行实际 stock/native `__doprnt`，并使用两边实际 `console_printbuf_putc` 回调写入有限 fixture。回调不是成功 stub。外层 IOLog/printf 共同调用、数字表和已独立资格的回调交叉核对格式器入口；测试不生成 import anchor，不把 KDK 研究名称转成绑定。

首轮实际对照通过 290 个格式/数值向量，随后 `%lld` 输入 `0x8000000000000000` 失败。stock 返回 20，文本为 `-9223372036854775808`；当前 DEVELOPMENT source 进入真实 `__ubsan_handle_negate_overflow_minimal` 的 trap。独立单项复现保留了实际指令轨迹，确认不是回调替换或未实现指令被当作成功。

公开 XNU `printf.c` 在负数分支执行 `u = -n`，其中 `n` 是 signed long long。`LLONG_MIN` 的正数幅值无法用该 signed 类型表示；在本机 DEVELOPMENT 配方的 UBSan 检查下，这条路径会触发真实 fatal。不能通过关闭 sanitizer、跳过该输入或返回虚构成功解决。

## 修正与当前验收

新增设备层 `patches/xnu-ios18e/0003-zizhan-printf-unsigned-magnitude.patch`：先转为 unsigned，再用 `0ULL - (unsigned long long)n` 取得定义良好的模运算幅值。`build-zizhan-native18-core.py` 将它加入隔离 native overlay，保留 UBSan、原始 fatal 和默认 22H374 配方。公开 reference 和 Pong checkout 不改。

补丁在固定公开 XNU 上 dry-run 通过。新的 `root-candidate-logfmt-v1` 已完成真实 UUID、维护 UFS/AES/GPU/sharedSMMU/FST2/Wi-Fi/ADSP/双电池/背光/display-handoff 的完整 DEVELOPMENT 链接。实际生成并编译的 `printf.c` 包含 unsigned 幅值表达式，保留 UBSan/fatal；真实新 image 的 **836 个格式器向量全部通过**，其中包含原先失败的 LLONG_MIN。

新 strip SHA-256 为 `b8f4c4e7252df1041a24543796d3bb776f1bae1dc09f3ee05765cdcc2ae556b7`，unstripped 为 `a3065d4197976a5173aa9e79a91224c784a09f075e8d7d0333f4cbeffa185320`。新类/PAC 表重新派生为 149/175；用于实际回调的基础资格表从新 image 的符号和实际指令独立生成，没有复制旧地址。全量直接契约/导入闭包仍在重新派生，**旧内核的 705 项数字不代表新内核结果**。

测试将换行 flush 的 console_write 设为拒绝进入的真实边界；只有回调实际产生文本、计数、终止符与 ABI 均符合独立预期才通过。整个日志 sink、OS log 队列、实际 ramoops/串口、线程/中断环境尚未验证。

最初测试 helper 把回调内部分支也计入 sink 集合，前置拒绝且没有产物；修为只统计函数外调用后进入真实回放。随后真正的 LLONG_MIN 失败及全部已通过前缀保留在本机工作树，不覆盖负结果。

新证据批次的第一版 runner 把 venv 的 Python symlink resolve 成了基础解释器，导致 panic 回放缺 Unicorn；这是主机启动失败，无 anchor 输出。修为保留 venv 路径后继续，仅复用前两项已经成功且实际输入 hash 再核对的 vnode 证据，失败日志不覆盖。它不改变真实 kernel 检查或放宽资格规则。

## 新输入闭包与两项前置核查

全部证据已对新 kernel 重新派生，最终 `logfmt-closure-v2-refreshed` 为 `QUALIFIED_IMPORTS_INCOMPLETE`：**718 个真实引用目标未获独立资格，研究图缺 600 项**。与旧显示输入的缺口逐项比较，新增未通过目标恰为一个 NoCopy 工厂和它派生的 12 个全局槽位，没有其他目标变化。这是撤下尚未闭合的证明，不是 runtime 回归。

实际 producer 重跑发现两个此前未正确表达的前置：

- 时钟 producer 曾要求“独立配对 RTClockData”，但旧资格表没有该数据对象。当前改为检查完整 stock interval 模板、完整 source 换算 helper、实际 32 位 offset0 频率读取，并以 UUID 匹配 KDK 的两个 API 名称佐证。1,536 组真实指令换算/溢出回放保留；**不生成整份 RTClockData 的数据绑定**。把 source 频率读取改为 64 位会拒绝；伪造整份全局的 reviewed 提议不会改变函数证明或生成 data anchor。
- NoCopy producer 的 `_strnlen`、`OSSymbolPool::findSymbol` callee 在旧独立资格表均缺失。新输入的 producer 正常拒绝；本批撤下工厂和 12 个槽位，未复制旧 anchor。producer 现在明确只接受 `INDEPENDENT_CONTRACTS_ONLY` 的 map，真实当前研究 map 的拒绝控制通过。旧 705 项是当时实现的计数，**不能继续作为这些工厂依赖已获证明的依据**。

换句话说，源代码的 LLONG_MIN 缺陷已修复并通过真实新 image 回放；导入工程仍未完成。下一单项先补 `_strnlen` 的完整有界读取/返回契约，再处理 `findSymbol` 的真实参数、池对象与 SMR/查找依赖，条件满足后才能恢复 NoCopy 与槽位证明。IOLog/printf 的整个 logger/console sink 仍未获资格。

新 kernel placement 检查通过，实际 stock KC/新 strip kernel 的 loader parser 在 ASan/UBSan 下 15 项控制通过。仓库要求的精简 CI 配置及完整默认 GPU/Wi-Fi 内核均实际 `KERNEL_DONE`。这些是主机证据，不代表 collection、手机 boot 或安装。

新 kernel 再做一次清空所有 provisional function/data 提议的对照：完整 4,512 个函数、386 项 data 资格表与真实导入清单逐项不变，仍为 718 项未通过。公开 54 个 Python 文件编译、16 份 Markdown 链接、TOML、77 个 changed paths 的隐私/凭据扫描、14 area 增量记录和 diff 检查通过。原始失效证据、各次前置拒绝和负控制都保留。

本批新增测试/补丁/构建入口未 commit、push 或触发云端 CI。完整板级新输入仍用同一独立 raw 卷 UUID，OS/loader/已安装 v23、canonical OS370、旧 Data 和回退保持。
