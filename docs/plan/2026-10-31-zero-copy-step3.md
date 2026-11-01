# 第 3 步设计：DPU 直接翻到 CoreAnimation 的 surface（零拷贝）

2026-10-31 22:20–23:30，只读调研。引用格式：`文件:行`。`0001:N` = Mac `UniXNU-zizhan/patches/xnu-ipados18/0001-pong-darwin24.patch` 第 N 行；`0234:N` = `device/ref/iosplus10-dpu/0234-negroni-fb-zero-copy.patch` 第 N 行；`Scanout.cpp` = `device/PongAdreno/PongAdrenoScanout.cpp`（与 Mac `drivers/PongAdreno/` 同文件 diff 为空）；`DarwinBoot.c` = Mac `drivers/DarwinBoot24/DarwinBootPkg/Driver/DarwinBoot.c`；`xnu/` = Mac `refer/ipados18/xnu-11417.140.69/`。

## 0. 结论先行

1. **XNU 在这台机上一页 4 GiB 以下的物理内存都没有**：DarwinBoot 的 4 GiB 主窗口只能落在 0x9_0000_0000 那条 6 GiB bank（低端最大 bank 只有 0x6a00_0000 = 1.66 GiB，`dts/pong.dts` memory 节点），追加 bank 又被硬性钳在 `>= 0x1_0000_0000`（`DarwinBoot.c:547,564`，README "leases ... above 4 GiB, leaves the low DXE heap intact"）。g152i 的 `Pong RAM: 4 owned banks, 9728 MiB` 全在高端。所以 `inTaskWithPhysicalMask(..., 0xFFFFF000)`（`Scanout.cpp:415`）**必然失败**，与碎片、掩码、对齐无关——`IOKernelAllocateWithPhysicalRestrict` → `kmem_alloc_contig(max_pnum=atop(0xFFFFFFFF))`（`xnu/iokit/Kernel/IOBufferMemoryDescriptor.cpp:241-243,316-329`、`xnu/iokit/Kernel/IOLib.cpp:738-767`、`xnu/osfmk/vm/vm_kern.c:1051-1061` 走 `cpm_allocate`），而 `vm_pages` 里最低的页就在 gPhysBase ≥ 0x8_0000_0000。SSPP_SRC0_ADDR 是 32 位、显示流不翻译（g151），所以**第 3 步的 surface 内存只能来自 XNU 不管的低端内存**：splash 区（只剩 16.8 MiB，放不下）或**开机 carve-out**。
2. 推荐路线：DarwinBoot 照 `pong-audio-dma` 的现成模式（ADT 占位 16 字节 → `AllocatePages(AllocateMaxAddress < 4 GiB, EfiReservedMemoryType)` → 回填 `{pa, bytes}`）给一个 48 MiB **面板池** `pong-panel-pool`；内核 `IOMemoryDescriptor::withPhysicalAddress` 包起来，先给 `pong_dpu_flip=1`（代替失败的 RAM 分配，回答"DPU 能否扫 splash 之外的 DRAM"），再给 MiniIOSurface 把 backboardd 的 2160×1916 BGRA canvas 放进池槽。
3. 今天内核里一帧的 surface 是以 `(IOMemoryDescriptor*, offset)` 到达扫描输出驱动的（`0001:6086-6089` → `Scanout.cpp:581`），物理地址一句 `getPhysicalSegment(0,…,kIOMemoryMapperNone)` 就有；缺的是 **GPU 完成信号**（`MiniIOSurfaceDrawn` 是永远 true 的桩，`0001:8882-8885`），现在靠"presenter 的 blit 排在同一条 in-order ring 之后"隐式保证。零拷贝去掉 blit 后要显式等 fence：ring 的 `lastSubmitted()/completedFence()`（`device/PongAdreno/submit/NativeAdrenoRing.h:102-103`，全局 `gGtRing` `NativeAdrenoSubmit.cpp:44`）够用，不必改插件。
4. CA 在 g152i 里只用 **两个** canvas surface 轮换（swap 1..8：17,18,17,18…，没有出现"none twice"统计行），所以生命周期的核心是：**CA 再画进 A 之前，翻到 B 的 flush 必须已 latch**（g152i 实测 latch 2 µs～13 ms，偶发 160 ms）。我们的 `SwapWait` 是空操作（`0001:6503-6509`），要么实现它，要么在 SwapEnd 队列里卡住。

## 1. 今天一帧怎么指认 CA 的 surface，内核怎么拿到物理地址

- swap 记录：每层 surface ID 在 `0x9c+4*layer`，源/目标矩形在 `0xac`/`0x10c`（`0001:5152-5155`）。SwapEnd 时 `MiniIOSurfaceCopy(sid)` 持有对象（`0001:5964`，查表 `0001:7536-7550`），presenter 线程取出后 `show()`（`0001:6003`）。
- GPU 路径候选条件（`0001:6076-6085`）：只有 layer 0、`canDrawLinearBGRA`（BGRA、无 plane、bpe 4、bpr ≥ w×4，`0001:5254-5268`）、surface 宽高 ≥ 面板、源/目标矩形都是 (0,0,2160,1916)。然后 `memory = base->client ?: base->memory`，`offset = pixels() - (client ? kpixels : kbase)`（`0001:6086-6087`），调 `pong_scanout_frame(memory, offset, bpr, w, h)`（`0001:6089`）。
- 内核侧：`IOSubMemoryDescriptor::withSubRange + prepare()`（`Scanout.cpp:605-610`），映进 presenter（`createMappingInTask`，`Scanout.cpp:566`），按 (source, offset, bytes) 缓存 4 个映射（`Scanout.cpp:79-82,598-599`），线协议 `PongScanoutFrame{sequence,address,bytes,stride,width,height,format,token,evict,target}`（`device/gpu-perf/drivers/PongAdreno/PongScanoutWire.h:18`）。用户态只认 `address+token`（`GpuScanoutPresenter.h:58-71`），GPU VA 从不出现在协议里。
- MiniIOSurface 的内存布局：`memory` = 一页共享状态 + 像素（`0001:9026-9037`，`kpixels = kbase + PAGE_SIZE` `0001:7428`），`info.offset` 再加在上面（`pixels()` `0001:9065-9068`）。所以像素物理地址 = `memory->getPhysicalSegment(PAGE_SIZE + info.offset)`；要整块连续就要整个 `memory` 连续。`client` 分支（`IOSurfaceAddress` 包客户内存，`0001:7429-7457`）是客户自己的页，永不连续，排除。
- **GPU 完成**：`MiniIOSurfaceDrawn` 返回 `drawn == 0`（`0001:8882-8885`），`drawn` 只在 show 结束时被当时间戳用（`0001:6141`）。今天 CA 的渲染和 presenter 的 blit 在同一进程、同一条 ring 上顺序执行，所以不需要 fence。零拷贝需要：在 `pong_scanout_frame` 入口采样 `gGtRing->lastSubmitted()` 作 F（CA 为这帧提交的最后一个 fence ≤ F），翻页前等 `completedFence() >= F`（`observeFence`/`fencePage` 均可轮询，`NativeAdrenoSubmit.cpp:444,137`）。单 ring in-order 是这个方案的前提；将来多 ring/抢占时改为用户态按 command buffer 报 fence。

## 2. 谁分配、什么几何、能不能连续、GPU 导入会不会变

- 分配者：backboardd 经 `IOSurfaceRootUserClient::create` → `surfaceCreate`（`0001:7228`）；显示器的默认 surface 是 `MiniIOSurfaceCreateSimple(fWidth,fHeight,'BGRA')`（`0001:5587`）。几何：`bpr = IOSurfaceBytesPerRow ?: alignUp(w×bpe, 64)`（`0001:7364-7367`，`MINISURF_ROW_ALIGN 64` `0001:6812`），`alloc = alignUp(bpr×h+offset, PAGE_SIZE)`（`0001:7403`）→ 2160×1916×4 = 0xFCA000（16.5 MiB，恰是 g152i 里 buffer 1 的偏移 `offset 0xfca000`）。
- 分配方式：`surfaceMemory(PAGE_SIZE + alloc)`（`0001:7413`）：先从 4 槽的"留用池"拿同尺寸旧内存（4～32 MiB，`0001:6865-6867,6910-6932`），没有才 `inTaskWithOptions(kernel_task, InOut|KernelUserShared, bytes, PAGE_SIZE)`（`0001:6936`）。16.5 MiB 的 canvas 正好落在留用池区间：CA 每次新建 canvas 多半拿回上一块。池只有 `busy`（清零中）一个状态（`0001:6893`），没有"正在被扫出"的概念。
- 用户态映射：`memory->createMappingInTask(fTask, 0, kIOMapAnywhere)`（`0001:7863`），默认缓存属性；`client` 路径另映 `pixelMap`（`0001:7875`）。
- **物理连续改不了分配器就能做**：如上，XNU 没有 <4 GiB 的页；`kIOMemoryPhysicallyContiguous + 掩码` 这条路在这台机上是死的（`Scanout.cpp:415` 的 options/mask/alignment 本身是标准写法：`0xFFFFF000` 即"上限 4 GiB、4 KiB 对齐"）。只能把 surface 的像素放到内核外的低端内存里：用 MiniIOSurface 现成的 `client/kpixels/clientMap` 分叉（`0001:9033-9037`）指向池槽的 `IOMemoryDescriptor::withPhysicalAddress`，状态页仍是普通 `IOBufferMemoryDescriptor`。`surfaceMemoryBack` 只回收 `IOBufferMemoryDescriptor`（`0001:6952-6957`），池槽不会被留用池吞掉；`free()` 要归还槽位。
- GPU 导入不受影响：presenter/Turnip 用 `newBufferWithBytesNoCopy(user VA)` → `importBo` → `importTask`（`submit/NativeAdrenoMemory.cpp:183-229`）：`withAddressRange(user VA, task, kIOMemoryPersistent)` + `prepare()`，再逐 4 KiB `getPhysicalSegment` 合并成 span。连续只是让 `span_count_ = 1`。上限 256 MiB/对象（`NativeAdrenoABI.h:17`），物理地址 < `1<<output_bits`；**没有 16 MiB 限制**，4 只是内核留映射数 `KeptCount`（`Scanout.cpp:81`）和 presenter `kKept`（`GpuScanoutPresenter.h:111`）。"用户 VA 映射的是 XNU 之外的物理内存"这种导入已被 g152e/f/i 证实可用（mode 2 的 splash 尾部 0xb9b36000 就是这么被 GPU 写的）。注意 GPU 写入的导入按非一致映射（`NativeAdrenoVm.cpp:15-18`），GPU 写完落 DRAM，DPU 直接读 DRAM，不经 CPU cache；CPU 写的帧才需要 `dc cvac`（见 §5 风险）。

### 2a. 物理内存版图与 carve-out 怎么做

- DRAM 12 GiB（ADT `dram-size=0x300000000`），bank：`0x80000000+0x6a000000`、`0xf4600000+0xba00000`（4 GiB 以下共 ~1.84 GiB）、`0x8_0000_0000+0x39900000`、`0x8_39f0_0000+0x1e00000`、`0x8_4000_0000+0xc0000000`、`0x9_0000_0000+0x1_8000_0000`（`dts/pong.dts` memory 节点，`tools/gen_reserved.py` 从实机 FDT 生成）；低端 39 个 no-map 区（同文件 region0..38），其中 `region29@b8000000 0x2b00000` 是 splash。
- UEFI 低端可用（EfiConventional）的候选在 `configs/pong-memory-sm8475.json` "allocatable"：`0xa0400000+0x6a00000`（106 MiB）、`0xa760d000+0x109f3000`（266 MiB）、`0xbab00000+0x25b00000`（603 MiB）、`0xe0a00000+0x4bf3000`、`0xe5700000+0x3100000`、`0xf1400000+0xec00000`——ADT `pong-allocatable-ram` 把它们全部报给 loader（Mac `scripts/make-pong-adt-ipados18.py:189-191`），但 `ClaimPongRam` 的 `Start = MAX(MAX(Base, D->PhysicalStart), 0x100000000)`（`DarwinBoot.c:547,564`）把低端全过滤掉；主窗口 `AllocateWindow` 取第一段 ≥ 4 GiB 的 EfiConventional 连续段（`DarwinBoot.c:457-491`）。这是有意为之：低端是 DXE 堆，固件回调到 ExitBootServices 前还在用（README "Additional Pong DRAM"）。
- 现成模板 = 音频 DMA：ADT 占位 `('pong-audio-dma', bytes(16))`（本地 `device/scripts/make-pong-adt-ipados18.py:187`）；loader `AllocatePages(AllocateMaxAddress=0xffffffff, EfiReservedMemoryType, 16 页)`、清零、刷 cache（`DarwinBoot.c:750-760`），把 `{pa, 65536}` 写进 ADT 单元（`DarwinBoot.c:800-801`），失败路径 FreePages（`:917`）；内核 `PongAudio.cpp:124` 从 `/chosen` 读出、校验 `< 4 GiB` 后映射。`EfiReservedMemoryType` 不会被 `ClaimPongRam` 当 RAM（只收 EfiConventional，`:542,557`），也不会被 XNU 当页管理。
- **面板池**：同一模式，`pong-panel-pool` 16 字节，大小 `3 × 0xFCA000` 向上取 2 MiB = 0x3000000（48 MiB；n 由 loader 常量定，kernel 按 `pong_dpu_bufs` 切槽）。内核侧 `IOMemoryDescriptor::withPhysicalAddress(pa, bytes, kIODirectionInOut)`（`Scanout.cpp:486` 同款）或 `withAddressRanges`（`:441`）；用户态映射 `createMappingInTask`（`:566`，mode 2 已验证）。
- 不推荐的两条：(a) 放开 `ClaimPongRam` 的 4 GiB 下限让 XNU 管一段低端内存——会让任意内核分配散落到低端、动摇 README 里的固件契约，且 `inTaskWithPhysicalMask` 事后分配仍可能碎片化；(b) 继续用 splash 区——0xb8000000 之后已有内屏 15.8 MiB + 外屏 10.4 MiB（VIG2 `0xb8fc9900`），剩 16.8 MiB，只够 1 个；关掉外屏也只到 27 MiB。

## 3. CA 的帧布局是否就是 VIG0/VIG1 取的布局

- 固件留下的管线（g149）：VIG0 `addr 0xb8000000 size 0x77c0438 (1916×1080) xy 0 stride 0x21c0 fmt 0x237ff`，VIG1 同地址 `xy 0x438`（x=1080），CTL0 fetch VIG0+VIG1 → INTF_1。
- `SRC_FORMAT 0x237ff` 按 sde_hw_sspp `setup_format` 解码：bits0-7=0xff 四分量各 8 bit，bit8 alpha，bits9-11=3 → 4 字节/像素，bits12-13=3 → 4 分量解包，bit17 tight，bits19-20=0 interleaved，bits30-31=0 → **线性取数，无 UBWC/tile**。分量顺序在 `SSPP_SRC_UNPACK_PATTERN (0x34)`，探测没读；但 XNU 控制台和 CA 的 BGRA 帧原样 blit 进该缓冲画面正确（g152h 用户验收），即管线的解包顺序 == CA 写的字节序。零拷贝不改一个字节，**不存在 BGRA/RGBA 转换问题**；建议把 0x34 加进 `dpuProbe` 存档。
- CA canvas 尺寸：`fWidth/fHeight = fPanelWidth/fPanelHeight`（`0001:5492-5493`），只有 boot-arg `fbscale/fbwidth` 才改（`0001:5505-5513`）；ADT `display-scale=2` 只影响点/像素比，不影响像素几何。bpr：CA 不指定则 8640（64 对齐恰好 = 2160×4）；若 CA 指定了别的 `IOSurfaceBytesPerRow`，`dbFlip` 现在只写 `SRC0_ADDR`（`Scanout.cpp:320`），需同时写 `YSTRIDE0 (0x24)`（0234 每次 flip 都写，`0234:595`），`SRC_XY` 保持 0/0x438。surface 比面板大时用 `SRC_XY` 裁剪（`0234:594-596`）。原点都是左上，翻转位在 `OP_MODE`（探测已打印 `op`）。**待确认项**：实际 CA surface 的 bpr/alloc——在 `swap 1 (#0)` 日志行加印 `bpr/allocSize` 即可（`0001:6066`）。

## 4. 0234 怎么解决指认与生命周期，哪些没做对/没做完

- 指认（`0234:371-387`）：swap 只有一层、源/目标 = 整个 canvas、`negroni_surface_copy(sid)` 得到的 surface 有 `slot ≥ 0`、宽高等于 canvas、`bytesPerRow ≤ 0xffff` 且 64 对齐、`phys+allocSize` 不越 splash 末尾。
- 槽：`zeroCopyInit` 在 splash 剩余空间切 3 槽（`0234:65,342-343,366`）；**只在 backboardd 创建**几何完全匹配的 surface 时发槽（进程名比对 `0234:892-897`，几何 `0234:820-837`），像素 = `withPhysicalAddress(slot)`、可缓存映射、建时清零+flush（`0234:899-921`）；弱表 `negroni_slot_surface[]` 在 `free()` 清槽（`0234:749,758-765`）。
- 生命周期：swap 时 `negroni_surface_hold(zc,true)`（`0234:237-240`），`fZCShown/fZCNext` 各持一个引用+hold（`0234:125-126`）；每个 vsync tick `zeroCopyRetire` 看 `CTL_FLUSH` 清零即 latch，才放掉上一个（`0234:401-431,435-452`）；GPU 帧等 `NegroniGPUFenceDone` 才 flip（`0234:186-188`）；CPU 帧先 `dc cvac` 脏行（脏行由每次 swap 的矩形累计到所有槽，`0234:229-236,840-873,583-590`）；`SwapWait(id)` 阻塞到该 swap 已上屏（≤50 ms，`0234:626-648`），这是防 CA 画进正在扫的缓冲的关键，来自 ios-s9 0096 的观察"CA 在重用另一块 framebuffer 前会 SwapWait 上一次的 id"；12 tick 没 latch 永久关闭（`0234:66,418-430`）；显示关/panic 把管线写回面板内存（`0234:503-516,523-563,651-659`）。
- 没做对/没做完：(1) 从未开机验证。(2) 3 槽放 splash 剩余——在我们机上放不下（§2a）。(3) 依赖 CA 调 `SwapWait` 且传对 id；我们内核的 `SwapWait` 现在直接返回成功（`0001:6503-6509`），移植时必须实现，或改用 SwapEnd 队列闸门（`fSwapsWaiting ≥ swapQueue()` 阻塞，`0001:5950-5957`，`SWAP_QUEUE 2` `0001:5212`）并把"开始展示"定义为"flush 已 latch"。(4) CPU 渲染视角的 cache 维护（slot 可缓存映射 + 手工 `dc cvac`，`0234:786-799`）对我们是 GPU 渲染为主，可省，但要给 CPU 合成帧留开关。(5) 时钟偏移 hack（`0234:460-499`）与我们无关。(6) 他们的 `flip` 在工作循环里自旋等 latch 打日志前 4 次（`0234:609-619`），与我们的 `dbFlip` 一样。

## 5. 实施计划（小步、可 A/B）

前提：`pong_dpu_flip=1/2` 的 flip 机制已验证（g152f/h/i），mode 1 的 RAM 分配必失败（§0）。

**3.0 面板池 carve-out + 用 mode 1 探 XPU**（loader + kernel，不碰 MiniIOSurface）
- ADT：`('pong-panel-pool', bytes(16))`（Mac `scripts/make-pong-adt-ipados18.py` 紧挨 `pong-audio-dma`）。DarwinBoot：`AllocateMaxAddress 0xffffffff, EfiReservedMemoryType, 48 MiB`，2 MiB 对齐，回填 `{pa, bytes}`，`Print("DARWINBOOT24: panel pool 0x%lx %lu MiB")`；失败回填 0。
- `Scanout.cpp dbInit` mode 1：先读 `/chosen/pong-panel-pool`（校验 `pa+bytes ≤ 4 GiB`、`pa & 0x1fffff == 0`），`withPhysicalAddress` 代替 `inTaskWithPhysicalMask`；读不到再走原分配（必败→mode 2）。boot-arg `pong_panel_pool=0` 忽略池。
- 预期日志：`PONG DBUF: on (mode 1): 4 buffers 0xb8000000 0x<pool> 0x<pool+0xfca000> 0x<pool+0x1f94000>`，`flip 1: buffer 1 at 0x<pool> taken after N us`，`PONG DBUF 64: ... taken == flips`，用户看屏无撕裂。
- 风险：XPU/NoC 不放行 DPU 读非显示内存 → flush 不 latch（1 s 后 `dbOff` 回 console，`Scanout.cpp:385`）、黑屏或 SError。先跑 `pong_dpu_flip=1 pong_dpu_flip_test=0xb pong_dpu_flip_klog=1`（自测：翻到池 300 ms 再翻回，不动 GPU，klog 留标记），再去掉 test 位。这一步同时验证"GPU 写池、DPU 读池"。

**3.1 MiniIOSurface 池槽 surface（仍 blit，不翻）**
- `surfaceCreate`：`pong_zc≥1` 且进程 backboardd、BGRA、planeCount 0、bpe 4、`width==fPanelWidth`、`height==fPanelHeight`、alloc ≤ 槽大小、`IOSurfaceAddress` 为空 → 取空槽，`s->client = withPhysicalAddress(slot)`，`kpixels = 内核映射`，用户映射走 `pixelMap`（`0001:7875`），`free()` 归还槽；池由 Scanout 导出 `pong_panel_slot_take/give(phys*)`。槽数 = 池大小/0xFCA000（48 MiB → 3）。
- 日志：`MiniIOSurface: backboardd surface %u (2160x1916 bpr %u) in panel slot %d at 0x%llx`；`swap 1 (#0)` 行加 bpr。预期 fps 与 g152i 持平（blit 照旧，证明 GPU 导入池槽 surface 没问题）。
- 风险：CA 创建的 canvas 几何不等于面板（bpr≠8640 / 更大）→ 不进槽，日志可见，按 §3 处理 stride/裁剪。

**3.2 零拷贝翻页**（`pong_zc=2`，需 `pong_dpu_flip=1`）
- `pong_scanout_frame` 入口：若 `source` 是池槽描述符（指针比对）且 `offset==0`：不下发 presenter；采样 `F = gGtRing->lastSubmitted()`，持有该 MiniIOSurface（`MiniIOSurfaceCopy` 计数已在 SwapEnd 做了，这里再 +1，交给 flip 状态机），worker 等 `completedFence() ≥ F`（上限 50 ms，超时回拷贝路径并记日志），然后 `dbFlip` 到槽物理地址（bpr≠8640 时连写 `YSTRIDE0`）；`dbSettle` latch 后释放上一个 shown 的 surface。shown + flipped 两个 surface 始终被持有；CA 的第三块（若有）照常。
- 闸门：`show()` 在零拷贝帧上**直到该帧 flush latch 才让 SwapEnd 队列前进**（即把 `fSwapFirst++/fSwapsWaiting--` 延后到 latch，`0001:6045-6049`），保证 CA 不会在 DPU 仍扫 A 时开始画 A；`SwapWait(id)` 实现为"等 id ≤ 已 latch 的 swap id"（0234 语义）。
- 非零拷贝帧（多层/部分更新/CPU 合成）：照旧 blit 进 dbuf 缓冲，或 `pong_scanout_cpu_frame` 翻回 console（`Scanout.cpp:709-718`）；panic/到期：`dbRestore`（`Scanout.cpp:456-461`，建议同时挂到 `kPEPanicBegin`，如 `0234:651-659,719-722`）。
- 预期日志：`PONG ZC: flip N: surface S slot K at 0x… fence F waited N us, taken after N us`；`PONG SCANOUT TIMING` 的 `user=` 从 3300–4900 µs（g152i）降到 0；`PONG FPS SUMMARY` 翻页动画 moving fps 对比 g152h/i（57 fps）；`PONG ZC 64: zc N copy N fence-wait max N us latch max N us`。
- A/B：`pong_zc=0`（3.1 行为）/ `2`；`pong_dpu_bufs` 调槽数。

**3.3 收尾**：`pong_zc_clean=1`（CPU 写帧前 `flush_dcache64(phys,bytes,1)`，`Scanout.cpp:422` 同款，默认关）；把 `SSPP_SRC_UNPACK_PATTERN` 加进探测；`gtrace` 对翻页动画专门量一次再决定 display-scale。

**风险汇总**：(1) XPU 拒绝 DPU 读普通 DRAM——3.0 一轮即知；若拒绝，退路是只在 splash 区放 1 槽 + 现有 dbuf（去撕裂、省不了 blit）。(2) CA 只用 2 块 canvas，latch 偶发 13–160 ms 会把 SwapEnd 卡到下一帧，表现为掉帧而非撕裂；可观察 `fb_queue`/第三块 surface 的效果。(3) 单 ring in-order 假设（fence 采样法）——多 ring 后要改为用户态报 fence。(4) 池槽 surface 的用户映射缓存属性与 GPU 非一致导入的组合同 mode 2 已验证；CPU 路径写入才需 clean。(5) 留用池/`MiniIOSurfaceClearKept` 不会碰池槽（非 `IOBufferMemoryDescriptor`），但 `free()` 漏还槽会在 3 帧后把零拷贝饿死——加计数日志。
