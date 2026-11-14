# OSSymbol 字符串池依赖审查

新内核的 `_strnlen` 已独立证明后，`logfmt-closure-v3-strnlen` 的真实缺口从 718 降到 717。NoCopy 工厂仍不能恢复，因为它还依赖 `OSSymbolPool::findSymbol` 的完整 SMR/池对象契约。

源码中的 `smrh_key_t` 是 `{smrk_string/smrk_opaque, smrk_len}`；长度不超过 0x80 时复制到栈缓冲，较大字符串走临时 `kalloc_data`，随后按表的 Jenkins string hash 查找。命中后还要经过 `OSSymbol_obj_try_get`，涉及 permanent 标志或 tagged retain；超大键走带锁 huge 链表。正常路径包含 SMR hash、原子操作、线程抢占状态、PAC 字段和释放/生命周期，不能用 host 成功 stub 代替。

实际 stock 函数为 880 字节，source 函数为 532 字节。stock NoCopy 调用边界已经确认调用 stock `_strnlen`，新 source 也有唯一 `_strnlen`。source `findSymbol` 的真实调用链包括 hash、memcpy、临时分配/释放、SMR lookup、对象 try-retain、锁和原子计数；这些依赖还没有独立资格。公开 `OSSymbol.cpp` 与 `smr_hash.h` 已核对参数、长度、字符串比较和大键分支，但这属于 source 契约审查，不能升级为 PAC、调度、allocator 或对象生命周期证明。

`native18-symbol-pool-anchors.py` 现在已把真实 stock/source NoCopy 调用边界、16 字节 `smrh_key_t` 参数、KDK UUID 匹配名称和 pinned source 契约写成独立 API evidence。它明确列出 findSymbol 的实际外部依赖，但不伪造 SMR/allocator/lifetime 成功。加入 `_strnlen` 和该 API boundary 后，`logfmt-closure-v7-nocopy` 的真实缺口为 **716**，NoCopy factory 已获得一项最终导入证明；12 个全局槽位仍未恢复，因为对应 initializer 还没有独立资格。

随后 `native18-symbol-pool-leaves.py` 对实际 stock/source 的 `OSSymbol_obj_hash` 和 `OSSymbol_obj_equ` 做了 512 组真实指令回放：Jenkins seed/字节模型、相等与不等 key 均通过。`OSSymbol_obj_try_get` 只完成结构审查，保留真实 tagged-retain/SMR 生命周期前置。hash/equality leaf 加入 `logfmt-closure-v8-symbol-leaves` 后，实际缺口仍为 716，说明它们不是当前 retained branch 的新缺口，但证据已明确记录，未把 try-retain 当作成功。

`work/native18-20261114/findsymbol-contract-review-v1/review.json` 保留早期 `REVIEW_ONLY_UNQUALIFIED` 记录，说明为何不能直接把函数名当作完整行为。新 anchor 的范围是 KPI call contract，不代表 SMR 并发、allocator、PAC key 或对象生命周期。下一步应从 source/stock 中分别证明 hash traits、`OSSymbol_obj_equ`、try-retain 和小键 SMR lookup 的最小调用边界，再重跑剩余 initializer；整份 `findSymbol` 仍需真实 allocator/SMR 前置，不能仅凭 KDK 地址绑定。
