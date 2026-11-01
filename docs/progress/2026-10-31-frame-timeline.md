# 帧率：逐帧时间线与用户态提速（2026-10-31 中午起）

方案见 `docs/plan/2026-10-31-frame-timeline.md`。起点 g134：2 s 一次合成滑动，在动的帧 54.0 fps；用户不在手机旁，全部无人值守（`cycle.sh`）。

## 1. GPU 提交时间线（A3，g136）

内核改动（Mac `UniXNU-zizhan/drivers`，镜像在 `device/`）：
- `PongAdreno/submit/NativeAdrenoSubmit.cpp`：`pong_gtrace=<s>` 期间记录每次提交（fence、进程名、IB 数、是否切了 ring 上下文、µs）；一个内核线程每 200 µs 读 CP 写的时间戳（`NativeAdrenoRing.h::peekFence`，只失效缓存再读 `fence_[1]`，不记账），推进时记一条；结束后把全部事件写进 DIAG 区（`GT s/d/c` 行）并打 `PONG GTRACE:` 汇总。6144 条上限（6 s 约 1200 条）。
- `PongAdreno/PongAdrenoScanout.cpp`：每次扫描输出拷贝记 offer / pickup / done（`GT c`）。
- `ArmDTPlatform/PongBootReset.h`：和 tsample 一样 SpringBoard 后 20 s 启动。
- 时钟与显示驱动 `touch_lat=1` 的 `tl: s ID at US`（CA 交出 swap）、`tl: v ID ready/shown/done`（出队、等到节拍、像素进面板）相同（`mach_absolute_time` 的 µs）。
- 分析 `tools/mac/gtrace.py LOG`：按 swap 把上一 swap 之后 backboardd 的渲染提交归到这一帧，拆成 gap / enc / gpu / post / queue / beat / copy；扫描输出的 blit 提交是 pickup 之后 backboardd 的**第一条**提交（第一版把拷贝窗口里的下一帧渲染也当成 blit，91 帧"无渲染"就是这个 bug）。

g136（`pong_autoreset_sb=35 pong_touch_synth=2 pong_gtrace=6 touch_lat=1`，klog 完整，55.9 fps）：6 s 里 472 次提交（backboardd 468 = 234 渲染 + 234 blit，WidgetRenderer 3，壁纸 1）、234 次扫描输出拷贝、8 次上下文切换，GPU 有 fence 在飞的时间占 24%。223 个在动的帧（中位 / p90 / 均值，µs）：

| 段 | 含义 | 中位 | p90 | 均值 |
|---|---|---|---|---|
| frame | swap 间隔 | 12423 | 33288 | 17047 |
| gap | 上一 swap → 本帧第一条渲染提交 | 12024 | 33072 | 16739 |
| enc | 第一条 → 最后一条渲染提交（每帧只有 1 条命令缓冲） | 0 | 0 | 128 |
| **gpu** | **渲染提交 → fence 完成** | **4576** | **9718** | 4599 |
| post | fence 完成 → swap 交出（负：swap 在 GPU 完成前） | −4138 | −343 | −4298 |
| queue | swap → 出队 | 47 | 735 | 551 |
| beat | 出队 → 等到节拍 | 623 | 7034 | 2298 |
| copy | 节拍 → 像素进面板（blit 排在本帧渲染后面，含渲染剩余时间） | 3850 | 8859 | 5019 |

- CA 每帧只提交 **1 个命令缓冲**，提交后 **0.2 ms 就交出 swap**（不等 GPU）；下一帧的渲染提交总是在上一帧 **GPU 完成之后 0.3–5.3 ms**（上一帧 gpu_done → 下一帧提交），即 **CA 等上一帧渲染完成，再在自己的下一个 120 Hz 节拍开始下一帧**。帧周期 ≈ GPU 渲染时间 + 等节拍 ≈ 4.6 + ~4 → 中位 12.4 ms，与 1/2 拍分布一致。
- 所以瓶颈是 **GPU 渲染一帧要 4.6 ms（p90 9.7 ms）**，不是 CPU：各进程 CPU 低、GPU 忙 24% 都和它相符（4.6 ms × 55 fps = 25%）。我们的插件强制 `TU_DEBUG=sysmem,nocb`（每个 pass 都走系统内存，不用 GMEM 分块），AdrenoMetal 461a842 就是这一项：强制 sysmem 时 GPU 忙 19%，让 Turnip autotune 自选后 70 fps p50。
- 顺带：扫描输出 blit 本身约 2–3 ms GPU（16 MB 拷贝），和渲染串在一条 ring 上；pickup 15 µs；节拍等待中位 0.6 ms。

16 帧样例（相对第一帧 swap 的 µs）：

```
      id        s    first     last gpu_done    ready    shown    panel     nsub
     145        0     -213     -213     8335       90       90    10198        1
     146    11578    11440    11440    17043    11607    16765    20785        1
     147    21035    20695    20695    28627    21085    25235    31871        1
     148    34089    33785    33785    39176    34128    34128    40414        1
     149    44766    44439    44439    52760    44855    50793    54579        1
     150    55853    55703    55703    61209    55880    59201    62984        1
     151    66822    66465    66465    74629    66930    67583    76613        1
```

## 2. 用户态：Turnip 渲染路径可切、设备应答缓存（D1、D2，g137 镜像）

- `PongMetal/PongMetalPlugin.mm`：`TU_DEBUG` 默认仍 `sysmem,nocb`，boot-arg `pong_tu_debug=<flags>` 覆盖（读 `kern.bootargs`，与 `pong_gpu_scanout=0` 同法），日志 `Pong Metal: TU_DEBUG=…`。0016（tile store 越行）暂不需要：主屏 surface 2160×1916 正好是 16×4 块的整数倍，且 `PONG_IOSURFACE_PADDING` 没开。
- `PongMetal/NativeBridgeMetalFrontend.mm::askAnswer`：AdrenoMetal 31867dc，带参数的设备应答按 (selector, 参数) 记住。我们的前端基于 AdrenoMetal bcbbc70（差 341 行）；main 之后还有 d67af6a、461a842 等，gpu-parity 分支有编码线程。
- 我们的 Turnip 已带 UniXNU cd1b975（fence 等待一次 100 ms），data-a-live 6265e2d 同一修法，排除。
- 部署 `tools/mac/metal-refresh.sh TAG`：`run-bridge.sh`（cargo 增量 + 前端 .mm）→ `build-pong-metal-plugin.py --clients '*'` → `cp -c` 备份 OS 镜像 → `sudo build-ipados18.py refresh_metal` → `prepare-ipados18-ufs-layout.py`（三个镜像重新哈希，8 s）→ `install_pinned.py --only OS`（6528 MiB，约 12.8 MB/s，约 8.5 min）。替换掉的东西都带 TAG 保留（`work/gpu/plugin.g137`、`prepared/pong-ipados18-system.g137.cdr`、`ufs-metal/ufs-layout.g137.json`）。
- A/B 批处理 `tools/mac/ab-tudebug.sh`：g137 `pong_tu_debug=nocb` / g138 默认 / g139 `nocb`，都带 gtrace + tl。

## 3. A/B 第一批（g137–g139，g137 镜像，都带 gtrace + tl）

| 轮次 | TU_DEBUG | 在动的帧 | 帧间隔中位 / p90 | GPU 渲染中位 / p90 | 1 拍占比 |
|---|---|---|---|---|---|
| g136（老插件） | sysmem,nocb | 55.9 fps | 12423 / 33288 | 4576 / 9718 | 236/727 |
| g137 | nocb（autotune 自选 GMEM） | 60.2 fps | 11455 / 33205 | 5056 / 9987 | 345/727 |
| g138 | sysmem,nocb（默认） | **63.9 fps** | 10494 / 33277 | 5276 / 9794 | 411/789 |
| g139 | nocb | 59.5 fps | 10356 / 33567 | 5026 / 9672 | 356/699 |

- **让 Turnip 自选 GMEM 没有收益**（60.2/59.5 vs 63.9），GPU 渲染一帧仍约 5 ms——不是 sysmem 分块的问题。AdrenoMetal 461a842 的收益在 A740 上，不在我们这。
- g138 比老插件高约 8 fps，唯一差别是 31867dc 设备应答缓存（QuartzCore 带参数的设备查询每次要过 m2v）；只有一轮，之后要重复确认。
- 第二批 `tools/mac/ab-tudebug2.sh`：g140 `gmem,nocb`（强制 GMEM）、g141 `nocb` + 固定 900 MHz（`pong_gpu_dcvs=0 pong_gpu_level=13`）、g142 两者叠加——分别检验分块和 GPU 频率对那 5 ms 的影响。

## 4. A/B 第二批（g140–g142）

- g140 `pong_tu_debug=gmem,nocb`（强制 GMEM 分块）：59.6 fps，GPU 渲染中位 5161 / p90 10109 µs——**和 sysmem 一样**，分块方式与那 5 ms 无关。
- GPU 时间直方图（g136/g138/g139/g140 都一样）是双峰：约 40% 的帧 <3 ms，约 50% 的帧 6–10 ms。按帧看（g138 第 155–194 帧）：翻页动画中每帧约 29–30 个 IB、GPU 6–10 ms，相邻帧 10 ms 一帧，blit 还排在下一帧渲染后面——**滑动期间 GPU 实际是饱和的**（10 s 平均 21–28% 只是因为滑动只占一部分时间）；<3 ms 的是动画外的帧（18–21 个 IB）。
- g141/g142 第一次因命令行 1035/1040 字节 > 1023 被 DarwinBoot24 拒绝（`ci_boot_ufs.sh` 检查生效，没浪费轮次）；g141 去掉 `pong_tu_debug` 后重跑（固定 900 MHz，`pong_gpu_dcvs=0 pong_gpu_level=13`）。
- 带宽：内核 DDR 投票表就是厂商 DT 的 `qcom,bus-table-ddr`（12 档，顶 13.09 GB/s，GPU 顶档配 bus 11），不是我们压低的。

## 5. 固定频率与重复基线（g141、g143），结论

| 轮次 | 设置 | 在动的帧 | GPU 渲染中位 / p90 | GPU 时间直方图（<3 ms / 6–15 ms 帧数） |
|---|---|---|---|---|
| g141 | 默认 + 固定 900 MHz（`pong_gpu_dcvs=0 pong_gpu_level=13`） | 62.5 fps | 5319 / 9715 | 112 / 144 |
| g143 | 默认（g138 重复） | 59.2 fps | 5128 / 9732 | 105 / 120 |

- **GPU 频率也不是原因**：固定 900 MHz 直方图不变。翻页动画里每帧约 30 个 IB 在 900 MHz 下就要 6–10 ms，这是 CA 渲染这一帧的真实 GPU 工作量（两页图标 + 壁纸 + 背景模糊等），再加同一条 ring 上 16 MB 的扫描输出 blit（约 2–2.5 ms）。动画期间 GPU 饱和，帧周期约 10 ms → 约 60 fps；动画外的帧 <3 ms。
- 今天的净收益：老插件 54–56 fps → 新插件（31867dc 设备应答缓存）59–64 fps（g138 63.9、g141 62.5、g143 59.2、g137/g139/g140 带不同 TU_DEBUG 59.5–60.2）。GMEM/sysmem、GPU 频率都在噪声内。手机上现在是 g137 镜像（默认 `sysmem,nocb`，可用 `pong_tu_debug=` 覆盖），内核带 `pong_gtrace`（不给 boot-arg 就不开）。
- 排除清单（都有数据）：内核扫描输出 3 ms、大小核、节拍/队列、GPU 频率、壁纸扩展、Turnip fence 轮询（已是 100 ms）、GMEM/sysmem、CPU 编码（每帧 1 个命令缓冲、提交后 0.2 ms 就 swap）。

### 下一步（按预期收益）
1. **A4 逐 IB 的 GPU 时间**（内核）：提交时在 IB 之间插 `CP_EVENT_WRITE`/ALWAYS_ON 时间戳（Linux msm `a6xx_submit` 的 submit profiling 做法），配合已记的每个 IB 前 28 个 dword，找出每帧多出的那约 10 个重 IB 是哪些 pass（模糊？两页整屏合成？纹理上传？）。这是决定"砍什么"的依据。
2. **去掉扫描输出 blit**：每帧 16 MB 读 + 16 MB 写占 ring 约 2–2.5 ms。根治是 DPU 直接从 CA 的 IOSurface 扫描（翻页而不是拷贝，annibale `2026-10-28-panel-120hz-path.md` 的方向），工程量大；折中是让 blit 不和渲染串行（第二条 ring / 优先级）。
3. 减少 CA 每帧的 GPU 工作：确认 1 之后再定（例如背景模糊降采样、减少整屏 pass）。
4. backboardd 编码线程（gpu-parity）对我们**没有**意义：CPU 不是瓶颈。

## 6. 逐 IB 的 GPU 时间（A4，g144–g145）

实现（`submit/NativeAdrenoSubmit.cpp`，`pong_gtrace` 期间默认开，`pong_gtrace_ib=0` 关）：按 Turnip 自己 `tu_trace_record_ts` 的 A7XX 写法，在 ring 里每个 IB（前 32 个）之后插 `CP_THREAD_CONTROL(BR)` + `CP_EVENT_WRITE7(RB_DONE_TS, WRITE_ALWAYSON, RAM)` + `CP_THREAD_CONTROL(BOTH)`：BR 管线干完手头工作时写 19.2 MHz always-on 计数器，相邻差就是该 IB 的 GPU 时间（BV 分箱跑在前面，不计）。64 位时间戳写在 fence 页 0x100 起的 8 个槽（fence%8），轮询线程看到 fence 完成就读槽（8 次提交之后才复用）。`scratch_words_` 加大到 128×4+34×8。输出 `GT i FENCE N SUBMIT_US us:bytes:sig …`，`gtrace.py` 按重帧（GPU ≥5 ms）/轻帧汇总。

g144：532 次提交全部打上时间戳、531 回读，无 GPU 丢失，60.7 fps（探针不影响帧率）。重帧 n=116，时间戳总和 6454 µs/帧；轻帧 n=127，1464 µs/帧。一个重帧（swap 160，29 个 IB，5656 µs）：**IB2 一个 1476 B 的 IB 占 2879 µs**，IB4/5/7 各 500–600 µs，IB27 357 µs，IB17 175 µs，其余 20 多个 IB 每个 7–28 µs（872–880 B 的是 Turnip 每个命令缓冲的首尾）。每个 IB 开头都是 `CP_THREAD_CONTROL`，前两个 dword 认不出 pass——g145 改成内核走一遍 IB（含一层 IB2）统计 draw/blit/compute 数、`CP_SET_MARKER` 模式、窗口裁剪、MRT0 格式、2D blit 目标尺寸/格式（寄存器号来自 mesa `a6xx.xml`）。

g145（IB 分类，486/486 回读，61.1 fps，探针仍不影响帧率）。重帧 n=104，GPU 6681 µs/帧；轻帧 n=121，1345 µs/帧。重帧里按 pass：

| pass（裁剪窗口 / 绘制数 / 模式） | 每重帧均值 | 占比 |
|---|---|---|
| 全屏 2160×1916，5 次绘制，DIRECT（sysmem） | 2578 µs | 39% |
| 全屏，13 次绘制 | 736 µs | 11% |
| 全屏，1 次绘制 | 678 µs | 10% |
| 1297×1150，1 次绘制 | 556 µs | 8% |
| 全屏，12 次绘制 | 313 µs | 5% |
| 528×83（搜索条）、36×36、24×36 等小窗口 | 各 ≤ 256 µs | |
| 每个命令缓冲首尾的 compute（Turnip 的 1 个 dispatch） | 7–10 µs × 14 | |

一个重帧（swap 154，29 个 IB，5518 µs）：IB2 全屏 5 draw 2782 µs，IB4 1297×1150 1 draw 520 µs，IB5 全屏 1 draw 611 µs，IB7 全屏 58 draw 572 µs（图标），IB27 全屏 13 draw 352 µs，IB17 全屏 3 draw 171 µs，其余小窗口 pass 各 7–27 µs。轻帧的全屏 pass（8–59 次绘制）只要 25–350 µs——"全屏"是裁剪窗口，轻帧里的绘制面积小（图标）。

**结论**：一次真正覆盖全屏的绘制约 0.55 ms ≈ 4.1 MP ÷ 7.5 Gpx/s，正是 A730 在 900 MHz 的 ROP 填充率；重帧相当于 **9–10 层全屏填充**（翻页动画期间 SpringBoard 把两页/背景等整层离屏栅格化再合成：那几个 1 次绘制的全屏 pass 和 1297×1150 pass 就是这种"整层画一遍"），加上同一条 ring 上 16 MB 的扫描输出 blit（约 2.5 ms，DDR 带宽 13 GB/s 投票下 33 MB 的读写），一帧 GPU 约 8–9 ms → 60 fps。GMEM/sysmem、GPU 频率、CPU 侧都已排除，与此一致。

### 下一步评估
1. **去掉扫描输出 blit = DPU 直接翻页**（每重帧省约 2.5 ms，GPU 约 6.5 ms 可落进一拍 8.3 ms → 滑动接近 120 Hz）。一手参考：iOSPlus10（一加 10 Pro，SM8450，DPU 8.1 同代）`patches/xnu/0234-negroni-fb-zero-copy.patch`（只读 DPU 探测：MDSS 0x0ae00000，找 `SRC0_ADDR` 等于 GOP 帧缓冲的 SSPP；翻页 = 写 `SSPP_SRC0_ADDR`/`YSTRIDE0` + `CTL_FLUSH`，100 ms 没生效就回退）、`0269-negroni-fb-double-buffer.patch`（3 个面板缓冲）、`docs/audit/2026-10-26-display.md`（风险：自动刷新下只 FLUSH 不 START 是否在帧边界 latch、撕裂）。他们设备卖掉前**从未启动过**这两补丁；sheng 有"实验性直接 scanout"（DPU CTL/SSPP）。副本在 `device/ref/iosplus10-dpu/`。步骤：① 只读探测一轮（找 SSPP/CTL、确认 SRC0_ADDR = 我们的 GOP 地址）；② 在 splash 区外分 2–3 个面板大小缓冲，blit 到空闲缓冲后翻页（先保留 blit，验证 latch）；③ 让 CA 的 IOSurface 直接当面板缓冲（真正零拷贝，要 MiniIOSurface 分配器配合）。
2. 减少 CA 的全屏层：要看 SpringBoard 的层树（哪些层在翻页时被整层栅格化、`shouldRasterize`/组透明/背景模糊），属于用户态/系统设置范畴，收益不确定。
3. 不值得做：编码线程、GMEM、GPU 频率。

## 7. DPU 直接翻页：只读探测（13:45 起）

- 参考：iOSPlus10 `0234-negroni-fb-zero-copy.patch` 的探测段（SSPP 0x5000/0x7000/0x9000/0xb000/0x25000/0x27000/0x29000/0x2b000：SRC_SIZE +0、SRC_XY +8、OUT_SIZE +0xc、OUT_XY +0x10、SRC0_ADDR +0x14、YSTRIDE0 +0x24、SRC_FORMAT +0x30、OP_MODE +0x38；CTL 0x16000..0x1b000：LAYER0 +0、TOP +0x14、FLUSH +0x18、INTF_ACTIVE +0xf4、FETCH_PIPE_ACTIVE +0xfc；翻页 = 写 SSPP_SRC0_ADDR/YSTRIDE0/SRC_XY + CTL_FLUSH（管线位 VIG0..3 = 0/1/2/18，DMA0..3 = 11/12/24/25，CTL 位 17），引擎在下一帧起点吃掉 flush、CTL_FLUSH 读回清零即 latch）。zizhan `live.dts` 的 `qcom,mdss_mdp@ae00000` 偏移与之**完全相同**（sde-sspp-off、sde-ctl-off、intf 类型 dp dsi dsi dp）。
- 实现：`PongAdrenoScanout.cpp::dpuProbe`，`pong_dpu_probe=1` 在截止复位前（`pong_scanout_summary`）跑，2 = 开机也跑；`ml_io_map_unmappable(0x0ae00000, 0x84000)`，只读；打印 8 个 SSPP、6 个 CTL，并标出 `SRC0_ADDR == 控制台物理地址` 的管线。另读 APPS SMMU（`apps-smmu@15000000`）的 sCR0/IDR 与所有有效 SMR/S2CR/CBAR，标出显示流（DT `qcom,handoff-smrs = <0x2800 0x402>`、`smmu_sde_unsec_cb iommus <0x2800 0x402>`）。
- **为什么要看 SMMU**：零拷贝要 CA 的 3 个轮换 surface 都是 DPU 能扫的面板缓冲，每个 2160×1916×4 = 15.8 MiB，3 个 47.4 MiB，而 `cont_splash_region`（0xb8000000，0x2b00000 = 43 MiB）装不下 3 个。若显示流在 SMMU 里是旁路（S2CR TYPE=bypass 或 sCR0.CLIENTPD）或恒等映射，缓冲可以放任意物理内存；若是带页表的 translate，就只能在 splash 区做 2 缓冲翻页（只去撕裂，去不掉 blit），或者自己给显示上下文建页表。
- g146：`cycle.sh` 在 `adb reboot bootloader` 后 **NO_FASTBOOT**，之后 USB 上 adb/fastboot 都不见（`ioreg` 只有 Mac 自带 hub）——与 10-30 20:28 那次一样，手机停在 fastboot 而这个口不枚举，需人工换口/重插。探测内核与镜像已备好（`out/boot-g147`，命令行 973 字节），手机回来后 `bash cycle.sh out/boot-g147/pong-xnu-initial.img g147 420` 即可。
- g147 ×2（16:14、16:17，手机换口后回到 Android）：两次都在 SpringBoard 后 30 s 的截止点打出 `PONG DPU deadline: console phys 0xb8000000, mdss 0xae00000 mapped` 之后**没有任何一行**，接着 75–90 s 回到 fastboot，`klog not written`，ramoops 的 DRAM 副本整篇位翻转（g145 的同一副本是干净的）→ 读第一个 MDSS 寄存器（VIG0 0xae05014）就把 SoC 挂死，看门狗冷复位。诊断行是 `CleanPoC_DcacheRegion_Force` + `dsb` 直落 DRAM 的，所以挂点确定在第一组 SSPP 读，SMMU 还没碰到。
- **原因（一手资料）**：显示子系统的寄存器路径要两级 AHB 时钟。mainline `drivers/clk/qcom/gcc-sm8450.c` 在 probe 里把 `0x37004 GCC_DISP_AHB_CLK`、`0x3701c GCC_DISP_XO_CLK` 列为 always-on 强制打开；`dispcc-sm8450.c`（`qcom,sm8475-dispcc` 同布局，我们 DT 叫 `qcom,cape-dispcc`，基址 0x0af00000）里 `disp_cc_mdss_ahb_clk` CBCR = 0x80a4、`mdss_mdp_clk` 0x800c、`mdss_vsync_clk` 0x8024、`mdss_non_gdsc_ahb_clk` 0xc004、`mdss_rscc_ahb_clk` 0xc00c、`mdss_gdsc` GDSCR 0x9000、`mdss_int2_gdsc` 0xb000；下游 sde 驱动碰寄存器前先 `sde_power_resource_enable` 拿 `iface_clk`（live.dts `mdss_mdp` clock-names 第 4 项 = disp_cc_mdss_ahb_clk）。面板在扫（MDP 核心时钟、GDSC 开着）不代表 AHB 开着；iOSPlus10 在一加 10 Pro 上直接读能活，是那台的固件把 AHB 留开了，我们这台没有。
- **改法（16:28，g148）**：`dpuProbe` 改成分级：GCC（0x00130000 映射 64 KiB：0x37004/0x37008/0x3700c/0x37014/0x3701c）→ DISPCC（GDSC、AHB、MDP、VSYNC 等 7 个寄存器）→ MDSS SSPP/CTL → APPS SMMU；每级先把一行写进 ramoops，发现 `GCC_DISP_AHB_CLK` 或 `DISP_CC_MDSS_AHB_CLK` 的 CLK_OFF（bit 31）为 1 就停。`pong_dpu_probe` 位：1 截止点、2 开机、0x10 允许把关着的 AHB 打开（写 CBCR bit 0 再等 CLK_OFF 落，mainline `qcom_branch_set_clk_en` 同一动作）、0x20 跳过 SMMU。g148 先用 1 只看（不写）。
- 顺带：16:14 那次 75 s 回 fastboot 不是人为重启，`pong_autoreset_sb=30` 本来就是 SpringBoard 后 30 s 复位。
- g148（16:29）：内核 6 s 增量编好（镜像里已有分级探测的字串），但 `adb reboot bootloader` 后手机再次 **NO_FASTBOOT**（Mac 上 adb/fastboot/ioreg 都看不到），同 g146。Mac 上留了 `logs/g148.autorun.sh`：一看到手机（adb 或 fastboot）就自动 `cycle.sh out/boot-g148/pong-xnu-initial.img g148 420`。待办：查 USB 反复不枚举的原因（下一节）。
- 修正：ramoops DRAM 副本的位翻转在 cycle.sh 注释里记过是**发热**所致（g40–g58），不能单凭它判断冷复位；g147 的判断依据是"mapped 之后无任何一行、没有 `deadline reached`、klog 没写"。
- g148（16:48，分级探测只读，60.1 fps 不受影响）：`GCC disp_ahb 0x0`（voted 分支，CLK_OFF=0 → 开）、`qmip_disp_ahb 0x80000002` 关、`disp_hf_axi 0x111` 开、`disp_xo 0x80000000` 关；`DISPCC mdss_gdsc 0xf822f000`（PWR_ON）、`mdss_ahb 0x80000000`（**CLK_OFF=1，关**）、`mdp 0x221` 开、`vsync 0x1` 开、`non_gdsc_ahb 0x0`、`rscc_ahb 0x1` 开 → 探测在 MDSS 前按设计停下。**结论：面板在扫、MDP 时钟和 GDSC 都在，唯独寄存器总线时钟 `DISP_CC_MDSS_AHB_CLK` 被固件关了，g147 读 0xae05014 就是撞在它上。** 下一轮 g149 用 `pong_dpu_probe=0x11`：写 0x80a4 bit 0 开 AHB、等 CLK_OFF 落，再读 SSPP/CTL/SMMU。零拷贝正式实现时要把"开 AHB"作为前置（Linux `sde_power_resource_enable` 同一动作），并考虑 `GCC_DISP_XO_CLK` 是否也要开。

## 8. USB 反复"不枚举"的根因与修复（16:35–16:52）

现象：`adb reboot bootloader` 后手机进 fastboot，但 Mac 上 adb/fastboot/ioreg 都看不到，直到人工重插（10-30 20:28、10-31 13:46 g146、16:29 g148）。

Mac 统一日志（`log show --predicate 'subsystem == "com.apple.usb"'`）12:00 起 46 次 fastboot 枚举、25 次 Android 枚举，看到的规律：
- **每一次** Android→fastboot 切换，手机第一次 attach 都失败：`AppleUSB20HubPort::resetAndCreateDevice: reset did not enable port`（断开后 ~3.7 s），11/11 次；正常情况下 bootloader 3 s 后自己再 attach 一次，第二次成功（`enumerated 0x18d1/d00d`）。
- 失败的两次：g146 第一次 attach 失败后手机**没有再试**；g148 连第一次 attach 都没出现。XNU 自动复位→fastboot、ramoops 转储→Android 这两种切换从没失败过。
- 手机接在 Mac Studio 前面板 USB-C（`Port-USB-C@5`），走 ASMedia 3142 控制器下的 Apple 内置 USB2 hub（`usb2-hub-port1@08310000`，uhubctl 里是 hub 8-3 port 1；之前在 port 2），始终 480 Mbps。Mac 不睡眠（`pmset` sleep 0）。
- 判断：Qualcomm XBL 的 USB 高速握手与这颗内置 hub 的端口复位不合，首次必失败，靠 bootloader 重试补救，约 1/10 不重试就卡死。不是我们的软件问题，也与手机在 Android 里待多久无关（g136 隔 10 h 也正常）。

修复（远程实验室的常规做法，uhubctl README）：Apple 内置 hub 支持逐口断电（`uhubctl` 列出 `ppps`）。16:48 对卡死中的手机执行 `uhubctl -l 8-3 -p 1 -a cycle -d 3`（和 8-1 同口），6 s 后 `fastboot devices` 出现，g148 随即自动开跑。已落到脚本（`tools/mac/` 同步到 Mac `src.noindex/`）：
- `usbkick.sh remember|kick|status`：手机可见时记下 hub/口（`logs/phone-port.txt`，现为 `8-3 1`），`kick` 对该口和同总线兄弟 hub 的同号口断电 3 s。
- `boot.sh`：`adb reboot bootloader` 前先 remember；之后每 20 s 没见 fastboot 就 kick，最多 3 次，再报 NO_FASTBOOT。
- `cycle.sh`：等自动复位回 fastboot 时从 120 s 起每 45 s kick 一次；等 Android 回来时 60 s 起每 60 s kick；回到 Android 后再 remember。旧版留作 `*.pre-usbkick`。
- 若换口/换 hub：手机可见时跑一次 `bash usbkick.sh remember` 即可；外接 hub 需支持 ppps 才能 kick。

### g149（16:52，`pong_dpu_probe=0x11`，开 AHB 后全读）：DPU 布局与 SMMU 都拿到了
`DISP_CC_MDSS_AHB_CLK enabled -> 0x1 (running)` 之后 MDSS 全部可读（在开机握手点，"boot"）：

| 块 | 读数 | 含义 |
|---|---|---|
| VIG0 | addr 0xb8000000 size 0x77c0438 xy 0 stride 0x21c0 fmt 0x237ff | 内屏左半：1080×1916，源 = console 物理地址，行距 8640 = 2160×4 |
| VIG1 | addr 0xb8000000 xy 0x438 out_xy 0x438 | 内屏右半：同一缓冲，x 偏移 1080（双 LM 拆分） |
| VIG2 | addr 0xb8fc9900 size 0x9d80438 stride 0x10e0 | **外屏** 1080×2520，缓冲紧跟内屏缓冲（0xb8000000 + 0xfc9900 = 2160×1916×4） |
| VIG3 / DMA0–3 | 全 0 | 空闲 |
| CTL0 | layer 0x100002d 0x100002d, intf 0x2, fetch 0x30000 | 驱动 INTF_1（内屏），取 VIG0+VIG1 |
| CTL1 | layer2 0x1000140, intf 0x4, fetch 0x40000 | 驱动 INTF_2（外屏），取 VIG2 |
| SMMU | sCR0 0x200406, IDR0 0x4c017e71（113 SMR）, IDR1 0x6000004b | 未匹配流 fault（USFCFG），不是全局旁路 |
| SMR3 | id 0x2800 mask 0x402 → S2CR 0x3 **translate** cb 3, CBAR 0x1f000（stage-1） | 显示流走 context bank 3 的页表；0xb8000000 是恒等映射（IOVA==PA） |

然后 **panic：`Unhandled implementation specific error`（SError）**，发生在打印 SMR4 之后、循环继续读 SMR5… 的时候（GCC/DISPCC/MDSS 读都早已完成）。推断：APPS SMMU 在 Gunyah 下只对 HLOS 开放部分组，越界读触发异步外部中止。32 s 复位，klog 未写（panic 路径）。

**对零拷贝的含义**：
1. splash 区 43 MiB 里已有内屏缓冲 15.8 MiB + 外屏缓冲 10.4 MiB，剩 16.8 MiB → 只够 **再放 1 个** 内屏缓冲（即便停掉外屏回收 10.4 MiB 也放不下第 2 个）。所以 iOSPlus10 0234 的做法（把 backboardd 的 3 个 canvas surface 放进 splash 区 slot）在这台机上放不下；0269 的"面板双缓冲 + flip"能去撕裂但去不掉 2.5 ms 的 blit。
2. 真零拷贝 = 让 DPU 直接扫 CA 的 IOSurface（任意物理页）→ 必须过显示 SMMU：要么把 S2CR3 改成 bypass（一次写；splash 恒等映射所以画面不变），再给 CA 分配物理连续的 surface；要么往 cb3 页表里加映射（页表在 bootloader 保留内存里，CB 寄存器在 SMMU 基址 +0x80000 + 3×0x1000，IDR1 NUMPAGENDXB=6 → 128 页 ×4 KiB）。
3. 还要搞清 CA 的 surface 由谁分配（iOSPlus10 是自己的 NegroniSurface 给 backboardd 发 canvas surface；我们这边是 IOSurfaceRoot 还是 pong 自己的实现，待查）。

探测代码更新（g150 用）：位 2 = 开机点、0x20 = 读 SMMU 但**读到显示组就停**、0x40 = 把显示流 S2CR 写成 bypass 并回读（含 sGFSR）。g150 = `pong_dpu_probe=0x73`，开机点做 bypass 测试、截止点再读一遍。手机在 Android 时先经过新 boot.sh（usbkick）。
- g150（16:58，`pong_dpu_probe=0x73`，S2CR3 → bypass 测试）：开机点探测跑了两次（console 握手两次调用 `pong_scanout_initialize`），两次都在打印 `SMR3 … <- display stream` 之后、`S2CR3 … bypass written` 回读行之前停住，29 s 后复位，无 panic 文本。**写 S2CR 的那条指令没有返回**——Gunyah 把 APPS SMMU 全局空间（SMR/S2CR）握在手里，HLOS 只能读自己的几个组，不能改流映射。"显示流旁路"这条路堵死；剩下 **往 cb3 的页表里加映射**（页表是内存，TTBR0 可从 CB 寄存器读到；CB 空间按 MMU-500 布局在 SMMU 基址 +0x80000 + 3×0x1000）。g151 = `0xb3`，读 CB3 的 SCTLR/TTBR0/TTBR1/TCR/TCR2/MAIR/FSR 和 CBA2R3。
- 本轮也是新 boot.sh（usbkick）跑的第二轮：Android→fastboot 一次成功，没触发 kick。
- **g151（17:01，`pong_dpu_probe=0xb3`，读 CB3）：显示流实际上不翻译。** `CBA2R3 0x1`（VA64），`CB3 SCTLR 0xe0`（**bit 0 M=0，翻译关着**；只有 CFRE/CFIE/CFCFG 置位），`TTBR0 0x0 TTBR1 0x0 TCR 0x0 TCR2 0x60 MAIR 0x0 FSR 0x400`，`CBAR 0x1f000` 的 TYPE=01 = stage-1 + stage-2 bypass。即 context bank 3 是个空壳：显示流的地址原样进 DRAM（这就是为什么 SSPP 里写的是物理地址 0xb8000000）。**结论：DPU 现在就能扫任意物理内存，不需要动 SMMU。** 本轮完整跑到截止点（78 s，klog 写了，64.7 fps），开机点开 AHB + 读寄存器对运行无影响；截止点再读一遍数值相同。
- 顺带确认：我们的 IOSurface 是自己的 `MiniIOSurface`（`patches/xnu-ipados18/0001-pong-darwin24.patch`，`IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionInOut|kIOMemoryKernelUserShared, bytes, …)`），CA 的 canvas surface 放哪、是否物理连续由我们决定——加 `kIOMemoryPhysicallyContiguous` 即可（2160×1916×4 = 16.5 MiB，backboardd 启动早期分配 3 个没问题；失败回退拷贝路径）。

### 零拷贝路线（定稿，按 iOSPlus10 0269 → 0234 的顺序）
前置（每次碰 MDSS 寄存器前）：`DISP_CC_MDSS_AHB_CLK`（DISPCC 0x80a4 bit 0）开着——g148/149/151 证实它默认关、开了无副作用；GCC 侧 `GCC_DISP_AHB_CLK` 本来就开。
1. **步骤 2 = 面板双缓冲 + flip（0269）**：内核分配 2 个物理连续 16.5 MiB 面板缓冲（不用 splash 区，放不下）；扫描输出 worker 把 CA 的帧 blit 到空闲缓冲，GPU fence 完成后写 VIG0/VIG1 `SRC0_ADDR`（两个管线同一地址，x 偏移不变）+ `CTL0_FLUSH`（VIG0 bit 0、VIG1 bit 1、CTL bit 17），`CTL_FLUSH` 回读清零 = 引擎已取走，旧缓冲可复用；1 s 没取走就回退到 console 内存。收益：去撕裂、验证 flip 机制；帧率不变（blit 仍 2.5 ms）。验证靠人看屏 + 日志（flush 回读、CB3 FSR/sGFSR 无 fault）。
2. **步骤 3 = 直接扫 CA 的 surface（0234）**：MiniIOSurface 给面板几何（2160×1916 BGRA，bpr 8640）的 surface 用物理连续分配并记下物理地址；swap 只有一层、全屏、就是这种 surface 时，不 blit，直接 flip 到它（GPU 帧等 fence；CPU 帧先 clean cache），shown/pending 两个 surface 保持 held，SwapWait 等 flip 被取走；其他帧走原拷贝路径。预期每重帧省 2.5 ms GPU → 翻页动画 ~6.5 ms/帧，可落进 8.3 ms 节拍。
3. 风险/待查：Gunyah 对设备 DMA 读 HLOS 内存没有 stage-2 限制（CBAR 说 stage-2 bypass，Android 下 DPU 也直接读 HLOS 内存，应无问题）；外屏 VIG2/CTL1 不动；panic 时把管线指回 console 内存（0234 zeroCopyRestore）；`pong_dpu_probe` 的开机探测在 console 握手时跑了两次（`pong_scanout_initialize` 被调两次），实现时用一次性标志。

## 9. 步骤 2 实现：面板双缓冲 + DPU flip（18:15–18:40，待 g152/g153 验证）
按 §7 末"零拷贝路线"步骤 2 写完，设计照 iOSPlus10 0269（`device/ref/iosplus10-dpu/0269-negroni-fb-double-buffer.patch`：`dbufFlip` 写各管线 `SSPP_SRC0_ADDR` + `CTL_FLUSH`，`zeroCopyLatched` = FLUSH 回读清零，1 s 没取走就关掉），差别只在缓冲放哪、谁在拷。
- **内核 `drivers/PongAdreno/PongAdrenoScanout.cpp`**（本地镜像 `device/PongAdreno/`）：
  - `dpuBus()`：GCC→DISPCC→MDSS 三段时钟检查/映射，探测和 flip 共用（前置条件 `DISP_CC_MDSS_AHB_CLK` 在这里开）。开机探测加了一次性标志（console 握手调两次）。
  - `dbInit()`（boot-arg `pong_dpu_flip`）：`dbFindPipes()` 按寄存器找"SRC0_ADDR == console 物理地址"的 SSPP（g151 = VIG0+VIG1）和 `FETCH_PIPE_ACTIVE` 覆盖它们的 CTL（CTL0），flush 位 = sspp_tbl 位 + CTL bit 17 = 0x20003。
    - `=1`：`IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task, 物理连续, N×16.5 MiB, 掩码 0xFFFFF000)`——SSPP 地址是 32 位、显示流不翻译（g151），所以必须 <4 GiB；N = `pong_dpu_bufs`（默认 3：shown + flipped 都忙时第三个空闲，拷贝永不等 flip）。presenter 的映射就是这块内存，console 留给 CPU 帧。分配失败降级到 2。**这也是"管线能不能取 HLOS 内存"的测试**，步骤 3 依赖它。
    - `=2`：splash 区尾部空位（region29 0xb8000000+0x2b00000，末 16.5 MiB，先查没有活动 SSPP 覆盖）当第二个缓冲，presenter 映射 = {console, 空位} 两段物理描述符；两个缓冲，上一 flip 没取走时下一帧在 gate 里自旋等 ≤12 ms（纯 flip 机制测试）。
  - 每帧（`pong_scanout_frame`）：`dbTarget()` 选 ∉{shown, flipped} 的缓冲，帧结构 `target` 字段 = 它在映射里的字节偏移；`finish()` 里拷贝 retire 后 `dbFlip(target)`；前 4 次 flip 自旋 ≤20 ms 看取走没有并打日志 `PONG DBUF: flip N: buffer i at 0x… taken after N us`。
  - CPU 帧：新导出 `pong_scanout_cpu_frame()`，`MiniIOMobileFramebuffer` 在 CPU 合成前调（新补丁 `patches/xnu-ipados18/0009-zizhan-dpu-flip-cpu-frame.patch`，像 0002+ 一样每次构建倒回重打），把管线翻回 console。模式 1 关掉后 GPU 路径整体停用（console 不在 presenter 映射里）。
  - 复位前 `dbRestore()` 翻回 console（panic 文字可见），统计行 `PONG DBUF 64/deadline: on/off, flips taken (latch max us) waits (max us) in-place cpu-flips, shown flipped, CTL_FLUSH`。
- **协议/用户态**：`PongScanoutWire.h` 的 `PongScanoutFrame.reserved` → `target`（结构大小不变）；`PongMetalScanout.h` 不再拒绝非零值；`GpuScanoutPresenter.h` `Frame.target` 做边界检查并作 `destinationOffset`。旧插件遇到非零 target 会停（回 CPU 路径），所以插件先部署（`metal-refresh.sh g152`，备份 TAG g152）。
- 验证计划：g152 `pong_dpu_flip=2`（flush 在 autorefresh 下是否被取走——§7 2.2 的问题），g153 `pong_dpu_flip=1`（HLOS 内存 DMA）。看屏：撕裂应消失；看日志：`taken after` 应 ≤8.3 ms，`in-place`/`waits` 数、FPS SUMMARY 对比 g151 64.7。
- **g152（18:42，`pong_dpu_flip=2 pong_dpu_probe=0x12`）：手机硬挂。** 420 s 内无 fastboot、无 adb，uhubctl 看端口有电无设备，7 次踢口无效，Gunyah 看门狗也没把它拉回（g147 那种挂死 90 s 就回 fastboot）。要长按电源复位；DRAM ramoops 随之丢失，这轮没有日志。挂点未知：候选是 (a) GPU 往 splash 尾部 0xb9b36000 写（XPU/总线保护？），(b) 第一次写 `SSPP_SRC0_ADDR` / `CTL_FLUSH`，(c) 两段物理描述符映射给 presenter。
- 对策（已编译）：`pong_dpu_flip_klog=1` 在每个危险步骤前后写 `PONG DBUF mark:` 行并调 `pong_klog_flush(3)` 把 ramoops 刷到 UFS 的 klog 文件（冷复位也留得住；手机回 Android 后 `klog.py read` 取）；`pong_dpu_flip_test` 位 1 = 第一帧时做一次"原地 flush"（SRC0_ADDR 写回 console + CTL_FLUSH，看 latch），位 2 = 翻到缓冲 1 300 ms 再翻回（不碰 GPU），位 8 = 自测后关掉双缓冲、本轮按老路跑。下一轮 g152b = `pong_dpu_flip=2 pong_dpu_flip_test=0xb pong_dpu_flip_klog=1`（`logs/g152b.autorun.sh` 等手机回 Android 自动起跑）；通过后 g152c 去掉 test 位，让 mark 括住第一次 GPU 拷贝。

### 现状快照（19:10，等人长按电源）
- 手机：g152 挂死后仍在失联状态（USB 口有电无设备），等长按电源回 Android。Mac 上 `logs/g152b.autorun.sh`（nohup）在轮询 adb，看见手机后 30 s 自动跑 g152b；要取消就 `pkill -f g152b.autorun`。
- 手机上的用户态镜像：**g152 插件**（presenter 支持 `target` 偏移；对旧内核兼容，target 恒 0）。备份：`work/gpu/plugin.g152`、`prepared/pong-ipados18-system.g152.cdr`、`ufs-metal/ufs-layout.g152.json`。
- 最新内核：`out/pong/.../zizhan_ipados18_gpu/kernel`（含 dbuf + klog mark + self-test、补丁 0009）；round.sh 每次重编（6 s）再打包。不带 `pong_dpu_flip` 时与 g151 行为一致。
- 源码位置：Mac `UniXNU-zizhan/drivers/PongAdreno/{PongAdrenoScanout.cpp,.h,PongScanoutWire.h}`、`drivers/PongMetal/{PongMetalScanout.h,GpuScanoutPresenter.h}`、`patches/xnu-ipados18/0009-zizhan-dpu-flip-cpu-frame.patch`；本地镜像 `device/PongAdreno/`、`device/gpu-perf/drivers/{PongAdreno,PongMetal}/`、`device/patches/xnu-ipados18/`。
- 若 g152b 再硬挂：长按复位回 Android 后手动取 klog：`cd src.noindex && python3 klog.py read ../logs/g152b.klog.txt`（header state 1 才算写成；reason 3 = flip mark），看最后一条 `PONG DBUF mark:` 就是挂点前的最后一步。
- 19:15 用户反馈：g152 挂死时**屏幕定在 iOS 桌面**（不是黑屏/花屏）。含义：dbInit（console 握手时）没挂；挂在桌面出现后第一帧 GPU 路径附近——第一次 GPU 往缓冲 1 写、或第一次 SSPP/CTL_FLUSH 写；缓冲 1 从未被扫出来（否则会看到它的内容）。用户随后长按电源重启，g152b 自动起跑。

### 交接记录（19:40，对话将清空，从这里接着干）
**此刻状态**
- 手机：g152b 挂死后仍定屏在 iOS 桌面、USB 不枚举，等用户长按电源 ~10 s 复位到 Android。软件侧无任何办法（uhubctl 只切 VBUS）。
- Mac 后台：`logs/g152b.klog.sh`（nohup）在轮询 adb；手机一回 Android 就 `klog.py read logs/g152b.klog.txt` 并把 `PONG DBUF` 行写到 **`logs/g152b.klog.out`**，然后停（不起新一轮）。接手后先 `cat logs/g152b.klog.out`。若它没跑（Mac 重启等），手动：`cd src.noindex && python3 klog.py read ../logs/g152b.klog.txt && grep -a "PONG DBUF" ../logs/g152b.klog.txt`。header state 必须是 1（已写）、reason 3 = flip 标记触发的刷写。
- 手机用户态 = **g152 插件**（presenter 支持 `PongScanoutFrame.target` 偏移；对不带 `pong_dpu_flip` 的内核完全兼容，target 恒 0）。备份：`work/gpu/plugin.g152`、`prepared/pong-ipados18-system.g152.cdr`、`ufs-metal/ufs-layout.g152.json`（恢复用 `metal-refresh.sh` 的反向，或直接 `install_pinned.py --layout ufs-layout.g152.json --only OS`）。
- 最新内核已编译（含 dbuf、klog 标记、自测、复位路径 gate-held 守卫、补丁 0009）；`round.sh` 每次重编 6 s。**不带 `pong_dpu_flip` 时与 g151 行为一致**，可以照常跑别的实验。
- 没有任何其他进程在跑；g153（`pong_dpu_flip=1`）已取消，没跑过。

**已实现的东西与开关**（内核 `UniXNU-zizhan/drivers/PongAdreno/PongAdrenoScanout.cpp`，本地镜像 `device/PongAdreno/`）
- `pong_dpu_flip=1|2`：面板双缓冲（§9 开头）。1 = 内核物理连续内存 <4 GiB（`pong_dpu_bufs` 个，默认 3，分配失败降级 2）；2 = splash 区尾部 0xb9b36000 当第二缓冲。
- `pong_dpu_flip_klog=1`：危险步骤前后 `PONG DBUF mark: …` 行 + `pong_klog_flush(3)`（UFS，冷复位留得住）。标记顺序：`first GPU copy … offered` → `… retired` → `first flip: before the SSPP_SRC0_ADDR writes` → `SSPP written, before CTL_FLUSH` → `CTL_FLUSH written` → `first flip: done`。
- `pong_dpu_flip_test` 位：1 = 第一帧原地 flush（SRC0_ADDR 写回 console + CTL_FLUSH，标记 `test A: …`），2 = 翻到缓冲 1 停 300 ms 再翻回（`test B: …`），8 = 自测后关双缓冲。自测在第一帧 GPU 路径、合成线程、gate 持有时跑。
- `pong_dpu_probe` 位（只读探测）不变：1 复位前、2 开机（已一次性）、0x10 开 AHB、0x20 SMMU、0x80 CB。
- 协议：`PongScanoutWire.h` `reserved`→`target`；`PongMetal/PongMetalScanout.h`、`GpuScanoutPresenter.h` 用它做 destinationOffset。xnu 补丁 `patches/xnu-ipados18/0009-zizhan-dpu-flip-cpu-frame.patch`：CPU 合成前调 `pong_scanout_cpu_frame()` 翻回 console（每次构建倒回重打，`git apply --check` 已通过）。
- 复位路径（`pong_scanout_summary`，由 `ArmDTPlatform/PongBootReset.h` pinger 线程调）：现在 `IOLockTryLock` 失败就**不碰 MDSS**，只打一行。

**两次硬挂**
- g152 19:42 `pong_dpu_flip=2 pong_dpu_probe=0x12`：定屏 iOS 桌面，420 s 无 fastboot/adb、7 次踢口无效、看门狗不回。无日志（DRAM 随长按丢失）。
- g152b 19:02 `pong_dpu_flip=2 pong_dpu_flip_test=0xb pong_dpu_flip_klog=1 pong_dpu_probe=0x12`：同样挂法。这轮 GPU 没写新缓冲（test 位 8 自测后关掉），所以挂点在：寄存器写、或 klog 刷写、或复位路径。
- **关键认识：Gunyah 看门狗从不咬**。g147 的"90 s 回来"其实是 pinger 自己 `resetNow()`。只要 pinger 线程被卡（MDSS 总线不应答、或等 `gHost.lock` 被卡住的 klog 刷写），手机就永久定屏。

**假设（按可能性）与 klog 能区分什么**
1. 合成线程调 `pong_klog_flush(3)` 死锁/卡住（持 fLock+gate；UFS `gHost.lock`），到期 pinger 调 `pong_klog_flush(1)` 也卡在 `gHost.lock` → 永久挂。klog 若 state 1、标记只到 `test A: no-op flush, before the SSPP writes`（第一次刷写就是它）→ 刷写本身能成功一次，但不排除第二次卡；若 header state 0/未写 → 第一次 flush(3) 就没成。
2. `SSPP_SRC0_ADDR` 或 `CTL_FLUSH` 写让显示 AHB 死掉，合成线程卡；到期 pinger 在旧内核里 `dbRestore()` 读 MDSS 也卡。klog 标记停在 `SSPP written, before CTL_FLUSH`（SSPP 写成功、FLUSH 挂）或停在 `before the SSPP writes`（SSPP 写就挂）。g152 没有 klog 也没有自测，挂点只可能是这条或 GPU 写 splash 尾部。
3. 两段物理描述符映射给 presenter 出事（只影响 g152，g152b 也有这个映射但没挂在那——g152b 到了第一帧，说明映射没问题）。

**下一步（顺序）**
1. 读 g152b klog（见上），按标记定位。
2. 加"保底复位"：`PongBootReset.h` 再起一个独立 kernel thread，deadline+20 s 到就直接 `resetNow()`（不调 summary、不写 klog、不碰 MDSS），保证挂了也回 fastboot（DRAM ramoops 还能被 RamoopsDump 读到）。另外把 klog 刷写从合成线程挪到独立线程或干脆只在 pinger 路径刷。
3. 若是寄存器写挂：先只写 SSPP 不写 CTL_FLUSH 看是否挂；查 UEFI 侧（`uefi/`、Xiaomi 的 display DXE）怎么编程这些寄存器（时钟/qos/是否经 reg-dma）；查 Gunyah stage-2 是否对 MDSS 只读（live.dts `qcom,sde-max-trusted-vm-displays = 1`，TUI 借用机制存在，但平时归 HLOS）；考虑 `MDP_TOP CLK_CTRL` force-on 后再写。
4. 若 flip 路线走不通，退路：保留 blit 但让它不占渲染时间（第二个 ring/优先级、或把 blit 放到 CA 下一帧渲染之后错开），以及 CA 侧降低每帧 fill 数。
5. 通过后再跑 g152c（`pong_dpu_flip=2 pong_dpu_flip_klog=1`，标记括住第一次 GPU 拷贝）→ g153（`pong_dpu_flip=1`）。

**跑一轮的命令**（Mac）：`cd $ZIZHAN_SRC && bash round.sh gNNN <boot-args…> > ../logs/gNNN.run.out 2>&1`，结果 `logs/gNNN.txt`，`PONG FPS SUMMARY` 用 `fpsvote.py`。手机需在 Android 可见；换 USB 口后 `bash usbkick.sh remember`。

### g152b 的 klog 读到了（19:31 用户长按复位回 Android，`logs/g152b.klog.out`）
header state 1、reason 3，9 次 `pong_klog_flush(3)` 全部成功。标记序列完整到 `test B: done`：
- test A 原地 flush（SRC0_ADDR 写回 console + CTL_FLUSH 0x20003）：**17875 µs 后 FLUSH 回读清零**（约两帧）。
- test B 翻到缓冲 1（0xb9b36000）：`flip 1 … taken after 2 us`；翻回 console：`flip 2 … taken after 5111 us`。SSPP addr 回读随之变化。
- **结论：写 SSPP_SRC0_ADDR / CTL_FLUSH 不挂，flush 在 autorefresh 下会被取走（§7 2.2 的问题答案是"会"）。** 挂点在自测之后、本轮没有标记的区域。自测后发生的事：`dbOff`（无 MDSS 写，shown 已是 0）→ 第一帧 GPU 拷贝经**新的两段物理描述符映射**写 console（g152 则是写缓冲 1）→ 之后的 MDSS 读（`pong_scanout_cpu_frame`→`dbSettle`、到期 `dbRestore`）。
- 两个候选：(a) 带 CTL bit 17 的 flush 让 CTL 重新 latch 了我们看不到/没程序的 CTL 影子寄存器（DSC_ACTIVE、MERGE_3D、INTF 等），显示块停摆（cmd 面板 GRAM 自持所以画面仍是桌面），随后任何 MDSS 读把 CPU 卡死，pinger 也卡；(b) GPU 经新映射导入/写入时卡死（g152、g152b 都在自测后第一帧 GPU 拷贝时挂）。
- 区分实验（都要先有保底复位）：`pong_dpu_flip_test` 新位 0x10 = 自测后强制 CPU 路径（GPU 不碰新映射）——还挂则是 (a)；不挂则是 (b)。新位 4 = flush 不带 CTL bit 17（只 SSPP 位）。klog 标记要加到"自测后第一次 MDSS 读"和"第一帧 GPU 拷贝 offered/retired（不论目标）"。
- 20:00 新增（已编译）：`ArmDTPlatform/PongBootReset.h` **保底复位线程** `guard`：pinger 过了到期点（或绝对 deadline）后 `pong_autoreset_guard`（默认 20）秒内没复位，就直接 `resetNow()`（PSCI SYSTEM_RESET2 + 1 s 看门狗），不碰 IOLog/klog/MDSS；DRAM ramoops 可被 RamoopsDump 读到。`=0` 关。另：`pong_dpu_flip_test` 位 4 = flush 不带 CTL bit 17，位 0x10 = 自测后强制 CPU 路径；klog 标记加了"第一帧 GPU 拷贝 offered/retired（不论目标）"和"自测后第一次 MDSS 读 before/after"。
- g152c（20:02，`pong_dpu_flip=2 pong_dpu_flip_test=0x8 pong_dpu_flip_klog=1`）：不写任何 MDSS 寄存器，只让 GPU 经新的两段映射拷到 console → 区分假设 (b)。
- **g152c（19:36）：也硬挂**，而且保底复位线程也没把它拉回（420 s 无 fastboot）。这轮没有写任何 MDSS 寄存器 → 挂点不是 flush 后的 MDSS 读，而是 **GPU 经新的两段物理映射做第一帧拷贝**（或映射/导入本身），且是连 PSCI SYSTEM_RESET2 都救不回的整机冻结（像总线/NoC 级错误，例如 GPU 经错误页表写到受保护物理区触发 XPU）。三轮共同点：`scanout` 被换成 31 MiB 两段物理描述符、presenter 导入 31 MiB BO。下一步纯读代码：GPU 导入路径（NativeAdrenoMemory/Vm/UserClient）对页数/大小的假设、对 scanout 内存的特殊处理（coherent skips）。手机又需长按复位；`logs/g152c.klog.sh` 已挂起等 adb，回来后自动读 klog（看 `first GPU copy … offered` 后有没有 `retired`）。**在找到根因前不再跑 flip 轮。**
- 20:20：g151 日志证实 `DISP_CC_MDSS_AHB_CLK` 开机开了之后到期时仍是 0x1（不会被谁关掉），排除"AHB 又关了导致读挂"。GPU 导入路径（`NativeAdrenoMemory::importTask`）按页取物理段、支持多段，读代码看不出 31 MiB 两段导入的问题。新增 `pong_dpu_flip=3`：**只换映射**（presenter 拿 console + splash 尾部两段映射、拷到 console），完全不碰显示时钟和 MDSS 寄存器；klog 标记：第一次拷贝 offered/retired、64 次拷贝完成。g152d = `pong_dpu_flip=3 pong_dpu_flip_klog=1`，由 `logs/g152d.autorun.sh` 在 g152c 的 klog 读完后自动起跑。挂 → 纯 GPU/映射问题（再对比 mode 1 的 RAM 缓冲映射）；不挂 → 问题在 dpuBus/MDSS 读与 GPU 的组合。

### 交接记录 2（20:30，对话清空前的最终状态）
**结论到此为止**
1. DPU flip 机制本身可用：写 `SSPP_SRC0_ADDR` + `CTL_FLUSH`（0x20003）不挂，flush 在固件 autorefresh 下会被取走（g152b klog：原地 17.9 ms、翻到缓冲 1 后 2 µs、翻回 5.1 ms）。
2. 真正的挂点在"GPU 经新的 presenter 映射工作"这一半：g152c 完全不写 MDSS 也挂；保底复位（PSCI SYSTEM_RESET2）救不回 → 总线级冻结。三轮共同点 = `scanout` 换成 31 MiB 两段物理描述符（console + splash 尾部 0xb9b36000）、presenter 导入它为 31 MiB BO。
3. Gunyah 看门狗不会自己咬；手机挂了只能长按电源。每次冷复位丢 DRAM ramoops，但 `pong_dpu_flip_klog=1` 的标记在 UFS klog 里留得住。

**此刻在跑/挂着的自动化（Mac，nohup）**
- `logs/g152c.klog.sh` → 等 adb，读 g152c 的 klog 到 `logs/g152c.klog.out`（看 `first GPU copy … offered` 后有没有 `retired`）。
- `logs/g152d.autorun.sh` → 等上面 KLOG_DONE 后 10 s 自动跑 **g152d = `pong_dpu_flip=3 pong_dpu_flip_klog=1`**（只换映射，不碰显示时钟/寄存器）→ `logs/g152d.run.out`、`logs/g152d.txt`/`.klog.txt`。取消：`pkill -f g152d.autorun`。
- 若 g152d 也挂：需再长按；复位后 `python3 klog.py read ../logs/g152d.klog.txt`。下一步对比 **mode 1**（`pong_dpu_flip=1 pong_dpu_flip_klog=1`：presenter 映射 = 内核 RAM 的 IOBufferMemoryDescriptor，不含设备内存）——若 mode 1 不挂，问题就是"两段/设备内存物理描述符经 presenter 导入"，可改成 mode 1 路线（CA 的 surface 在 RAM，本来步骤 3 就要走 RAM）。若 mode 1 也挂，查 GPU 导入 >16 MiB 或 IO-coherent 对该区域的行为（`submit/NativeAdrenoMemory.cpp importTask`、`NativeAdrenoVm.cpp coherentMapping/importedUntouched`、`NativeAdrenoUserClient.cpp:269`），以及 Turnip 侧 31 MiB BO。
- 若 g152d 不挂：问题在 dbInit 的 `dpuBus`（开 AHB）/MDSS 读与 GPU 的组合——下一轮 `pong_dpu_flip=2 pong_dpu_flip_test=0x18`（不自测、自测后 CPU-only）再细分。

**代码状态**（全部已编译，`round.sh` 自动重编）
- 内核 `drivers/PongAdreno/PongAdrenoScanout.cpp`（镜像 `device/PongAdreno/`）：`pong_dpu_flip` 1/2/3、`pong_dpu_bufs`、`pong_dpu_flip_klog`、`pong_dpu_flip_test` 位 1/2/4/8/0x10、保底复位守卫；`drivers/ArmDTPlatform/PongBootReset.h`（镜像 `device/ArmDTPlatform/`）：`pong_autoreset_guard`（默认 20 s）。补丁 `patches/xnu-ipados18/0009-zizhan-dpu-flip-cpu-frame.patch`（镜像 `device/patches/xnu-ipados18/`）。
- 用户态：手机上是 **g152 插件**（`PongScanoutFrame.target`），对任何内核兼容；备份 TAG g152。
- **不带 `pong_dpu_flip` 的内核与 g151 完全一致**，其他实验照常。
- 本地 repo 未 git commit（一直是未跟踪状态，和之前一样）。

### 21:15 更正：g152～g152d 四轮"硬挂"很可能都是假象——命令行里没有 `pong_autoreset`
- 复核 `logs/g15*.build.out`：g145～g151 都带 `pong_autoreset=300 pong_autoreset_sb=30`，而 **g152、g152b、g152c、g152d 四轮都没有**（`round.sh RUN 参数…` 时忘了带；手机端 launchd 打印的 boot-args 也证实没有）。`PongBootReset.h:201` 没有这个参数就直接返回：pinger、deadline 复位、守卫线程、deadline 前的 klog 刷盘统统没有装上。
- 因此"420 s 不回 fastboot、踢口无效、看门狗/守卫都救不回"在这四轮里是**必然现象**，不能说明 SoC 冻结；iOS 下 USB 本来就不枚举。用户 19:15 看到的"定在 iOS 桌面"同样可以是正常运行（mode 2 若 flip 没生效，屏幕会一直显示缓冲 0，看起来像定格）。
- g152c 的 klog（21:06 读出）与此一致：`first GPU copy … offered` → `GPU frame completed sequence=1` → `retired` → `actual GPU scanout copy completed`，之后再无内容——因为没有 deadline 刷盘，UFS klog 只在 mark 处刷，正常运行也会这样。**即：GPU 经 31 MiB 两段映射的首帧拷贝是成功的。** 交接记录 2 里"总线级冻结"的推断撤回。
- 处置：g152d（mode 3，21:06:49 起，同样无 autoreset）此刻大概率正常跑在 iOS 里，需要用户 Power+VolDown ~10 s 强制回 fastboot。Mac 上 `logs/g152d.collect.sh`（nohup，输出 `logs/g152d.collect.out`）等 g152d 的 cycle.sh 退出后：收 DRAM ramoops（`g152d.ramoops.txt`，若其中 DCVS 10 s 报告持续了几分钟即证明手机一直活着）+ klog，然后自动跑 **g152e = `pong_dpu_flip=3 pong_autoreset=300 pong_autoreset_sb=60 pong_dpu_flip_klog=1`**，有日志再跑 **g152f = `pong_dpu_flip=2`（完整翻页）**。
- 教训：`round.sh` 的调用方必须自带 `pong_autoreset=… pong_autoreset_sb=…`；后续应把它加进 round.sh 的默认参数（见下一条）。

### 22:05 带看门狗重跑：映射与完整 DPU 翻页都通了（g152e/g152f）
- **g152d**（mode 3，无 autoreset，用户 21:51 直接重启回安卓）klog：`64 GPU copies through the two-range mapping done`，DCVS 报告正常——手机一直活着。
- **g152e** = `pong_dpu_flip=3 pong_autoreset=300 pong_autoreset_sb=60`：106 s 自己回 fastboot，64 次拷贝完成，klog reason=deadline。两段 31 MiB 映射彻底无罪。
- **g152f** = `pong_dpu_flip=2`（完整双缓冲 + SSPP_SRC0_ADDR/CTL_FLUSH 翻页）：同样 106 s 正常回来。**翻页工作**：SpringBoard 后 60 s 内 `flips 516 taken 515`，首翻 2 µs 取走，之后 3–6 ms；`waits 61 (max 15 ms)`、`in-place 3`、`cpu-flips 0`，latch 最大值 165 ms 只出现一次（早期）。`logs/g152f.txt`。
- 所以步骤 2（内核双缓冲 + DPU flip）已验证；下一步是步骤 3（直接翻到 CA 的 surface，去掉 2.5 ms blit）。**还没有人看过翻页时的屏幕**（撕裂/错帧），下一轮请用户看一眼。

### 22:00 两项"回得来"保险（用户要求"两条都加上"）
1. **内核默认看门狗**：`PongBootReset.h start()`：没有 `pong_autoreset` 参数时也按 300 s 装（`DefaultDeadline`），打印 `PONG_AUTORESET: no pong_autoreset boot-arg: using the default 300s`；只有显式 `pong_autoreset=0` 才关（打印 off）。`round.sh` 另外在调用方没给时补 `pong_autoreset=300 pong_autoreset_sb=60`。
2. **长按电源 3 s 软复位**：新补丁 `patches/xnu-ipados18/0010-pong-power-key-reset.patch`（`bsd/dev/pong_buttons.c`：轮询线程里电源键按住 ≥ `pong_button_reset` ms，默认 3000，0 关；到时调用 `pong_button_reset_hook`）。kext 侧 `PongBootReset.h buttonReset()`（弱引用该 hook，老内核没有也能链接）：若 `pong_autoreset=0` 没写过 SDAM 则先 `markBootloader`，打 `pong_scanout_summary`，`pong_klog_flush(4)`（klog.py 显示 reason=power-key），然后 `resetNow()`（PSCI SYSTEM_RESET2 暖复位 + 1 s 看门狗兜底）→ fastboot，DRAM/klog 都在。比 PMIC 10 s 硬组合键快，且走的是和 deadline 相同的路径。iOS 自身也会收到电源键事件（出现关机滑块无所谓，马上复位）。
- 镜像：`device/ArmDTPlatform/PongBootReset.h`、`device/patches/xnu-ipados18/0010-*.patch`、`tools/mac/klog.py`、`tools/mac/round.sh`、`tools/mac/g152d.collect.sh`、`tools/mac/g152g.sh`。
- 验证轮 **g152g**（`logs/g152g.sh`：命令行**不带** `pong_autoreset`，带 `pong_dpu_flip=2`）：期待日志 `PONG_AUTORESET: no pong_autoreset boot-arg: using the default 300s`；若用户在桌面出现后长按电源 3 s，期待 `Pong keys: power held 3000 ms: resetting` + `PONG_AUTORESET: power key held` 并提前回 fastboot，klog reason=power-key；否则 300 s 到点回来。第一次打包失败是 klog.py 的 f-string 语法（已修），内核 6 s 增量构建已含 0010 与 kext 改动（`strings` 查到两处新字符串）。
- **g152g 结果（22:06）**：命令行无 `pong_autoreset`，内核打印 `PONG_AUTORESET: no pong_autoreset boot-arg: using the default 300s`，300 s 到点自己回 fastboot，klog reason=deadline → **默认看门狗验证通过**。翻页 300 s 内 `flips 2764 taken 2764`，`waits 68 (max 15 ms)`，无 cpu-flips。按键驱动 `power=1 up=1 down=1` 正常，但整轮没有任何 `Pong keys: … down/up`（用户没按），所以**长按电源 3 s 复位还没在真机验证**：下一轮任何时候按住电源 3 s，看 `Pong keys: power held 3000 ms: resetting` + klog reason=power-key 即可。

### 22:12 g152h：用户在旁验收——画面正常、长按电源 3 s 复位通过
- g152h = `pong_dpu_flip=2 pong_autoreset=600 pong_autoreset_sb=240`。用户反馈：**画面正常没撕裂，滑动顺，动画比之前好多了**；桌面左右翻页动画偶有一下卡顿（用户猜分辨率/缩放）。
- **按键复位验证通过**：日志顺序 `Pong keys: 0 down` → `Pong keys: power held 3000 ms: resetting` → `PONG_AUTORESET: power key held: resetting into fastboot now` → DBUF 汇总 → klog `reason power-key` → 开机后 187 s 回 fastboot（到点本应 256 s），cycle.sh 正常收到日志。至此三条回路：到点（默认 300 s）、panic、长按电源 3 s。
- 翻页统计（约 170 s 含用户操作）：`flips 1324 taken 1324`，`waits 642 (max 15 ms)`，`in-place 27`，`cpu-flips 0`；latch max 357 ms 出现过一次（用户操作期间，待查是否对应其感到的卡顿），汇总行里的 54 s 是按键定屏后最后一帧未取走的度量，忽略。
- 桌面翻页卡顿的已知背景：第 6 节测得滑动帧是 GPU 填充受限（9–10 个全屏等效填充 + 2.5 ms blit 同一 ring，~60 fps 边缘），第 3 步去 blit 后再用 `pong_gtrace` 对翻页动画专门量一次，再决定是否动 display-scale。

## 10. 第 3 步（零拷贝：直接翻 CA 的 surface）开工（22:20 起）
- 22:20 用户确认工作方式：不用逐项确认，能做多少做多少；需要人眼/人手的项攒到下面"待用户验证"里一起做。
- 并行：研究子任务（0234 设计、MiniIOSurface 分配、presenter 如何指认 surface、VIG 布局匹配）→ `docs/plan/2026-10-31-zero-copy-step3.md`；同时跑 **g152i = `pong_dpu_flip=1`**（内核 RAM 里 <4 GiB 的连续缓冲、3 缓冲）探 DPU 能否扫 splash 区以外的普通 DRAM（XPU 是否放行）——第 3 步的前提。

- **g152s（23:21，`pong_zc=2`）：零拷贝翻页工作。** `PONG ZC: frame 2: buffer 4 at 0xaf300000 stride 8704, fence 12/12 after 1 us, shown after 1 us`；262 个零拷贝帧（隔帧——只有 surface 17 在槽里），fence 等待平均 1.4 ms/最大 8 ms、无超时；latch 等待平均 8.6 ms/最大 16.6 ms（= 同步等到下一个 vsync）；`in-place 18`（启动动画阶段 CA 画进了正在扫出的画布 18 次，之后为 0）；`PONG SCANOUT TIMING user=` 从 3.3–4.0 ms 降到 ~1.0 ms（隔帧仍走拷贝）。没有合成滑动，fps 不可比。
- 槽漏发的原因：miss 日志显示 backboardd 另有一张 `'ARGB' bpr 8640 alloc 16556032` 和 SpringBoard 的 `'ARGB' 8704` 面板尺寸 surface（都不是画布，格式不同，正确拒绝）；而 swap 伙伴 surface 18（'BGRA'）根本没进 0011 的判定 → 它应是 framebuffer 打开时内核自建的 `MiniIOSurfaceCreateSimple` 表面（bpr 8640，`proc_selfname` = kernel_task）。0011 放宽：`backboardd` 或 `kernel_task` 都可拿槽，去掉"必须指定 bpr"的条件（bzero 问题已除，当初 g152l 的冻结不再适用）。
- 23:24 **A/B 批 `logs/g152t.sh`**：g152t = `pong_dpu_flip=1 pong_zc=2 pong_touch_synth=2`，g152u = 同配置 `pong_zc=0`（走拷贝）。比较 `PONG FPS SUMMARY` 的 moving fps 与 `PONG SCANOUT TIMING user=`、`PONG ZC … in-place`。

- **g152t/g152u（23:24–23:33，合成滑动 A/B）：零拷贝全帧生效，撕裂计数 0，显示出的独立帧数多 59%，但 CA 侧产帧率更低。**
  - g152t（`pong_zc=2`）：两张画布都进槽（17 = CA 画布 bpr 8704 @0xaf300000；18 = framebuffer 内核自建表面 bpr 8640 @0xb0400000，`kernel_task` 放行后拿到）；**2641 帧全部零拷贝、全部被取走（taken 2641/2642）、in-place 0**；fence 等待平均 0.9 ms（最大 22 ms）、latch 等待平均 13.4 ms（最大 16.6 ms）；`PONG FPS SUMMARY moving 48.1 fps`，节拍分布 `0/1953/0/518/0/30/22`：74% 的帧恰好 2 拍（16.7 ms = 60 Hz）、20% 恰好 4 拍（33 ms）——被 vsync 严格量化。
  - g152u（`pong_zc=0`，拷贝）：moving 55.2 fps，但 `flips 2888 taken 1662`：**1226 次翻页在 latch 前被下一帧覆盖，这些帧从未显示**；节拍乱（off 1569）。按"真正显示出的独立帧"算：零拷贝 2641 vs 拷贝 1662。
  - 解读：面板此刻按 60 Hz 扫（latch 周期 16.7 ms）。零拷贝路径里 `show()` 同步等到 latch 才返回，CA 因此每帧都对齐 vsync，渲染+等待超过 16.7 ms 的帧掉到 30 fps（20%）；而拷贝路径 CA 不等任何东西，产帧多但多数没显示。要在零拷贝下继续提高，只有两条路：**(a) 三缓冲**——CA 现在只建一张画布 + 借 framebuffer 默认表面当第二张（swap 17/18 交替），若能让 CA 用 3 张就不必等 latch 就能画下一帧；**(b) 面板 120 Hz 模式**（latch 周期减半）。
  - 发现：CA 每帧都调 `SwapWait`（`tl: w N 0x80000001` 紧跟 `s N`，`tl: w N 0x80000000` 在下一帧前），我们的实现是空操作；若实现成"等该 swap 的翻页 latch"，就能把等待从 `show()` 挪到 CA 自己的线程里（更干净，吞吐相同）。
  - 现状结论：`pong_dpu_flip=1 pong_zc=2` 为推荐配置（去掉每帧 2.5 ms blit、无撕裂、显示帧更多），等用户目测后再设为默认。

- 23:50 研究备忘 `docs/plan/2026-10-31-triple-buffer-swapwait.md`：(1) 没有任何属性能直接让 CA 多建画布；CA 的画布数是动态的，取决于它认为哪些 surface "还在用"——我们的 `SwapWait` 永远成功、显示持有也不计入 `useCount`，所以两张就够它用；iOSPlus10 0254 把 `SwapWait` 改成上屏前返回 Busy 后，"CoreAnimation keeps 3 surfaces"。(2) `SwapWait(id, flags)`：bit31 = 非阻塞查询；type 1 = 该 swap 已上屏；type 0 = 已被后一帧替换（其画布空闲）。CA 每帧 `s N` → `w N 0x80000001` → ~16 ms 后 `w N 0x80000000` → `s N+1`。(3) 面板现在 60 Hz（固件 splash 留在 timing@0），DT 里有 120 Hz 时序，切换要走 DSI `timing-switch-command` + INTF tearcheck 重编程，先做只读探测。
- 23:53 **补丁 0012 `fb_swapwait`**（`edit_0012.py`，MiniIOMobileFramebuffer.cpp）：记录 `fEndedID/fPresentedID/fRetiredID`（SwapEnd 时、`show()` 开头把前一帧标为已上屏、`show()` 末尾把本帧标为已上屏——零拷贝帧此时 flip 已 latch），`SwapWait` 查询未就绪返回 `kIOReturnNotReady`（`fb_swapwait=2` 则 Busy），阻塞调用最多等 50 ms；`tl: w` 行带返回值；5 s 统计行加 NotReady/超时计数。**g152v** = `pong_dpu_flip=1 pong_zc=2 fb_swapwait=1 touch_lat=1 pong_touch_synth=2`（命令行顶到 1023 字节上限，`pong_dpu_bufs=2`/klog_period 只好去掉）。看：CA 收到 NotReady 后是重查、延后 SwapEnd，还是另建第三张画布（`canvas slot NONE FREE for` 或 miss 日志）。

- **g152v（23:54，`fb_swapwait=1`）：CA 收到真实应答后真的去建第三张画布。** `PONG DBUF: canvas slot NONE FREE for 0x0 (2 taken so far)`——第三张 backboardd BGRA 画布来要槽，但只剩 2 槽（dbuf 自占 3）。应答统计：`w N 0x80000001` 2241 次成功 / 1576 次 NotReady，`w N 0x80000000` 1837 次 NotReady / 280 次成功；CA 对 NotReady 的反应是照常 `SwapEnd N+1`（不阻塞），并多建一张画布换着用。因第三张落在普通 RAM 走拷贝路径，这轮 fps 反而 43.2。→ **g152w** = 同配置 + `pong_dpu_bufs=2`（dbuf 自占 2，留 3 槽），期望三张画布全部进槽、beats 分布离开 2×8.33 ms。

- **g152w（23:58，`pong_dpu_bufs=2` → 3 槽）：三张画布全部进槽，全程零拷贝**（`canvas slot given … (3 taken so far)`，`PONG SCANOUT TIMING … user=0`，presenter 完全不工作）。fps 45.5，节拍仍是 `0/1005/0/342/0/23/22`（67% 两拍、23% 四拍）：三缓冲没有改变量化，因为 `show()` 仍同步等 latch，presenter 线程一帧一 vsync 串行；四拍帧 = 该帧（CA 渲染 + fence）没赶上下一个 vsync。下一步提速只剩两条：去掉 `show()` 的 latch 等待（memo §2.4 第 3 步：内核同时 hold 在屏与待 latch 两张、retire 延后）+ 面板 120 Hz。

- 00:02 **默认值切换**（`edit_defaults.py` + 0012 重生成）：内核默认 `pong_dpu_flip=1`（有池用池，无池回落 splash 双缓冲）、`pong_dpu_bufs=2`、`pong_zc=2`、`fb_swapwait=1`。旧行为：`pong_dpu_flip=2 pong_zc=0 fb_swapwait=0`。原因：命令行已顶到 1023 字节，不可能每轮都带这些参数。**g152x** = 不带任何显示参数只加 `pong_touch_synth=2`，验证默认值生效。

- **g152x（00:02，默认值验证）通过**：不带任何显示参数 → `panel pool … 2 buffers, 3 canvas slots (pong_zc=2)`、画布进槽、`SwapWait: 276 not ready`、`user=0`（无拷贝）、moving 44.4 fps（节拍 834 两拍 / 336 四拍，同 g152w）。**从此每一轮默认就是零拷贝三缓冲。**

### 第 3 步收官小结（00:05）
- 链路：loader 96 MiB 面板池（`/chosen/pong-panel-pool`）→ 内核 dbuf mode 1（2 自用缓冲）+ 3 画布槽 → 0011 把 backboardd/内核自建的 2160×1916 BGRA 画布放进槽（copy-back 映射、不清零）→ `pong_scanout_frame` 认出槽 → `zcFlip`：等 GPU fence、写 `SSPP_SRC0_ADDR`/`YSTRIDE0`/`CTL_FLUSH`、等 latch → 0012 让 `SwapWait` 如实回答，CA 因此自建第三张画布。presenter 的 2.5 ms blit 彻底消失（`user=0`）。
- 数字：零拷贝下每一帧都被显示（taken == flips，in-place 0）；合成滑动 44–48 fps，全部量化到 2 或 4 个 8.33 ms 节拍（面板 60 Hz latch）。拷贝路径名义 55 fps 但近半数帧从未上屏。
- 剩下的提速杠杆（按优先级）：(1) `show()` 不再同步等 latch（memo `2026-10-31-triple-buffer-swapwait.md` §2.3/§2.4 第 3 步：内核 hold 在屏 + 待 latch 两张，retire 延后到 latch，SwapWait 已就位）；(2) 面板 120 Hz（memo §3：先只读探 INTF_1 tearcheck，再 DSI timing-switch）；(3) 再用 `pong_gtrace` 看四拍帧到底是渲染超时还是 fence 等待。
- 已知不完美：复用槽时会闪一帧旧画面（不清零）；`pong_klog_period` 只在排查时开；命令行长度已顶到 1023 字节，新开关尽量做成内核默认值。

## 待用户验证（攒着，下次来时一起）
- [ ] **零拷贝三缓冲画面 + 手感对比**（现在是默认值）：`round.sh gXXX pong_autoreset=600 pong_autoreset_sb=240` 看画面；再跑 `round.sh gYYY pong_dpu_flip=2 pong_zc=0 fb_swapwait=0 pong_autoreset=600 pong_autoreset_sb=240`（旧拷贝路径）对比滑动手感（零拷贝：每帧对齐 vsync、无撕裂但重帧会掉到 30 fps；拷贝：产帧多但很多帧没显示）。，桌面出来后左右滑、开关 App，看有没有撕裂/闪烁/错帧（`PONG ZC … in-place N` 计数对应 CA 画进正在扫出的画布的次数，启动阶段有 18 次，之后应为 0）；对比 `pong_zc=0` 的手感。按电源 3 秒结束。
- **g152i（22:24，`pong_dpu_flip=1`）：mode 1 分配失败**——`no contiguous 47 MiB below 4 GiB`、`no contiguous 31 MiB below 4 GiB`，自动回落 mode 2（splash 区）照常工作（516/516）。结论：开机后再向内核要几十 MiB 的 <4 GiB 物理连续内存不可靠（碎片），第 3 步若要 CA 的 surface 物理连续，必须走**开机时预留（carve-out）的面板池**，而"DPU 能否扫 splash 以外的普通 DRAM"仍未验证——需要先用预留池做一次 mode 1 实验。
- 22:35 研究备忘出炉：`docs/plan/2026-10-31-zero-copy-step3.md`。关键事实：**XNU 在这台机上没有任何 4 GiB 以下的页**（DarwinBoot 的 4 GiB 窗口和追加 bank 都在 0x1_0000_0000 以上，`DarwinBoot.c` ClaimPongRam 有意钳位），所以 g152i 的失败是必然，不是碎片。路线：loader 照 `pong-audio-dma` 模式预留 48 MiB 面板池（EfiReservedMemoryType，<4 GiB），ADT `/chosen/pong-panel-pool {pa,bytes}`，内核 `withPhysicalAddress` 包起来；CA 的 surface 指认今天已是 `(IOMemoryDescriptor*, offset)`，缺的只是 GPU 完成 fence 和 SwapWait/latch 闸门；VIG 格式 0x237ff = 线性 4×8bit，无需像素转换。
- 22:42 **3.0 实现**（已改、在编）：`DarwinBoot.c` `PANEL_POOL_PAGES=12288`、`mPanelPoolOffset`、AllocatePages(AllocateMaxAddress<4 GiB, Reserved) + 回填 + Fail 释放；`make-pong-adt-ipados18.py` 加 `('pong-panel-pool', bytes(16))`；`PongAdrenoScanout.cpp` dbInit mode 1 先读 lease（`pong_panel_pool=0` 忽略），`dbPool=withPhysicalAddress`，最多 3 槽，再退回原分配。批 `logs/g152j.sh`：重编 DarwinBoot24（`scripts/build-darwinboot24-macos.py`）→ g152j `pong_dpu_flip=1 pong_dpu_flip_test=0xb`（只做寄存器自测：翻到池 300 ms 再翻回，不让 GPU 用池）→ 成功则 g152k `pong_dpu_flip=1` 全量。镜像 device/DarwinBoot24、device/scripts、device/PongAdreno、tools/mac/g152j.sh。
- **g152j（22:44，`pong_dpu_flip=1 pong_dpu_flip_test=0xb`）：面板池路线成立。** loader 预留到 `0xe5800000`（48 MiB），内核 `PONG DBUF: panel pool … 3 buffers`，`on (mode 1): 4 buffers 0xb8000000 0xe5800000 0xe67ca000 0xe7794000`；自测翻到池缓冲 **1373 µs 被取走**、翻回 11 ms，之后 MDSS 读正常，整轮按时回来。→ DPU 读 splash 区以外的普通低端 DRAM 没有 XPU 障碍。g152k（全量 mode 1，GPU 写池 + 翻页）接着跑。
- 22:48 **3.1 实现**：池扩到 80 MiB（`PANEL_POOL_PAGES=20480`，5 槽）；`PongAdrenoScanout.cpp` 新增 `pong_panel_slot_take/give`（dbuf 自用 ≤3 槽，其余给画布；boot-arg `pong_zc=1` 开，几何必须 = 面板 2160×1916 stride 8640）；新补丁 `0011-zizhan-panel-pool-canvases.patch`：`MiniIOSurface::surfaceCreate` 里 backboardd 的 BGRA/bpe4/无 plane/offset 0 画布向 kext 要槽，拿到就 `client=withPhysicalAddress(slot)`（和"包客户内存"同一分叉：`memory` 只剩状态页，用户映射走 `pixelMap`），清零+flush；`free()` 归还槽；`.h` 加 `slotPhys`。批 `logs/g152l.sh`（等 g152k 完 → 重编 loader → g152l `pong_dpu_flip=1 pong_zc=1`，blit 照旧）：看 `MiniIOSurface: backboardd canvas 2160x1916 bpr 8640 … in panel slot 0x…`、`swap 1 (#0)` 后画面正常、fps 与 g152k 持平。
- **g152k（22:46，`pong_dpu_flip=1` 全量，池里 3 缓冲 + GPU 写池）：正常。** `flips 555 taken 489`、`waits 0`、`in-place 0`，首翻 2 µs；三缓冲下 presenter 从不等待。mode 1（面板池）从此可作为默认翻页模式（替代 splash 尾部的 mode 2）。
- **g152l（22:49，80 MiB 池 + 0011 + `pong_zc=1`）：哑轮。** 146 s 回来（与 sb=90 到点时间一致，也与"挂死后 60 s 看门狗咬"一致，分不清），**klog 没写（state 0）**，DRAM 副本只有 316 KB 且严重翻位，最后一行是 scanout worker ready 后的第一条 DCVS 报告；没有 `PONG_AUTORESET`/`PONG KLOG`/`PONG DBUF: on` 可读行。唯一可读的新信息：`PONG DBUF: canvas 2160x1916 bpr 8704 (…) does not fit a slot (… bpr 8640)` → **CA 画布行距是 8704（256 对齐），不是 8640**，槽被拒绝，所以 0011 的分配分支根本没执行。嫌疑：(a) 80 MiB 池落到了另一处低端地址（48 MiB 时在 0xe5800000）；(b) 0011/`pong_zc` 代码；(c) 单纯热翻位+偶发。排查批 `logs/g152m.sh`：g152m = `pong_dpu_flip=1`（80 MiB 池、zc 关），g152n = `pong_dpu_flip=2 pong_zc=1`（不用池、0011 活），都带 `pong_dpu_flip_klog=1`（首拷贝/首翻的 mark 会把整段控制台刷进 UFS klog，含"panel pool 0x…"行）。
- 槽几何待改（等排查结果后一起上）：槽步长改为 ≥ 8704×1916 = 0xFE7000（取 17 MiB = 0x1100000），池 96 MiB（5 槽）；`slot_take` 接受 bpr ≥ 8640 且 64 对齐，记录每槽 bpr；`dbFlip` 翻到行距不同的缓冲时同时写 `SSPP_YSTRIDE0`。
- **g152m/g152n（22:54/22:57）都正常**：g152m（`pong_dpu_flip=1`，80 MiB 池落在 `0xad000000`，3 缓冲 + 2 槽）533 翻页、in-place 0；g152n（`pong_dpu_flip=2 pong_zc=1`，0011 代码活、不碰池）512/512。→ 池的新位置和 0011 本身都不是 g152l 哑掉的原因；g152l 独有的是"pong_zc=1 + 池"下的槽拒绝路径（或偶发）。处理：先上行距修正（`edit_zc1b.py`：池 96 MiB、槽步长 17 MiB、`SSPP_YSTRIDE0` 随翻页写、槽登记进 `db[]` 供 3.2 用），重跑同配置 **g152o**（`pong_dpu_flip=1 pong_zc=1`）看画布是否进槽、是否再哑。
- **g152o（23:00，行距修正 + 96 MiB 池 + `pong_zc=1`）：又哑了**，和 g152l 一模一样（klog state 0、DRAM 副本 307 KB 全是乱码、176 s 回来 ≈ 76 s 到点 + 60 s 看门狗咬 → pinger 已死，是硬看门狗冷咬）。对比：g152m（池、zc=0）和 g152n（zc=1、无池）都好 → 触发条件 = `pong_panel_slot_take` 越过第一行真正执行（g152l 只走到几何拒绝+日志就哑了，g152o 走到发槽）。两条路径共有的只有 `dpuLine`（IOLog + `pong_ramoops_diagnostic_write`）在 backboardd 线程的 `surfaceCreate` 上下文里被调用。实验 **g152p**：槽函数只用 IOLog（`edit_zc1c.py`），其余同 g152o。
- **g152p（23:06，槽函数只 IOLog）：仍哑**（klog state 0、348 KB 乱码、146 s）。排除 dpuLine。再审 g152l：当时槽几何要求 bpr==8640，CA 画布 8704 被拒，但**内核自建的 framebuffer surface（`MiniIOSurfaceCreateSimple(2160,1916,'BGRA')`，bpr 恰为 8640）若在 backboardd 线程上下文创建就会拿到槽**——所以 g152l 多半也发了槽，三轮哑掉的共同点其实是"某个 surface 的像素被换成池里的 `withPhysicalAddress` 内存"。待证实。
- 23:11 **诊断手段 + 收窄**：`PongBootReset.h` 守卫线程支持 `pong_klog_period=<s>`（每 s 秒 `pong_klog_flush(5)`，klog.py 显示 periodic），冷咬也能留下几乎完整的控制台；0011 重生成：只对显式给了 `IOSurfaceBytesPerRow` 的 surface 发槽（CA 画布 8704 有，内核 CreateSimple 没有）。**g152q** = `pong_dpu_flip=1 pong_zc=1 pong_klog_period=3`。
- **g152q（23:11）：周期 klog 生效**（reason periodic，2971 行完整控制台）。冻结点：backboardd 的 Metal 设备建好、`scanout worker ready` 之后 ≤3 s，正是 framebuffer/画布创建与首帧之际；没有任何 `canvas slot`/`MiniIOSurface: backboardd canvas` 行留下来（可能在最后 3 s 窗口里）。**假设**：槽内存是 XNU 不管的物理页，`createMappingInTask(kernel_task, 0, kIOMapAnywhere)` 的默认缓存属性是 device 类，随后 `bzero` 16.7 MB（ARM64 bzero 用 `dc zva`）在 device 内存上触发同步异常/卡死，而 panic 路径此时拿不到 UFS 完成中断，无法暖复位 → 60 s 后冷咬。0234 当年正是显式用 `kIOMapCopybackCache` 映射槽的。
- 23:15 **修正**（`edit_zc1d.py`，0011 重生成）：内核映射与 `pixelMap` 用户映射都加 `kIOMapCopybackCache`；去掉 bzero/flush（loader 已清零；复用槽会闪一帧旧画面，先接受）。**g152r** 同 g152q 配置。
- **g152r（23:16）：3.1 通了，根因确认。** `canvas slot given 0xaf300000`、`MiniIOSurface: backboardd canvas 2160x1916 bpr 8704 (16678912 bytes) in panel slot 0xaf300000`，之后 swap 1..8 在 surface 17/18 间交替、首拷贝/首翻正常、`flips 548 taken 497`、moving 71–78 fps，整轮到点回来。→ 四轮哑掉 = **默认缓存属性下映射池内存 + bzero（dc zva）**；改 `kIOMapCopybackCache` 并去掉 bzero 即好。遗留：只有 surface 17 进了槽，surface 18 没有任何 `canvas`/`does not fit` 行 → 它的创建没走进 0011 的判定（props 不同？），0011 已加"面板尺寸却没进槽"的原因日志（`gSlotMisses`）。
- 23:20 **3.2 上机**（`edit_zc2.py`）：`pong_zc=2` 时 `pong_scanout_frame` 认出 source 的物理首页 = 某槽 → `zcFlip`：等 GPU fence（`pong_gpu_fences`：ring `lastSubmitted` vs fence page，≤50 ms）→ `dbFlip(槽)` → 等 latch（≤20 ms）→ 以"已完成"的 pending 走 `finish()` 做统计；`PONG ZC 64/deadline:` 行汇总 in-place/fence/latch。**g152s** = `pong_dpu_flip=1 pong_zc=2 pong_klog_period=3`。期望：`PONG ZC: frame 1: buffer N at 0xaf300000 stride 8704 …`，`PONG SCANOUT TIMING … user=` 下降（当前只有一张画布在槽里，所以隔帧零拷贝）。

> 整理版（起因、逐轮表、根因、最终通路、开关默认值、数字、清单、教训、待验证、下一步）见 `docs/progress/2026-11-01-zero-copy-and-return-paths.md`。
