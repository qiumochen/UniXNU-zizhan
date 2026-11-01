# 2026-10-31 晚 → 11-01 凌晨：回路修复、DPU 翻页、零拷贝三缓冲（详细记录）

本文把 10-31 21:00 到 11-01 00:05 的工作完整写下来：起因、每一轮的配置与结果、根因、最终的数据通路、所有开关与默认值、数字、教训、遗留和下一步。按时间顺序的流水账仍在 `2026-10-31-frame-timeline.md` 第 9–10 节；本文是整理版。

---

## 0. 一句话结论

- 显示管线最后一段"GPU 把 CoreAnimation 的画面再拷一次到面板缓冲"（每帧 2.5 ms，和渲染抢同一条 GPU 队列）**已经彻底去掉**：DPU 现在直接翻到 CoreAnimation 自己的画布，三缓冲，每一帧都被显示，无撕裂计数。
- 这套配置**已是内核默认值**（`pong_dpu_flip=1 pong_dpu_bufs=2 pong_zc=2 fb_swapwait=1`），新一轮什么都不带就是它。
- 顺手修好了"手机进 iOS 就出不来"的根本问题（四条回路：默认 300 s 到点复位、panic 复位、长按电源 3 s 复位、周期性 klog），并把两个会让整机冻死的坑写进了规则。

---

## 1. 起点与误判的纠正（21:00–21:30）

### 1.1 上一个会话留下的状态
- 10-31 下午把 DPU 双缓冲翻页（iOSPlus10 0269 的设计）写进了内核 `PongAdrenoScanout.cpp`（dbuf：`pong_dpu_flip`、`pong_dpu_bufs`、`pong_dpu_flip_test`、`pong_dpu_flip_klog`），但连续四轮（g152、g152b、g152c、g152d）"硬挂"：420 s 不回 fastboot、踢 USB 口无效、看门狗/守卫线程都救不回，每次都要用户长按电源。
- 上一会话据此得出"GPU 经新映射工作会引发总线级冻结"的结论，并暂停了 flip 实验。

### 1.2 真相：四轮都没带 `pong_autoreset`
- 复核 `logs/g15*.build.out`：g145–g151 的命令行都有 `pong_autoreset=300 pong_autoreset_sb=30`，而 g152、g152b、g152c、g152d **一个都没有**（`round.sh RUN 参数…` 时漏了）。手机端 launchd 打印的 boot-args 也证实。
- `PongBootReset.h:201`：没有这个参数直接 `return`——pinger、deadline 复位、守卫线程、deadline 前的 klog 刷盘统统没装。于是"进去就出不来"是必然：XNU 这个移植没有 USB 设备栈也没有网络，iOS 跑着的时候 Mac 对它完全没有通道，唯一的回路就是内核自己定时复位。
- 证据链：g152c 的 UFS klog（21:06 用户复位后读出）显示 `first GPU copy … offered → GPU frame completed sequence=1 → retired → actual GPU scanout copy completed`，之后无内容——因为没有 deadline 刷盘，正常运行也会这样。g152d 的 klog 更进一步：`64 GPU copies through the two-range mapping done`、DCVS 10 s 报告在刷。**GPU 经两段 31 MiB 物理映射工作完全正常。**
- 撤回"总线级冻结"的推断。用户 19:15 看到的"定在 iOS 桌面"也可以是正常运行（mode 2 若 flip 没生效，屏幕一直显示缓冲 0，看着像定格）。

### 1.3 带看门狗重跑（21:52–21:57）
| 轮次 | 配置 | 结果 |
|---|---|---|
| g152e | `pong_dpu_flip=3`（只换映射，不碰 MDSS）+ autoreset 300/sb 60 | 106 s 自己回 fastboot，64 次拷贝完成，klog reason=deadline |
| g152f | `pong_dpu_flip=2`（splash 尾部双缓冲 + SSPP/CTL_FLUSH 翻页） | 106 s 回来，**flips 516 taken 515**，首翻 2 µs 取走，之后 3–6 ms，waits 61（最大 15 ms），cpu-flips 0 |

→ 步骤 2（内核双缓冲 + DPU flip）验证通过。

---

## 2. 四条"回得来"的保险（21:40–22:12）

| 机制 | 实现 | 验证 |
|---|---|---|
| **round.sh 自动补参数** | 调用方没给 `pong_autoreset=` 时自动加 `pong_autoreset=300 pong_autoreset_sb=60` | g152e 起每轮 |
| **内核默认 300 s 到点复位** | `PongBootReset.h start()`：没有 boot-arg 也按 `DefaultDeadline=300` 装；只有显式 `pong_autoreset=0` 才关（打印 off） | g152g：命令行无参数，打印 `no pong_autoreset boot-arg: using the default 300s`，300 s 到点回 fastboot |
| **长按电源 3 s 软复位** | xnu 补丁 `0010-pong-power-key-reset.patch`（`bsd/dev/pong_buttons.c` 轮询线程里电源键按住 ≥ `pong_button_reset` ms，默认 3000，0 关，调 `pong_button_reset_hook`）；kext 侧 `PongBootReset.h buttonReset()`（弱引用 hook；若 `pong_autoreset=0` 没写过 SDAM 则先 `markBootloader`；`pong_scanout_summary`；`pong_klog_flush(4)`，klog.py 显示 power-key；`resetNow()` = PSCI SYSTEM_RESET2 暖复位 + 1 s 看门狗兜底）| g152h：用户按 3 s → `Pong keys: power held 3000 ms: resetting` → `PONG_AUTORESET: power key held: resetting into fastboot now` → klog reason=power-key → 开机 187 s 回 fastboot（到点本应 256 s）|
| **周期性 klog**（排查用） | `pong_klog_period=<s>`：守卫线程每 s 秒 `pong_klog_flush(5)`（klog.py 显示 periodic），冷咬也能留下几乎完整的控制台 | g152q：2971 行完整控制台，定位到冻结点 |

用户 22:10 在旁验收 g152h：**画面正常没撕裂，滑动顺，动画比之前好多了**；桌面左右翻页动画偶有一下卡顿。

---

## 3. 第 3 步：零拷贝（22:20–00:05）

### 3.1 调研先行（两份备忘）
- `docs/plan/2026-10-31-zero-copy-step3.md`（22:35）：
  - **XNU 在这台机上一页 4 GiB 以下的物理内存都没有**：DarwinBoot 的 4 GiB 主窗口只能落在 0x9_0000_0000 那条 bank，追加 bank 被 `ClaimPongRam` 钳在 ≥ 4 GiB（有意为之，低端是 DXE 堆）。所以 g152i 里 `inTaskWithPhysicalMask(…,0xFFFFF000)` 的 `no contiguous 47/31 MiB below 4 GiB` 不是碎片，是必然。
  - 推荐路线：loader 照 `pong-audio-dma` 的模式预留面板池；今天一帧的 surface 已经以 `(IOMemoryDescriptor*, offset)` 到达扫描驱动，物理地址一句 `getPhysicalSegment` 就有；缺的是 GPU 完成 fence 和 SwapWait/latch 闸门；VIG 格式 `0x237ff` = 线性 4×8bit 无 UBWC，像素不用转换。
- `docs/plan/2026-10-31-triple-buffer-swapwait.md`（23:50）：
  - 没有属性能直接让 CA 多建画布；CA 的画布数取决于它认为哪些 surface 还在用。我们的 `SwapWait` 永远成功，所以两张就够。iOSPlus10 0254 把 `SwapWait` 改成上屏前返回 Busy 后，"CoreAnimation keeps 3 surfaces"。
  - `SwapWait(id, flags)`：bit31 = 非阻塞查询；type 1 = 该 swap 已上屏；type 0 = 已被后一帧替换（其画布空闲）。CA 每帧：`s N` → `w N 0x80000001` → ~16 ms 后 `w N 0x80000000` → `s N+1`。
  - 面板现在 60 Hz（固件 splash 留在 timing@0）；DT 有 120 Hz 时序，切换需 DSI `timing-switch-command` + INTF tearcheck 重编程。

### 3.2 逐轮记录

| 轮次 | 时间 | 配置 | 结果 / 发现 |
|---|---|---|---|
| g152i | 22:24 | `pong_dpu_flip=1`（内核 RAM 分配） | `no contiguous 47 MiB below 4 GiB`、`31 MiB` 都失败，回落 mode 2 正常（516/516） |
| g152j | 22:44 | 48 MiB 面板池 + `pong_dpu_flip=1 pong_dpu_flip_test=0xb`（只自测） | loader 预留到 `0xe5800000`；自测翻到池缓冲 **1373 µs 取走**、翻回 11 ms → DPU 读普通低端 DRAM 没有 XPU 障碍 |
| g152k | 22:46 | 池 + `pong_dpu_flip=1` 全量 | flips 555 taken 489，waits 0，in-place 0；mode 1 可作默认 |
| g152l | 22:49 | 80 MiB 池 + 0011 + `pong_zc=1` | **哑轮**：klog state 0、DRAM 副本 316 KB 乱码。唯一可读：`canvas 2160x1916 bpr 8704 … does not fit a slot (… bpr 8640)` → **CA 画布行距 8704（256 对齐），不是面板的 8640** |
| g152m | 22:54 | 80 MiB 池 + `pong_dpu_flip=1`（zc 关） | 正常（池在 `0xad000000`，533 翻页）→ 池位置无罪 |
| g152n | 22:57 | `pong_dpu_flip=2 pong_zc=1`（不用池，0011 活） | 正常（512/512）→ 0011 代码本身无罪 |
| g152o | 23:00 | 行距修正（96 MiB 池、17 MiB 槽、YSTRIDE0 随翻页写）+ `pong_zc=1` | **又哑**（176 s ≈ 76 s 到点 + 60 s 冷咬） |
| g152p | 23:06 | 同 o，槽函数只用 IOLog | **仍哑** → 排除 dpuLine |
| g152q | 23:11 | 同 o + `pong_klog_period=3`，0011 只给显式指定 bpr 的 surface 发槽 | 周期 klog 生效：冻结点在 `scanout worker ready` 后 ≤ 3 s，正是 framebuffer/画布创建之际 |
| g152r | 23:16 | 槽映射改 `kIOMapCopybackCache`、去掉 bzero | **通了**：`canvas slot given 0xaf300000`、`backboardd canvas 2160x1916 bpr 8704 … in panel slot`，swap 17/18 交替，moving 71–78 fps |
| g152s | 23:21 | `pong_zc=2`（零拷贝翻页） | **工作**：`PONG ZC: frame 2: buffer 4 at 0xaf300000 stride 8704, fence 12/12 after 1 us, shown after 1 us`；262 个零拷贝帧（隔帧，只有一张画布在槽），fence 等待平均 1.4 ms / 最大 8 ms，latch 等待平均 8.6 ms，`user=` 从 3.3–4.0 ms 降到 1.0 ms |
| g152t | 23:24 | `pong_zc=2 pong_touch_synth=2`（放行 kernel_task 自建表面进槽） | **全帧零拷贝**：2641 帧全部零拷贝且全部被取走，in-place 0；moving 48.1 fps，节拍 `0/1953/0/518/0/30/22`（74% 两拍、20% 四拍） |
| g152u | 23:28 | 同 t 但 `pong_zc=0`（拷贝） | moving 55.2 fps，但 `flips 2888 taken 1662`：**1226 次翻页在 latch 前被覆盖，这些帧从未显示** |
| g152v | 23:54 | `pong_zc=2 fb_swapwait=1 touch_lat=1` | **CA 收到真实应答后去建第三张画布**（`canvas slot NONE FREE for 0x0 (2 taken so far)`）；应答统计：0x80000001 2241 成功/1576 NotReady，0x80000000 1837 NotReady/280 成功；CA 对 NotReady 不阻塞，照常 SwapEnd；第三张落在普通 RAM 走拷贝，fps 43.2 |
| g152w | 23:58 | + `pong_dpu_bufs=2`（3 槽） | 三张画布全部进槽，`user=0`，45.5 fps，节拍 `0/1005/0/342`（仍量化） |
| g152x | 00:02 | **不带任何显示参数**（默认值） | `2 buffers, 3 canvas slots (pong_zc=2)`、`SwapWait: 276 not ready`、`user=0`、44.4 fps → 默认值生效 |

### 3.3 两个会让整机冻死的根因（必须记住）
1. **池内存的默认映射是 device 类型。** 面板池是 XNU 不管的物理页，`createMappingInTask(kernel_task, 0, kIOMapAnywhere)` 的默认缓存属性不是普通 RAM 的 copy-back；随后 `bzero` 16.7 MB（ARM64 bzero 用 `dc zva`）在 device 内存上触发同步异常，而此时 panic 路径拿不到 UFS 完成中断、无法暖复位 → pinger 死 → 60 s 后 Gunyah 看门狗冷咬 → DRAM 控制台乱码、klog 没写。四轮（g152l/o/p/q）全是它。**规则：池内存一律 `kIOMapCopybackCache` 映射（内核映射和用户 pixelMap 都是），不要 bzero；loader 已清零。** 0234 当年就是这么做的。
2. **没有 `pong_autoreset` 的内核什么回路都没有**（见第 1 节）。现在内核默认 300 s，不可能再漏。

### 3.4 最终的数据通路（现在的默认行为）
```
DarwinBoot24                 AllocatePages(AllocateMaxAddress < 4 GiB, EfiReservedMemoryType, PANEL_POOL_PAGES=24576)
                             → 96 MiB 面板池（实机落在 0xac000000），清零 + FlushRange，回填 ADT /chosen/pong-panel-pool {pa, bytes}
                             （模板 = pong-audio-dma；Fail 路径 FreePages）
make-pong-adt-ipados18.py    ADT 占位 ('pong-panel-pool', bytes(16))
PongAdrenoScanout.cpp dbInit 读 lease；槽步长 SlotStride=17 MiB（容纳 8704×1916）；dbuf 自用 want=2 个缓冲（db[1..2]，presenter 映射 34 MiB）；
                             其余 3 个作为画布槽（slotPhys[]，也登记进 db[] 但 offset=~0，不是 presenter 目标）；pong_zc=2
                             没有 lease（旧 loader）→ 原 inTaskWithPhysicalMask（必败）→ 回落 mode 2（splash 尾部双缓冲）
xnu 0011 MiniIOSurface       surfaceCreate：backboardd 或 kernel_task 的 2160×1916 'BGRA' bpe 4、无 plane、offset 0 的 surface
                             → pong_panel_slot_take(width,height,bpr,alloc,&phys)（几何：宽高=面板，bpr≥w×4 且 64 对齐，alloc≤槽）
                             → client = withPhysicalAddress(slot)，内核映射与用户 pixelMap 都 kIOMapCopybackCache，不清零；free() 归还槽
                             面板尺寸却没进槽的 surface 打 miss 日志（gSlotMisses ≤ 8）
MiniIOMobileFramebuffer      show() → pong_scanout_frame(memory=client, offset=0, bpr, w, h)
PongAdrenoScanout.cpp        pong_scanout_frame：source 的物理首页 ∈ 槽 → zcFlip：
                               等 GPU fence（pong_gpu_fences：ring lastSubmitted vs fence page，≤ 50 ms）
                               → 前一次 flip 若未 latch 先等（≤ 20 ms，保持顺序）
                               → dbFlip(槽)：行距不同则先写 SSPP_YSTRIDE0，再写 SSPP_SRC0_ADDR，CTL_FLUSH 0x20003
                               → 等 latch（≤ 20 ms，使 show() 返回时帧已在屏上）
                               → 以"已完成"的 pending 走 finish() 做统计（PONG ZC 64/deadline 行）
                             非槽帧（多层/部分更新）照旧 presenter 拷贝进 dbuf 缓冲再翻；CPU 合成帧 pong_scanout_cpu_frame 翻回 console
xnu 0012 MiniIOMobileFramebuffer  fEndedID / fPresentedID / fRetiredID；SwapWait 查询未就绪返回 kIOReturnNotReady（fb_swapwait=2 → Busy），
                             阻塞调用最多 50 ms；show() 开头把前一帧标已上屏、末尾把本帧标已上屏（零拷贝帧此时已 latch）
CoreAnimation                拿到 NotReady 后自建第三张画布 → 三张轮转，全部在槽里
```

### 3.5 所有开关与默认值
| 开关 | 默认 | 含义 |
|---|---|---|
| `pong_dpu_flip` | **1** | 1 = 面板池 mode 1（无池回落 splash）；2 = splash 尾部双缓冲；3 = 只换映射不碰 MDSS；0 关 |
| `pong_dpu_bufs` | **2** | dbuf 自用的池缓冲数（2–3）；其余池缓冲给画布槽（96 MiB 池 = 5 个 17 MiB） |
| `pong_zc` | **2** | 0 关；1 画布进槽但仍拷贝；2 直接翻到画布 |
| `fb_swapwait` | **1** | 1 如实应答（NotReady）；2 用 Busy；0 永远成功（旧行为） |
| `pong_panel_pool` | 1 | 0 = 忽略 loader 的池 |
| `pong_dpu_flip_test` | 0 | 位 1 空 flush、2 翻到缓冲 1 300 ms 再翻回、4 不带 CTL 位 17、8 自测后关、0x10 自测后只 CPU 帧 |
| `pong_dpu_flip_klog` | 0 | 关键步骤前后写 klog 标记 |
| `pong_autoreset` / `pong_autoreset_sb` | **300** / 0 | 到点复位；sb = SpringBoard 后 s 秒结束；0 关 |
| `pong_button_reset` | 3000 | 电源键按住 ms 后软复位；0 关 |
| `pong_klog_period` | 0 | 每 s 秒把控制台复制到 UFS klog（排查用） |
| 旧行为组合 | — | `pong_dpu_flip=2 pong_zc=0 fb_swapwait=0` |

命令行长度已顶到 DarwinBoot24 的 1023 字节上限（g152v 因此打包失败两次），**新开关一律做成内核默认值**，不要靠命令行。

### 3.6 数字汇总（合成滑动 `pong_touch_synth=2`，sb=90 的轮次约 5 分钟）
| 配置 | CA 产帧 moving fps | 翻页 flips/taken | 真正显示的独立帧 | presenter 每帧用户态时间 | 节拍分布 |
|---|---|---|---|---|---|
| 拷贝（g152u，`pong_zc=0`） | 55.2 | 2888 / 1662 | 1662 | 3.3–4.9 ms | 乱（off-beat 1569） |
| 零拷贝双画布（g152t） | 48.1 | 2642 / 2641 | 2641 | 0（隔帧仍拷贝时 ~1 ms） | 74% 2 拍、20% 4 拍 |
| 零拷贝三画布（g152w/x，默认） | 45.5 / 44.4 | 全取走 | 全部 | **0** | 67% 2 拍、23–25% 4 拍 |

解读：面板按 60 Hz latch；零拷贝下 `show()` 同步等 latch，每帧对齐 vsync，渲染 + fence 超过 16.7 ms 的帧掉到 30 fps。拷贝路径看着 fps 高是因为 CA 不等任何东西、多数帧没上屏。按"显示出的帧"算零拷贝多 59%；按手感要用户目测（见第 6 节）。

---

## 4. 代码与文件清单

**Mac（权威）** `$ZIZHAN_SRC/`
- `UniXNU-zizhan/drivers/DarwinBoot24/DarwinBootPkg/Driver/DarwinBoot.c`：`PANEL_POOL_PAGES`、`mPanelPoolOffset`、分配/回填/释放
- `UniXNU-zizhan/scripts/make-pong-adt-ipados18.py`：`pong-panel-pool` 占位
- `UniXNU-zizhan/drivers/PongAdreno/PongAdrenoScanout.cpp`：dbuf mode 1 用池、`SlotStride`、`DbBuf.stride` + `dbFlip` 写 YSTRIDE0、`pong_panel_slot_take/give`、`zcFlip`、ZC 统计、默认值
- `UniXNU-zizhan/drivers/PongAdreno/submit/NativeAdrenoSubmit.cpp`：`pong_gpu_fences`
- `UniXNU-zizhan/drivers/ArmDTPlatform/PongBootReset.h`：默认 300 s、`buttonReset`、`pong_klog_period`
- `UniXNU-zizhan/patches/xnu-ipados18/0010-pong-power-key-reset.patch`、`0011-zizhan-panel-pool-canvases.patch`、`0012-zizhan-swapwait.patch`
- `klog.py`（reason 名：deadline/panic/flip-mark/power-key/periodic）、`round.sh`（自动补 autoreset）
- loader 重编：`cd UniXNU-zizhan && python3 scripts/build-darwinboot24-macos.py --edk2 $SRC/uefi-zizhan/Mu_Basecore --package $SRC/UniXNU-pin/package --llvm /opt/homebrew/opt/llvm@18/bin --output-dir $SRC/work/Pong-DarwinBoot24`（见 `logs/g152j.sh`）
- 改已应用的 xnu 补丁：先 `git -C work/pong-work/build/Pong-iPadOS18-native/tree/.cache/xnu-work apply --reverse 旧补丁`，再重生成（`edit_zc1*.py`、`edit_0012.py` 都这么做）

**本地镜像** （作者本机工作区）
- `device/DarwinBoot24/DarwinBoot.c`、`device/scripts/make-pong-adt-ipados18.py`、`device/PongAdreno/PongAdrenoScanout.cpp`、`device/PongAdreno/submit/NativeAdrenoSubmit.cpp`、`device/ArmDTPlatform/PongBootReset.h`、`device/patches/xnu-ipados18/0010–0012`
- `tools/mac/`：`round.sh`、`klog.py`、`edit_pool.py`（3.0）、`edit_zc1.py`（3.1）、`edit_zc1b.py`（行距）、`edit_zc1c/d/e/f.py`（排查与修正）、`edit_zc2.py`（3.2）、`edit_diag.py`（周期 klog）、`edit_0012.py`、`edit_defaults.py`、各 `g152*.sh` 批脚本、`g152d.collect.sh`
- 文档：`docs/plan/2026-10-31-zero-copy-step3.md`、`docs/plan/2026-10-31-triple-buffer-swapwait.md`、`docs/progress/2026-10-31-frame-timeline.md`（第 9–10 节流水账）、本文

**手机状态**：安卓待命；用户态 = g152 插件（presenter 带 destinationOffset，对任何内核兼容）；最新内核/loader = g152x 的（`out/boot-g152x`）。

---

## 5. 教训（写进了记忆）
1. 一轮"不回 fastboot"≠ 挂死，除非命令行里有 `pong_autoreset`（现已默认）。
2. XNU 不管的物理内存：映射必须显式 `kIOMapCopybackCache`，不要 bzero。
3. 冷咬后 DRAM 控制台不可信，排查时开 `pong_klog_period=3`。
4. 改已应用的 xnu 补丁先 reverse 再重生成；生成补丁时注意 Python 字符串里的 `\n` 要写成 `\\n`（g152s 第一次编译失败就是这个）。
5. 命令行 1023 字节是硬上限，新开关做内核默认值。
6. Mac 上的 `scp` 远端路径用绝对路径 `/Users/<user>/...`，`mac:~/...` 在变量里会被当成本地路径。

---

## 6. 待用户验证（攒着）
- [ ] **零拷贝三缓冲画面 + 手感**：`round.sh gXXX pong_autoreset=600 pong_autoreset_sb=240`（默认即零拷贝）桌面出来后左右滑、开关 App，看撕裂/闪烁/错帧；复用槽时可能闪一帧旧画面（已知，不清零的代价）。
- [ ] **对比旧路径**：`round.sh gYYY pong_dpu_flip=2 pong_zc=0 fb_swapwait=0 pong_autoreset=600 pong_autoreset_sb=240`，比较滑动手感（零拷贝：每帧对齐 vsync、无撕裂、重帧掉 30 fps；拷贝：产帧多但近半没显示）。
- 结束任一轮：按住电源键 3 秒。

---

## 7. 下一步（按收益排序）
1. **`show()` 不再同步等 latch**（memo `2026-10-31-triple-buffer-swapwait.md` §2.3/§2.4 第 3 步）：内核同时 hold 在屏那张和已 flush 未 latch 那张，retire 延后到 latch（vsync tick 或下一次 zcFlip 开头），`fPresentedID` 由 latch 驱动而不是 show() 末尾；CA 已经会尊重 NotReady（g152v），所以等待会自然挪到 CA 线程，SwapEnd 队列不再是节拍器。判据：in-place 0、taken == flips、四拍帧比例下降。
2. **面板 120 Hz**（memo §3）：先只读探 INTF_1（MDSS+0x36000）tearcheck 寄存器（期望 vsync_count 154、60 TE/s），再做 DSI `timing-switch-command`（DT timing@1/3/4 = 120 Hz）+ tearcheck 重编程；参考 iOSPlus10 0250/0259。
3. **四拍帧成因**：`pong_gtrace` 看是渲染超时还是 fence 等待（命令行紧张，gtrace 需要临时去掉别的参数）。
4. 小修：槽复用时闪旧画面（可在 take 时用 GPU 清零或让 CA 先画）；`SSPP_SRC_UNPACK_PATTERN (0x34)` 加进探测存档。
5. 显示之外的大方向不变：热管理、外屏/折叠、Wi-Fi/音频/ADSP、睡眠唤醒、脱离 Mac 的独立开机。
