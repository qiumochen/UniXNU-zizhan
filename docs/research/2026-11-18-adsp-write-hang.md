# ADSP 共享内存 WRITE 间歇挂死调研

日期：2026-11-18（UTC+8）。源码基线：`f385720`；本文初稿是本次唯一新增的仓库文件，根因更新后另新增 `docs/research/2026-11-18-kernel-stack-audit.md`。本文只做只读源码、DT、日志与公开内核对照，不包含手机操作、不运行 `adb`/`fastboot`/任何 cycle 或安装脚本，也不构建内核或镜像。

> **2026-11-18 02:36 结论更正（优先阅读）**
>
> 根因已经由 `n18c-p-1`（PRM34 dry，`zizhan_write_probe=1`）的完整 panic 确认：**不是 SMMU/ADSP 共享内存故障，也不是 checkpoint flush 或 100 ms 超时，而是内核栈溢出**。panic 原文是 `panic(cpu 5 caller ...): Invalid kernel stack pointer (probable overflow)`，panicked thread 为 `kernel_task` tid 200；协调者用 `work/m1-firstsound/build-wp/kernel` 的 `atos` 得到调用链 `Call_continuation → worker → prepare → zizhan_cs35l41_native_pulse_transfer → silent_run → zero_transfer → write_commands → IOLog → _IOLogv → os_log_with_args → _os_log_with_args_internal → log_queue_log → log_payload_send → __firehose_buffer_tracepoint_reserve → oslogwakeup → selwakeup → knote → knote_post → knote_enqueue → kqueue_threadreq_initiate`。
>
> 旧帧：PRM worker `0x1330`（4912 B）、`prepare` `0x1950`（6480 B）、`silent_run` `0x470`（1136 B）、`zero_transfer` `0x50`（80 B）、`write_commands` `0x540`（1344 B），再加上 IOLog→kqueue 的 `0x710`（1808 B），合计 **0x3d90 = 15760 B**，只剩约 624 B 余量；只要 `logd` 正在等日志、`IOLog` 走进 kqueue 唤醒，就越过 16 KiB 栈。溢出后的 panic 经常也写不完，所以大多数轮表现为 Gunyah 冷咬而不是完整 panic。
>
> 修复：`8d18d71` 把 `prepare` 的 amp 回执移到清零的静态存储，并给 PRM packet builder 加 `noinline`；`prepare` `0x1950→0x360`、worker `0x1330→0x540`。修复后同一链为 `0x540+0x360+0x470+0x50+0x4e0+0x710 = 0x1950 = 6480 B`。`1b535fd` 是默认关闭的 WRITE 延迟/slow probe 与 ADSP fatal klog，`a259237` 是 porting 记录；修复内核 sha256 为 `b698195262435c57427defdce8f7f9f9a61e92e26f1ba6db169e53704188a603`。**修复后 r（PRM34 dry）、s（PRM33）、t（PRM34 dry+checkpoint）全部 PASS**：r 4 条、s 2 条、t 4 条 WRITE 都是 `done≈10–12 ms`，t 的 checkpoint flush 也都返回（`flushed=0 us≈14–21 ms`）。
>
> §0–§11 保留为“发现根因前”的排除过程；**§12 是最终根因**，§13 是参考。新的栈审计见 `docs/research/2026-11-18-kernel-stack-audit.md`。

> **阅读提示**：下面 §0–§11 中的 H1（16 KiB 越界）、H2（SMMU fault）、H8（checkpoint flush）、100 ms 超时、H13（“首块 WRITE 路径挂住 SoC”）都是根因发现前的假设/症状描述；它们的排除过程仍有价值，但不要再把它们当作根因。

## 0. 结论摘要

先把四个问题直接回答：

1. **原厂/Android 的音频共享内存路径**：`qcom,msm-audio-ion` 把音频 dma-buf 映射进 apps SMMU 的 **SID `0x1801`**；`qcom,smmu-sid-mask = <0x00 0x0f>` 是**从 SID 派生地址高位 tag 时使用的掩码**（`sid = args[0] & mask`），不是 SMR mask。厂商代码取 `sid = 0x1801 & 0xf = 1`，把 `1 << 32` 或进地址高位；低 32 位是 IOVA。主线 `q6apm-dai` 用同一个 `SID_MASK_DEFAULT = 0xF` 和 `(sid << 32)`。[事实 + 推断：DSP 如何消费高位 tag 没有固件源码]
2. **是否需要 `qcom_scm_assign_mem`/`hyp_assign`**：ADSP 音频是 **stage-1 + iommus** 模型，公开主线只对 **modem/mDSP 的 stage-2 保护上下文** 使用 SCM/VMID 分配；2026-08 的 `q6apm-dai` stage-2 提案明确写了这一点。本机 live DT 的 `msm-audio-ion` 有 `iommus = <&apps_smmu 0x1801 0x0>`，apps SMMU 的 `ID1.NUMS2CB = 0`，没有证据要求给这块 UEFI 64 KiB 缓冲做 SCM/hyp assign。不要盲目加 `hyp_assign`。[事实 + 推断]
3. **我们的 SMMU context 与 Linux 的关键差异**：我们实际是 **terminate**（`SCTLR=0x27`，`CFCFG=0`），不是 stall；同时 `CFIE=0`、没有清 FSR/写 `CB_RESUME`、没有全局 `sCR0/sGFSR` 检查、没有 `CB_ACTLR` 读取、没有 LPASS TBU 管理。Linux 的 Waipio 音频域按默认属性是 **stall + CFIE + CFRE**，并带 `qcom,context-fault-retry`：故障时 invalidate context TLB、清 FSR、写 `RESUME`。这些差异决定“一次故障会不会变成 DSP/NoC 挂死”，但不能单独证明本次挂死就是它们造成的。[事实 + 推断]
4. **IPCC 路由**：代码审查没有发现能让整机挂死的 IRQ 死锁。`drain` 每次最多 32 条、先 clear 再 dispatch，回调只做原子计数和 `thread_wakeup`；`sheng_adsp_send` 只入队并 wake，真正 GLINK 发送在 ADSP worker 里，且 `pumpSends` 不在持锁状态下做 MMIO。历史 route1 统计为 95 s 内 441 次 IRQ、`unknown=0`、`budget=0`。它更像是需要一次 `zizhan_ipcc_route=0` 对照来排除的低概率项，不是首要嫌疑。[事实 + 推断]

**最重要的证据重读**：11-17 的 7 轮不是同一个故障点，也不是一个干净的“偏移无关”对照。

| 轮次 | 预滚 | 额外变量 | 最后 CHECKPOINT | 推断挂点 | 结果 |
| --- | --- | --- | --- | --- | --- |
| amp-pulse-1116-v1 | 16 KiB | 无 | 无 klog | 未知 | 挂（92 s） |
| amp-pulse-1117-klog | 16 KiB | 无 | 无 checkpoint，最后 DATA_PIN | DATA_PIN 后、第一条 WRITE 前；可能是周期 klog flush | 挂 |
| amp-pulse-1117-ckpt | 16 KiB | 无 | stage 9（屏幕见 flushed + send） | **A：第一条 WRITE 已发、未见 DONE** | 挂 |
| amp-pulse-1117-pre0 | 0 | 无 | 全通过 | — | 跑通，-36 dBFS，无声 |
| amp-pulse-1117-loud | 0 | `tone_x=8` | 全通过 | — | 跑通，-18 dBFS，无声 |
| amp-pulse-1117-rep | 0 | `tone_repeat=10` | stage 10（屏幕见 flushed） | **B：stage 10 后在 `pulse_run`/第二条 WRITE 前后** | 挂 |
| amp-pulse-1117-hold | 0 | `pulse_hold=300` | stage 9（屏幕见 flushed） | **A：第一条 WRITE 已发、未见 DONE** | 挂 |

> 上表的“推断挂点”已按 2026-11-18 现场屏幕证据更正：`stage=N flushed=` 是 flush **完成之后**才打印的，且只会被**下一次** flush 写进 UFS klog；klog 尾部停在 `stage=N` 是正常现象。正确分组见 §9.1：A = 第一条 WRITE 已发出、未见 WRITE_DONE；B = stage 10 之后在 `zizhan_pulse_run`（第二条 WRITE 前后）。

因此：

- **16 KiB 预滚的 3/3 轮都挂（11-17 小样本；n18 a-1/a-3 同为 16 KiB 且 4 写通过，见 §9.6）**，且当时都停在第一条 zero WRITE 附近；
- **0 预滚、无额外 hold/repeat 的 2/2 轮跑通**（其中 `loud` 另加了 `tone_x=8` 增益，严格说不是完全相同的输入）；
- **0 预滚、加 hold/repeat 的 2/2 轮也挂**，其中 `rep` 在 stage 10 之后、`pulse_run`/第二条 WRITE 前后（B 组）；`hold` 的第一条 WRITE 已发出、未见 DONE（A 组）。

这 7 轮把“偏移”“amp 保持/重复”“amp pulse 路径”混在一起，不能据此宣布“与偏移无关”。当时最具体、最可检验的触发**假设**是：**16 KiB 预滚让 ADSP 的首写/首读越过 64 KiB MEM_MAP 边界**；最具体的放大**假设**是：**SMMU 故障之后没有任何 owner 清 FSR/GFSR、恢复或管理 TBU**。这两条可以同时成立：前者制造故障，后者把故障变成整机挂死。[11-17 推断；已被 n18 的 A/B 分组削弱]

**建议的下一个最小实验**：h/i/j 已回（2026-11-18 01:36–01:52）：i（2000 ms、无 flush）4 条 WRITE 全部 `done us≈11 ms`；h-1/h-2（100 ms）和 j（2000 ms + send flush）都在 92 s 看门狗复位前挂死，j 的 `send ... sequence=0` 已落盘、没有 `done`/`fail`。因此 100 ms 超时不是充分解释；当前最强假设是 **H13：首块 WRITE 路径本身在 DONE 前挂住 SoC**。先重跑 i 的 2000 ms/无 flush 条件数次，并在 send flush 之后加一行确认，区分“DSP 不回 DONE”与“flush 自身未返回”。安全收尾仍按 §11 C，但它不是 j 的已证触发。

**02:36 更新**：H13 也被 §12 取代。`n18c-p-1` 的完整 panic 证明根因是 PRM worker 在 16 KiB 栈上执行 `prepare/write_commands`，`IOLog` 走进 kqueue 唤醒时栈溢出；A/B 的“DONE 不回”只是日志链在 `send` 后溢出。

> **2026-11-18 现场修正**：协调者屏幕观察到 ckpt 轮 `stage9 ... us=14257`（flushed 行）之后还有 WRITE send；hold 轮 `stage9 us=20594` 后卡住；rep 轮 `stage10 us=15980` 后卡住。说明 flush 已返回、WRITE 已发出；`amp-pulse-1116-v1` 没有任何 checkpoint/klog flush 也挂死。正确分组是 **A = 第一条 WRITE（pre-roll sequence 0）已发、未见 WRITE_DONE**；**B = stage 10 后在 `pulse_run`（第二条 WRITE 前后）**。H8（checkpoint flush 不返回）撤回；flush 只保留为次要扰动。详见 §9 和 §11。

## 1. 证据等级与来源

本文把结论分成三类：

- **[事实]**：直接读到的 DT、源码、寄存器值或已保存的设备日志。
- **[推断]**：由事实组合出的机制，尚未被一次带诊断的设备轮直接证实。
- **[未知]**：没有一手证据，不能写成结论。

一手来源：

- 本机 live DT 提取物：`$ZIZHAN_SRC/work/hardware-20261115/audio-dma-live-dt-v1.dts`（仓库外，以下称“live DT”）。
- 公开的 SM8475 / taro-S 厂商 audio-kernel 镜像：`https://github.com/ASUS-SM8475/vendor_qcom_opensource_audio-kernel/blob/taro-S/dsp/msm_audio_ion.c`。它与 live DT 的 `msm-audio-ion` 数值一致（SID `0x1801`、sid-mask `0xf`），但不是小米原厂树的逐字副本，引用时按“SM8475 厂商分支”看待。
- 主线 Linux（torvalds 镜像）：`sound/soc/qcom/qdsp6/q6apm.c`、`q6apm-dai.c`、`audioreach.h`、`arch/arm64/boot/dts/qcom/sm8450.dtsi`、`drivers/iommu/arm/arm-smmu/arm-smmu.c`、`arm-smmu-qcom.c`、`arm-smmu.h`、`drivers/iommu/io-pgtable-arm.c`。
- 2026-08 LKML：`[PATCH v2 1/3] dt-bindings: sound: qcom,q6apm-dai: add stage-2 (SCM/VMID) access model`，`https://lkml.rescloud.iu.edu/2608.3/03890.html`。
- 本仓库代码：`drivers/PongPower/ZizhanAudioDmaMap.{h,cpp}`、`drivers/PongAdreno/ArmSmmu4K/*`、`drivers/PongPower/ZizhanAudioPrm.cpp`、`ZizhanAudioWrite.h`、`ZizhanAudioMemory.h`、`ZizhanAudioBatch.c`、`ZizhanIpcc*`、`PongAdsp.cpp`、`QcomGlink.c`。
- 既往证据：`docs/progress/STATUS.md`、`docs/progress/2026-11-15-audio-silent-owner.md`、`docs/progress/2026-10-31-frame-timeline.md`、`docs/progress/2026-11-15-ipcc-routing.md`、`$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`、`$ZIZHAN_SRC/work/hardware-20261115/amp-*.json`。

## 2. 原厂/Android/Linux 的音频共享内存路径

### 2.1 live DT 的实际值

`msm-audio-ion` 节点（live DT 29241–29255）：

```dts
msm_audio_ion: qcom,msm-audio-ion {
    dma-coherent;
    qcom,smmu-sid-mask = <0x00 0x0f>;
    qcom,iommu-dma-addr-pool = <0x10000000 0x10000000>;
    iommus = <&apps_smmu 0x1801 0x00>;
    qcom,smmu-enabled;
    qcom,smmu-version = <0x02>;
};
```

apps SMMU 节点（live DT 18960–18978）：

```dts
apps_smmu: apps-smmu@15000000 {
    qcom,skip-init;
    qcom,use-3-lvl-tables;
    qcom,context-fault-retry;       /* Waipio 专用故障重试 */
    qcom,num-context-banks-override = <0x4e>;  /* 78，硬件实际 75 */
    qcom,num-smr-override = <0x78>;            /* 120，硬件实际 113 */
    qcom,handoff-smrs = <0x2800 0x402>;
    dma-coherent;
    interrupts = <0x00 0x41 0x04 ...>;         /* 全局 fault SPI65 */
};
```

LPASS TBU（live DT 19034–19041）：

```dts
lpass_tbu: lpass_tbu@151e9000 {
    compatible = "qcom,qsmmuv500-tbu";
    reg = <0x151e9000 0x1000 0x151ce230 0x08>;
    qcom,stream-id-range = <0x1800 0x400>;      /* 0x1800–0x1bff */
    qcom,micro-idle;
};
```

fastrpc 的 SID（live DT 12500–12528）是 `0x1803`、`0x1804`、`0x1805`，并带 `"stall-disable", "HUPCF"`。它们是 fastrpc 自己的 context，不是音频 WRITE 的缓冲；不能因为都在 `0x1800` 段就认为它们也映射同一块音频内存。[事实]

### 2.2 `qcom,smmu-sid-mask` 与 `1 << 32` 到底是什么

厂商 `msm_audio_ion.c`：

- 第 36 行：`#define MSM_AUDIO_SMMU_SID_OFFSET 32`
- 第 895–913 行：读 `qcom,smmu-sid-mask`，解析 `iommus` 的 arg0，`sid = args[0] & sid_mask`，再 `smmu_sid_bits = sid << 32`
- 第 263–280 行：`msm_audio_ion_get_phys()` 在 `is_iova` 时把 `*addr |= smmu_sid_bits`

所以对 live DT 的 `<0x1801 0x0>` 和 `<0 0xf>`：

```
sid = 0x1801 & 0x0f = 1
sid_bits = 1 << 32
DSP address = iova | 0x1_0000_0000
```

`1 << 32` **不是** “SID 0x1801 写在地址高位”，而是 **SID 的低 4 位（stream index 1）**。`qcom,smmu-sid-mask` 掩的是 SID，不是地址；掩完后再左移 32 位。低 32 位是 SMMU 的 IOVA；高位 tag 由 ADSP 固件/音频 DMA 硬件用来选择 LPASS 的 stream。按这个模型，送进 apps SMMU 的应是低 32 位 IOVA；DSP 固件源码不可得，不能直接验证这一点，但厂商构造和 32 位 IOVA 池支持这个解释。[事实 + 推断]

主线 `q6apm-dai.c` 完全同构：

- 第 42 行：`#define SID_MASK_DEFAULT 0xF`
- 第 548–551、670–673 行：`prtd->phys = substream->dma_buffer.addr | (pdata->sid << 32)`
- 第 1203–1207 行：`pdata->sid = args.args[0] & SID_MASK_DEFAULT`

主线 SM8450 的 `q6apmdai` 节点（`sm8450.dtsi` 2802–2812）就是：

```dts
q6apmdai: dais {
    compatible = "qcom,q6apm-dais";
    iommus = <&apps_smmu 0x1801 0x0>;
};
```

**SMR mask 是第三个 `iommus` cell，即 `0x0`，不是 `qcom,smmu-sid-mask` 的 `0xf`。** 我们的 SMR 用 `0x80001801`（valid + SID `0x1801` + mask 0），与 Linux 的 `iommus = <&apps_smmu 0x1801 0x0>` 一致。**不要**把 SMR mask 改成 `0xf`：那会让 `0x1800–0x180f` 都匹配到同一个 context，反而扩大误匹配面。[事实]

### 2.3 q6apm/audioreach/gpr 的 MEM_MAP 与 WRITE

主线 `q6apm.c`：

- `__q6apm_map_memory_fixed_region()`（210–250 行）：`APM_CMD_SHARED_MEM_MAP_REGIONS`，pool `3`（`APM_MEMORY_MAP_SHMEM8_4K_POOL`），1 个 region，`shm_addr_lsw/msw`，size 按 4 KiB 对齐。
- `q6apm_write_async()`（480–500 行）：`DATA_CMD_WR_SH_MEM_EP_DATA_BUFFER_V2`，`buf_addr_lsw/msw`、`mem_map_handle`、`buf_size`、时间戳、flags、metadata 地址；GPR token 为 `graph->rx_data.dsp_buf | (len << APM_WRITE_TOKEN_LEN_SHIFT)`。
- `q6apm.h` 45–47 行：`APM_WRITE_TOKEN_MASK = GENMASK(15,0)`、`APM_WRITE_TOKEN_LEN_MASK = GENMASK(31,16)`、`APM_WRITE_TOKEN_LEN_SHIFT = 16`。
- `q6apm.c` 576–610 行：收到 `DATA_CMD_RSP_WR_SH_MEM_EP_DATA_BUFFER_DONE_V2` 时用 `hdr->token & APM_WRITE_TOKEN_MASK` 取 buffer index，推进 `hw_ptr`。

也就是说，主线 WRITE token 的约定是：

```
token = (bytes << 16) | buffer_index     /* buffer_index 从 0 开始 */
```

我们的 `ZizhanAudioWrite.h:27` 用的是：

```
token = 0x7a1b0001 + sequence
```

对 mode 34 的第一个 zero WRITE，`sequence=0`，token 低 16 位是 1，高 16 位是 `0x7a1b`（31259）。第二个 zero/tone 写是 2/3/4。`ZizhanAudioMemory.h:20` 的 `MEM_MAP` token 是 `0x7a1a0001`；主线 `MEM_MAP` token 的低 16 位是 `graph_id`（`q6apm.c` 220 行 `graph_id | pos_mask`）。这两处都是**与主线不一致的协议偏差**。[事实]

它是否直接导致挂死？不能单独证明。DSP 通常会把 token 原样回显；我们的 `receipt` 也检查完整 token，所以主机的配对没错。但 `DONE` 的 token 低 16 位在 Linux 驱动里被用来索引 `graph->rx_data.buf[]` 和推进 `hw_ptr`。如果 DSP 固件也用低 16 位做 buffer index，我们的 1 起编号会让第一个写对应到 index 1，第四个写对应到 index 4；若固件的 ring 只有 4 项，index 4 越界。高 16 位的 31259 若被固件当作长度，也会大于 `buf_size=11520`。[推断]

### 2.4 是否需要 SCM/hyp assign

厂商 `msm_audio_ion.c` 确实有：

- `IOCTL_MAP_HYP_ASSIGN` / `IOCTL_UNMAP_HYP_ASSIGN`（680–750 行）
- `hyp_assign_phys(paddr, pa_len, source_vm, dest_vm, perms)`，目标 VMID 包含 `VMID_LPASS`、`VMID_ADSP_HEAP`、`VMID_HLOS`
- `msm_audio_ion_import()` 本身**不会自动**做 hyp assign；是否调用由用户态 ioctl 决定

但主线 `q6apm-dai.c` 的 SCM 路径只对 **modem/mDSP** 生效：

- 1209–1226 行：只有 `apm->gdev->domain_id == GPR_DOMAIN_ID_MODEM` 才 `use_scm_assign = true`，并且 `iommus` 与 mDSP SCM 路径互斥。
- 2026-08 LKML stage-2 提案进一步说明：现有目标（ADSP）是 **stage-1 context，buffer 通过 `iommus` 描述**；只有像 Shikra 这种“audio 由 modem DSP 提供、DSP 在 stage-2 保护上下文且不能用 SMMU”的平台，才用 `qcom,vmids` + SCM 分配。该提案不是本机 DT 的既有配置，引用它只是为了区分两种访问模型。

本机 live DT 的 `msm-audio-ion` 是 `iommus` 模型，apps SMMU 的 `ID1.NUMS2CB = 0`（没有 stage-2 context bank），所以**没有一手证据要求给这块 UEFI 缓冲做 SCM/hyp assign**。[事实 + 推断]

保留一个限定：如果这块 EfiReservedMemory 被某个 XPU/保护域挡住，DSP 访问仍可能失败；但那会表现为另一类故障（见 §3.4），不能靠“先加 hyp_assign”来猜。没有 Gunyah/XPU 的只读状态证据之前，不应改这条路径。

## 3. 我们的 SMMU context 与 Linux `arm-smmu-qcom` 的差异

### 3.1 实际寄存器对照

我们 `ZizhanAudioDmaMap.cpp:138–149` 的配置：

```cpp
o.lower = {32, 36, table_pa, 16};          // input 32-bit, output 36-bit, 16 leaves
o.range = {base, base, 65536, Read|Write, 0, 2};  // identity, NC, outer shareable
o.config.asid = 0x1801;
o.config.coherent_walk = 1;
o.config.polled_faults = 1;
o.config.ignore_top_byte = 1;
o.config.shared_asid = 1;
```

`ArmSmmu4KContext.c:50–70` 编码出的实际值：

| 寄存器 | 我们的值 | 说明 |
| --- | --- | --- |
| `CBAR` | `0x1f300` | type=1（S1 trans, S2 bypass），S1_MEMATTR=WB，S1_BPSHCFG=NSH |
| `CBA2R` | `0x1` | AArch64 VA64，无 stage-2 VMID |
| `TCR` | `0x80000000 \| 0x3500 \| 32` | TG1=4K，SH0=inner，ORGN0/IRGN0=WBWA，T0SZ=32 |
| `TCR2` | `0x38071` | PASIZE=36，SEP=7，AS=1（16-bit ASID），TBI0/TBI1=1 |
| `TTBR0` | `0x1801 << 48 \| root_pa` | ASID `0x1801`，root 在 36-bit PA |
| `TTBR1` | `0x1801 << 48` | EPD1=1，禁用 |
| `MAIR0` | `0xff44` | index0=NC，index1=WBWA |
| `SCTLR` | `0x27` | M, TRE, AFE, CFRE；**CFCFG=0, CFIE=0, HUPCF=0, ASIDPNE=0** |
| `SMR` | `0x80001801` | EXIDS=0，valid + SID `0x1801` + mask 0 |
| `S2CR` | `73` | type=TRANS，CBNDX=73 |
| `CB_ACTLR` | **未读、未改** | 只写了 CBAR/CBA2R/TCR/TCR2/TTBR/MAIR/FSR/SCTLR |

Live DT / 既往设备读数：

- `ID0 = 0x4c017e71`：113 个 SMR group，EXIDS=0，S1TS=1。
- `ID1 = 0x6000004b`：75 个 CB，128 pages，4 KiB granule，`NUMS2CB=0`。
- `ID2 = 0x5111`：VA 36-bit，PA 36-bit，4 KiB。
- 计划使用 slot 112，bank 73；slot 112 原 `SMR=0`、`S2CR=0x20000`（FAULT），bank 73 原 `SCTLR=0x2000230a`、`M=0`。

这些值见 `$ZIZHAN_SRC/work/hardware-20261115/amp-dmaobs-v1-device-review.json` 和 `docs/progress/2026-11-15-audio-silent-owner.md` 的 mode25/mode26 记录。[事实]

### 3.2 Linux 的默认值

`arm-smmu.c:1148–1155`：

```c
cfg->sctlr.cfre = !(test_bit(DOMAIN_ATTR_FAULT_MODEL_NO_CFRE, attributes));
cfg->sctlr.cfcfg = !(test_bit(DOMAIN_ATTR_FAULT_MODEL_NO_STALL, attributes));
cfg->sctlr.hupcf = test_bit(DOMAIN_ATTR_FAULT_MODEL_HUPCF, attributes);
```

对没有 `qcom,iommu-faults` 的 `msm-audio-ion`，按**主线 arm-smmu 的默认**是：

- `CFCFG=1`（stall on fault）
- `CFIE=1`（context fault IRQ）
- `CFRE=1`
- `HUPCF=0`
- `ASIDPNE=1`（`arm-smmu.c:591` 的 `FIELD_PREP(S1_ASIDPNE, stage1)`）
- `M=1`

而 live DT 的 fastrpc 节点明确写了 `"stall-disable", "HUPCF"`（live DT 12508/12517/12526），说明**音频 `msm-audio-ion` 没有走 fastrpc 那套 terminate 模型**；按主线模型它是默认的 stall + IRQ。[事实]

这里有一个来源限制：`msm_audio_ion.c` 本身只负责 dma-buf 映射，SMMU domain 的 fault 属性由厂商 SMMU 驱动设置；live DT 没有给 `msm-audio-ion` 写 `qcom,iommu-faults`，所以“默认 stall + IRQ”是主线模型的推断，不是本次直接读到的厂商运行时值。要 100% 确认，需要在 Android 侧读该 domain 的 SCTLR/属性，或在本机读我们自己的 context（我们已读，是 `0x27`）。[事实 + 未知]

Waipio 的 `qcom,context-fault-retry`（`arm-smmu.c:910–945`）检查 `NO_STALL`，并且只有 context fault IRQ 真的进入时才可能执行：

1. 读 `FAR`、`FSR`
2. 如果地址变化或计数器为 0：`arm_smmu_tlb_inv_context_s1()`
3. 写 `FSR` 清故障
4. `wmb()`
5. 写 `CB_RESUME = RESUME`

普通 fault handler（`arm-smmu.c:979–1015`）在 `FSR.SS` 时写 `CB_RESUME = TERMINATE`，然后清 FSR。[事实]

**我们的 context 是 terminate（CFCFG=0）且 CFIE=0，所以既不走 retry，也不走 IRQ handler；除了 `ProgramDisabledContext` 在 enable 前写 `FSR=all ones` 之外，运行期没有任何代码清 FSR 或写 RESUME。** 这不是“stall 不 resume”，而是“terminate 之后无人收尾”。两者都可能让 DSP 的 DMA 事务停在异常状态，但故障模型不同，不能混写。retry 路径与 stall 的语义配套；在 terminate 下，事务已经被终止，写 `RESUME` 不会把已终止的事务重新发起。[事实 + 推断]

### 3.3 其他差异

| 项目 | Linux | 我们 | 影响判断 |
| --- | --- | --- | --- |
| SCTLR 故障模型 | stall + CFIE + CFRE | terminate + CFIE off + CFRE | 关键差异；见 §3.4 |
| FSR 清除 / RESUME | fault handler 清 FSR、按 SS 写 RESUME/TERMINATE | 不写 | 关键差异 |
| `qcom,context-fault-retry` | Waipio 上 invalidate + clear + RESUME | 不使用 | 关键差异 |
| 全局 `sCR0/sGFSR` | reset/fault handler 读写；USFCFG 可配置 | 完全不读、不写 | 关键差异 |
| `CB_ACTLR` | 有 DT `qcom,actlr` 时写，否则默认 0 | 不读、不写，可能继承旧值 | 需先只读确认 |
| LPASS TBU | `arm-smmu-qcom.c:760–850` 的 ATOS/ECATS 路径有 halt/resume/ECATS 观察 | 不碰 | 需先只读确认 |
| ASIDPNE | 1（私有 ASID） | 0（共享 ASID，`0x1801`） | 硬件读回强制；有碰撞检查，但不是 Linux 原样 |
| ASID 值 | `cfg->asid = cfg->cbndx`（0..255） | `0x1801`（16-bit，TCR2.AS=1） | 若硬件只实现 8-bit，会截断；需用 TTBR 读回确认 |
| TBI | 默认 0 | 1（`TCR2=0x38071`） | 只影响 bit63:56；32-bit IOVA 下不是首要嫌疑 |
| SCTLR 缓存位 | `dma-coherent` 时设 WACFG/RACFG/SHCFG/MTCFG/MEMATTR | 0 | `arm-smmu.c` 注释说只在 bypass（M=0）生效；M=1 下不是首要嫌疑 |
| 叶子属性 | 可能 WB（IOMMU_CACHE） | NC（AttrIndx 0） | NC 更保守，不会造成缓存不一致挂死；只是慢 |
| SMR/S2CR 发布顺序 | S2CR 再 SMR（`write_sme`） | S2CR(bank) 再 SMR | 一致 |
| TLB | map/unmap 时 invalidate；fault retry 时 invalidate | enable 前 TLBIASID；retire 时 TLBIASID | 基本一致；缺 fault retry 的 invalidate |

### 3.4 哪一项可能让一次 ADSP DMA 故障把整个 SoC 挂住

先说 **`fault=400` 不是故障证据**：`amp-dmalease-v2` 等成功安装日志里的 `fault=400 fault_mask=c00001fe masked_fault=0`，以及 g151 显示 CB3 `SCTLR=0xe0`、`M=0`、`FSR=0x400`，说明 `0x400` 是这块 SMMU 在上电/禁用状态的默认 FSR 值；`FSR_FAULT` 掩码 `0xc00001fe` 不包含 bit10。不要把 `0x400` 当成“已经发生过 fault”。[事实]

真正可能把一次故障放大成整机挂死的机制，按可解释性排序：

1. **DSP 的 DMA 事务在 SMMU 处失败后，DSP 固件不处理该 abort，保持 AXI 事务/内部队列不释放。** 失败可能是越界读、地址宽度、SID 不匹配或页表问题。DSP 的 watchdog pinger 停止，Gunyah 约 60 s 后 cold bite；AP 侧的 GLINK/IPCC 也可能因 LPASS NoC 背压而写不出去。这个机制不要求我们“stall 不 resume”，terminate 的 abort 同样可以触发。[推断]
2. **全局 fault 没有 owner。** `sCR0=0x200406`（g149 观察）含 `GFIE=1`、`USFCFG=1`；如果 DSP 使用了未匹配的 SID（例如 tag/stream 不是 1，或某个预取用了 `0x1800/0x1802`），`sGFSR.USF` 会置位，SPI65（live DT 的 `0x41`）没有 XNU handler。全局 fault 是 level IRQ，可能反复进入/保持；同时 SMMU 不再正常服务该 stream。[推断，需读 GFSR/GFSYNR1 证实]
3. **LPASS TBU 被 fault/halt 状态扣住。** `lpass_tbu` 覆盖 `0x1800–0x1bff`，带 `qcom,micro-idle`；Linux 的 ATOS/ECATS 路径（`qsmmuv500_iova_to_phys`）会 halt/resume TBU，并在 ECATS 时采集。这不是普通 context fault handler，但说明 TBU 是需要显式协调的共享资源。我们的驱动不碰 TBU。如果 SMMU 的 fault 让 TBU 进入 halt/backpressure，NoC 可能把 AP 的 IPCC/GLINK 访问也堵住。[推断]
4. **`CB_ACTLR` 继承旧值。** bank 73 原来 `SCTLR=0x2000230a`（M=0），说明它被固件用过；我们只覆盖 CBAR/CBA2R/TCR/TCR2/TTBR/MAIR/FSR/SCTLR，没有读/清 `CB_ACTLR`（0x4）。旧 ACTLR 可能打开 context cache/prefetch 或改变 walk 行为。没有读到实际值前，不能排除。[未知]
5. **ASID/ASIDPNE 的共享命名空间。** 硬件把 `ASIDPNE` 强制为 0（请求 `0x1027` 读回 `0x27`），我们用共享 ASID `0x1801`。如果 16-bit ASID 实际被截成 8-bit，`0x1801` 的有效值可能是 `0x01`，与未来启用的其他 bank 冲突；当前 `active_banks=0`，映射时没有冲突，但后续 iOS 驱动启用 context 后未必。[未知]

**结论**：如果下一次实验能读到 context `FSR/FAR` 或全局 `GFSR/GFSYNR1`，就能把“DSP 固件自身”与“SMMU 故障放大”分开；在读不到之前，不能把 §3.4 的任何一条写成已证根因。

### 3.5 Gunyah 相关观察

- live DT 的 apps SMMU 带 `qcom,skip-init`（18965），说明全局初始化由固件/Gunyah 完成，Linux/我们不应重置它。我们的 `ZizhanAudioDmaMap` 确实不做全局 reset，只写自有 CB 的 CBAR/CBA2R/TCR/TCR2/TTBR/MAIR/FSR/SCTLR 和自有 SMR/S2CR。这是正确的方向。[事实]
- 既往 mode26 记录（`docs/progress/2026-11-15-audio-silent-owner.md`）显示：图关闭后 `dma_map_end()` 在 `write32` 写 `CBAR73=0` 时触发 implementation-specific error；后续改成“disable + TLB sync 后把 context 以 native idle 形态 retire，不写回 CBAR0”。这说明 **Gunyah/secure monitor 可能拦截部分全局 SMMU 写**，不能把所有 SMMU 寄存器写当作普通 MMIO。[事实]
- 这不等于“DSP 故障一定被 Gunyah 处理”；相反，我们的驱动没有和 Gunyah 的全局 fault/TBU/SCR0 owner 协调。`sCR0=0x200406` 的 `GFIE=1` 和 SPI65 没有 XNU handler，是可以观察到的未协调点。[事实 + 推断]
- `NUMS2CB=0`、`msm-audio-ion` 带 `iommus`，以及 2026-08 LKML 对 ADSP stage-1 / mDSP stage-2 的区分，都指向：本路径不应需要 Gunyah stage-2 内存分配。若未来在别的 SID/平台上看到 stage-2，才需要重新评估 `qcom,vmids`/SCM。[事实 + 推断]

## 4. 设备轮证据重读

### 4.1 CHECKPOINT 的位置

`$ZIZHAN_SRC/work/m1-firstsound/add-checkpoints.py` 在 `ZizhanCs35l41.c` 的 pulse 路径插入：

- stage 8：sample 49 进入 transfer 之前
- stage 9：`transfer->once(chip+2)` 之前
- stage 10：`transfer->once()` 返回并做一次 `run_state` 之后、`zizhan_pulse_run()` 之前
- stage 11/12：`zizhan_pulse_run()` 返回成功/失败

对应源码 `drivers/PongPower/ZizhanCs35l41.c:1194–1211`。因此：

- **stage 9 挂** = 第一条 WRITE 还没有返回（或返回前 AP 已经不能继续）。
- **stage 10 挂** = 第一条 WRITE 已经返回，且 `run_state` 已经返回（stage 10 日志在这之后）。stage 10 行之后还有一次 checkpoint flush；**现场 rep 看到 `stage10 us=15980`（flushed）之后才卡住**，所以挂点在随后的 `zizhan_pulse_run()`（第二条 WRITE 前后），不是 flush。klog 尾部没有 flushed 行是正常的：它只在下一次 flush 落盘。

`rep` 轮停在 stage 10，是“第一条 WRITE 已 DONE、stage 10 后在 `pulse_run`/第二条 WRITE 前后”；`hold`/`ckpt` 停在 stage 9，是“第一条 WRITE 已发出、DONE 未回”。这两类不应该合并成同一个“ADSP 处理 WRITE 挂死”。[事实]

> **2026-11-18 现场修正**：`rep` 的 `stage10 flushed=...` 在屏幕上出现后才卡住，不是 flush；`ckpt` 的 `stage9 flushed=...` 之后还有 WRITE send。klog 尾部缺少 flushed 行是正常现象；逐条以 §9.1 的现场表为准。

### 4.2 16 KiB 与 0 的混淆

mode 34 的 `zero_transfer`（`ZizhanAudioPrm.cpp:360–371`）顺序是 `{2,0,3,1}`：

- chip 2/3：`silence=true`，地址 = `base + preroll`，数据是 zero；
- chip 0/1：`silence=false`，地址 = `base`，数据是 tone。

所以 mode 34 的 zero 预滚**不是** 4 个连续 slot；它只是把同一个 `base+preroll` 地址写两次（chip 2 和 chip 3），tone 写 `base` 两次。`ZizhanAudioBatch` 的 `stride=0x3000`、4 个 slot 是 mode 36/37 的 batch 路径（`bytes=2880`），不是 11-17 mode 34 的预滚。[事实]

在 `preroll=16384`、`bytes=11520` 时，第一条 zero WRITE 覆盖 `[base+16384, base+27904)`；它本身没有到 64 KiB 末尾。**但如果 ADSP 在第一次 WRITE 时按 `MEM_MAP` 的整个 64 KiB region 做初始化/预取，而不是只读 `buf_size`，那么 `base+16384 + 65536 = base+81920`，越过 `base+65536` 的映射末尾 16 KiB。** 0 预滚时，`base+0 + 65536 = base+65536`，恰好落在映射末尾，不越界。这个几何关系与“16 KiB 3/3 挂、0 预滚无额外变量 2/2 过”吻合。[推断，需单条 16 KiB WRITE 验证]

反之，0 预滚 + hold/repeat 的 2/2 挂不能用这个几何解释：第一条 zero WRITE 在 `base+0`，整区预取也恰好不越界。`rep` 的 stage 10 在第一条 WRITE DONE 之后，现场 `stage10 us=15980`（flushed）后卡住，实际挂点是 **`pulse_run`/第二条 WRITE 前后**（B 组）；`hold`/`ckpt` 的 `stage9 flushed=...` 后还有第一条 WRITE send，挂点是首块 DONE 未回（A 组）。它们不是“amp/TDM/pulse 已经从第一条 WRITE 前就运行”的证据；B 组重新把 pulse/amp 纳入候选，A 组则不是 pulse。[事实 + 未知]

### 4.3 不能从日志推断“send 没发生”

`write_commands()` 的 `ZIZHAN_AUDIO_WRITE: send ...` 日志在 `sheng_adsp_send()` **之前**打印（`ZizhanAudioPrm.cpp:354–355`）。`ckpt`/`klog` 包有 `pong_klog_period=5`；其他包主要靠 checkpoint flush。如果 WRITE 后 <5 s 内整机挂死，且没有下一个 checkpoint，send 行可能只留在内存 console，没进 UFS klog。`amp-pulse-1117-klog` 最后停在 DATA_PIN、没有 send 行，不能据此断言 `sheng_adsp_send()` 没有执行；只能说“最后一条被持久化的行是 DATA_PIN”。[事实 + 推断]

## 5. IPCC 路由评估

### 5.1 代码路径

- `ZizhanIpcc.cpp:47–56`：IRQ 里调用 `zizhan_ipcc::drain()`，每次最多 32 条；统计 `received/delivered/unknown/budget`。
- `ZizhanIpccRouter.h:41–56`：每条先 `clear(id)`，再 `dispatch`；预算用完只置 `budget_used`，不阻塞、不重试。
- `PongAdsp.cpp:137–145`：`routedIpcc()` 只把 doorbell 映射成 `kEvSmp2p/kEvGlink/kEvAop`，做原子计数并 `wake(bits)`。
- `ZizhanAdspWait.h:10–18`：`take -> arm -> take -> block -> take`，doorbell 不会在 check 和 sleep 之间丢失。
- `PongAdsp.cpp:705–717`：`sheng_adsp_send()` 只在 `gClientLock` 下把消息拷进 16 槽队列并 `wake(kEvSend)`，不在锁内做 MMIO。
- `QcomGlink.c:421–455`：`qcom_glink_send()` 找不到 remote intent 时返回 `QcomGlinkAgain`，worker 下一轮重试；没有等待/自旋。
- `PongAdsp.cpp:432–443`：`pumpSends()` 在 `qcom_glink_send()` 之前释放 `gClientLock`，不会持锁做 GLINK/IPCC 写。

### 5.2 历史统计

`docs/progress/2026-11-15-ipcc-routing.md` 的 route1 设备轮：8 条快照覆盖 24.652–95.108 s，首条 IRQ 10、末条 IRQ 441、received/delivered 477/477，`unknown=0`、`budget=0`、`retries=0`，subscriptions=3。约 441/95.1 ≈ 4.6 条 IRQ/s，不是中断风暴。[事实]

### 5.3 判断

- IRQ 里没有 `IOLock`、没有分配、没有等待；回调只做原子和 `thread_wakeup`。
- `drain` 的 32 条预算用完后，level IRQ 会再次进入；不会丢消息，也不会死锁。
- 如果真有风暴，统计会显示 `budget_used`/`unknown` 增长或 IRQ 数异常；当前没有这种证据。
- 唯一值得做的干净对照是同一 silent WRITE 下 `zizhan_ipcc_route=0`（走旧 `SPI 32+kSpiIpcc` 路径）。如果挂死仍复现，IPCC 路由被排除；如果只在 route1 复现，再深入查 GIC trigger/共享 receiver。**这是一次对照，不是主线。**

补充一个方法上的限制：11-17 的 7 轮 package 都带 `zizhan_ipcc_route=1`（`package-ckpt.py`、`package-pre0.py`、`package-loud.py`、`package-rep.py`、`package-hold.py`），所以这 7 轮内部没有 route0 对照，不能把 IPCC 路由与其它变量分开。要排除它，必须单独跑一次 route0，而不是从已有 5/7 推断。

**结论：低概率。** 代码审查没有支持“高频 GLINK 收包触发中断风暴/死锁”的机制；但不能用“代码看起来安全”替代一次 route0 对照。[推断]

## 6. 假设清单（按可检验性与证据强度排序）

### H1：16 KiB 预滚让 ADSP 首写/首读越过 64 KiB MEM_MAP 边界

**证据**

- 16 KiB 的 3/3 轮都挂，且都在第一条 zero WRITE 附近（amp-pulse-1116-v1、klog、ckpt）。
- 0 预滚、无 hold/repeat 的 2/2 轮跑通（`loud` 另有 `tone_x=8` 增益；样本仍很小）。
- mode 34 第一条 zero WRITE 地址是 `base+16384`；如果 ADSP 首次按整个 `MEM_MAP` region（64 KiB）预取/初始化，读取范围到 `base+81920`，越过 `base+65536` 的映射末尾 16 KiB；0 预滚时 `base+0+65536` 恰好不越界。
- `ZizhanAudioPrm.cpp:341–342` 的注释本身就写着“the first ADSP read at 16 KiB hangs the SoC”，说明早期观察也指向这里。

**最小静音实验**

- 用一个新的 default-off 诊断 boot arg（例如 `zizhan_pulse_once=1`）只发 **一条** zero WRITE：
  - A 组：`zizhan_pre_off=0`；
  - B 组：`zizhan_pre_off=16384`。
- `zizhan_pulse_dry=1`（不 enable/不调音量）、`pong_audio=0`、`zizhan_dma_watch=1`、`pong_autoreset=210`；映射保持 2 s 再 restore。
- 两组除 `pre_off` 外完全相同；只变一个变量。

**代码改动**

- 在 `ZizhanAudioPrm`/`ZizhanSe15Prepare` 增加“只做第一条 zero WRITE 并保持 map”的诊断路径；不改变生产路径。
- 或在 mode 28 的 `write_commands()` 前增加一个 default-off 的 `zizhan_zero_off`，并加 `zizhan_dma_hold_ms`。
- 不需要改 SMMU 编程本身。

**判定**

- A 过、B 挂：H1 成立（首写整区预取/越界）。下一步把预滚改回 0，或把 UEFI extent 扩到 128 KiB 再试 16 KiB。
- A/B 都挂：不是单纯 offset；转 H2/H3/H4。
- A/B 都过：单条 WRITE 不够，需要 4 次 amp/transfer 时序；转 H4。

**风险/返回**：不开发功放、不写分区；autoreset + 电源键；rear USB-A。若 B 挂，保留第一次失败，不重复同一 B 轮，除非改变诊断。

### H2：SMMU 故障无人收尾（terminate/no IRQ/global/TBU/ACTLR）

**证据**

- 我们的 `SCTLR=0x27`：`CFCFG=0`、`CFIE=0`、`HUPCF=0`、`ASIDPNE=0`（`ArmSmmu4KContext.c:65–67`）。
- `qcom,context-fault-retry` 在 live DT 18967；Linux 的 retry 与 stall 语义配套，我们的 terminate 不走它；更关键的是 `CFIE=0` 让 fault handler 根本不运行。
- `dma_watch` 只读 `FSR/FAR/FSYNR0/GFSR`，不读 `GFSYNR0/1/2`、`sCR0`、`CB_ACTLR`、TBU；且 `dma_map_end()` 在 restore 前就 `dma_watch_stop()`（`ZizhanAudioDmaMap.cpp:167–170`）。
- `sCR0=0x200406`（g149）含 `GFIE=1`、`USFCFG=1`；全局 fault SPI65 没有 XNU owner。
- `lpass_tbu` 在同一个 apps SMMU 下，带 `qcom,micro-idle`；我们的驱动不碰 TBU。

**最小静音实验**

- 沿用 H1 的单条 zero WRITE，打开 `zizhan_dma_watch=1`；在 watcher 里增加**只读**：
  - `sCR0`（0x0）
  - `sGFSR`（0x48）
  - `sGFSYNR0/1/2`（0x4c/0x50/0x54）
  - `CB_ACTLR`（cb+0x4）
  - `lpass_tbu` base+0x0 / base+0x20（按 `arm-smmu-qcom.c:349–370`）
- 记录 context `FSR`、`FAR`、`FSYNR0/1`；fault 出现时同步 flush（现有 `pong_klog_flush(7)`）。
- 不在第一次实验里清 FSR/写 RESUME；先拿到签名。

**代码改动**

- 只读扩展 `dma_watch`，不改变 SMMU 写序列。
- 若看到 context fault：下一步实现“polled fault service”（`TLBIASID` + 清 FSR + `CB_RESUME=TERMINATE` 或 `RESUME`）；若看到 global unmatched fault：先查 `GFSYNR1` 的 SID，再决定是否补 SMR/修正 tag。
- 不建议在未看到 fault 前把 `CFCFG` 改 1；那会把 terminate 变成 stall，如果没有可靠 handler，反而更容易挂。

**判定**

- `FSR` 有 `TF/PF/EF/ASF` 且 `FAR` 在 `base+65536` 附近：H1 的越界 + H2 的 fault 放大成立。
- `FSR` 有 `ASF` 且 `FAR≈0x1_f9e3b000`：tag 没被 DSP 剥离/输入宽度不匹配。
- `GFSR.USF=1` 且 `GFSYNR1` 不是 `0x1801`：SID/tag 选择错误，不是 page table 问题。
- 完全没有 fault：SMMU 不是本次挂死的直接触发；转 H3/H4。

**风险/返回**：只读 MMIO；不写 SMMU；不碰手机；autoreset。

### H3：WRITE/MEM_MAP token 不符合 Linux 编码

**证据**

- 主线 token = `(bytes << 16) | buffer_index`，buffer_index 从 0 开始（`q6apm.c:485`、`q6apm.h:45–47`）。
- 我们 token = `0x7a1b0001 + sequence`（`ZizhanAudioWrite.h:27`）；第一个写低 16 位是 1，高 16 位是 `0x7a1b`（31259），不是 `11520`。
- `MEM_MAP` token = `0x7a1a0001`（`ZizhanAudioMemory.h:20`）；主线的低 16 位是 graph id。
- `ZizhanAudioBatch.c:38–40` 的 `sequence=slot`（0..3），经 `zizhan_write_next` 后低 16 位变成 1..4；如果 DSP 的 ring 只有 4 项，第 4 个写是 index 4。

**最小静音实验**

- 只改 token 编码，不改包体、不改地址、不改 SMMU：
  - WRITE：`token = (bytes << 16) | index`，index 从 0 开始；
  - MEM_MAP：如果 graph id 已知，低 16 位用 graph id；未知则先保留并记录，不猜。
- 用 H1 的单条 zero WRITE（0 预滚）和 4 条 zero WRITE 各一轮，`pulse_dry=1`。
- 与 H1 的 A 组做对照：如果 H1 A 组本来就过，H3 需要看“修 token 后 4 条/后续写是否更稳”，而不是只看单条。

**代码改动**

- `ZizhanAudioWrite.h`：`token` 改为 `((bytes & 0xffff) << 16) | (index & 0xffff)`；`ZizhanWriteState` 增加 `index` 或在调用处传入。
- `ZizhanAudioMemory.h`：`MEM_MAP` token 用 graph id；若当前代码没有 graph id，先加只读日志确认实际 graph，不要凭空写。
- 不改 `AR_OP_WRITE` 的 44 字节 body、不改 `buf_size`、不改地址。

**判定**

- 修 token 后挂死率显著下降：H3 成立/部分成立；保留 token 修复，继续查 H1/H2。
- 修 token 后无变化：token 不是触发；仍建议保留为协议一致性修复，但不要把它当根因。

**风险/返回**：只改 GPR token；不影响 SMMU/amp；autoreset。

### H4：amp/TDM/pulse 路径（stage 10+）

**证据**

- `rep` 轮停在 stage 10：第一条 WRITE 已返回，现场 `stage10 us=15980`（flushed）后卡住；挂点在 **`zizhan_pulse_run()`/第二条 WRITE 前后**（`ZizhanCs35l41.c:1207–1211`）。H4 因此重新成为 B 组候选。
- `hold` 轮带 `pulse_hold=300`，停在 stage 9；`zizhan_amp_pulse_hold_us` 在 boot arg 解析时已设置，但 `pulse_ops` 在 stage 10 之后才构造，是否影响 stage 9 的 amp 预脉冲状态未知。
- 2 轮成功都是 0 预滚、无 hold/repeat；2 轮 0 预滚挂死都带 hold 或 repeat。样本小，但 amp 变量与挂死相关。
- 两轮成功“无声”说明 amp/TDM 的数据路径本身也还没被听觉验证；V2DONE 早于真实播放是既往观察。

**最小静音实验**

- 同一 4 次 transfer 路径，只变 amp 变量：
  - A：`zizhan_pulse_dry=1`（不 enable/不调音量），`pulse_hold=0`，`tone_repeat=0`；
  - B：`zizhan_pulse_dry=1` + `zizhan_pulse_nopup=1`（跳过 native pup/pdn patch）；
  - C：`zizhan_pulse_dry=1` + `pulse_hold=300`（仍不开功放输出，只测 hold 变量）。
- 每组只变一个变量；都开 `zizhan_dma_watch=1`，确认是否伴随 SMMU fault。

**代码改动**

- 现有 `zizhan_pulse_dry`、`zizhan_pulse_nopup`、`zizhan_pulse_hold` 已够做 A/B/C；不需要新驱动改动。
- 若要分离“amp I2C 卡住”和“DSP WRITE”，可在 stage 9/10 之间加一条只读的 amp 状态日志（不写寄存器）。

**判定**

- dry 过、hold/repeat 挂：amp/TDM/pulse 路径；优先查 CS35L41 native pulse、TDM slot/clock、pup/pdn patch。
- dry 也挂：不是 amp 输出；回到 H1/H2/H3。
- 伴随 SMMU fault：先按 H1/H2 处理，再回到 amp。

**风险/返回**：dry 不开发功放；hold/repeat 轮仍可能挂死；autoreset + 电源键；不写分区。

### H5：IOVA 范围（identity 0xf9e3b000 vs 厂商 pool 0x10000000–0x20000000）

**证据**

- 厂商 `msm_audio_ion` 显式设置 `qcom,iommu-dma-addr-pool = <0x10000000 0x10000000>`（live DT 29245）；我们的 IOVA 是 UEFI 物理地址 `0xf9e3b000` 的 identity（`ZizhanAudioDmaMap.cpp:138`、`:176`）。
- 主线 `q6apmdai` 没有显式 pool，使用 DMA API 默认 IOVA；所以“必须在 0x10000000”不是主线的硬要求。
- 差异是事实，但是否影响 DSP 未知；DSP 只看低 32 位 IOVA，SMMU 可以映射任意 IOVA。

**最小静音实验**

- 保持物理缓冲 `0xf9e3b000` 不变，只把 IOVA 改成 `0x10000000`（或 `0x10001000`），页表映射 `iova -> pa`，DSP 地址用 `iova | 1<<32`。
- 用 H1 的单条 zero WRITE（0 预滚）跑 A/B：identity vs pool IOVA。
- 需要确认 IOVA 不与 table pool/其他映射重叠；`ArmSmmu4KBuild` 已支持任意 `iova/physical`。

**代码改动**

- `ZizhanAudioDmaMap` 增加 default-off 的 `zizhan_audio_iova` boot arg；`o.range={iova, o.base, o.bytes,...}`；`zizhan_audio_dma_map_address()` 返回 `iova | 1<<32`。
- 不改变 UEFI extent、不改变 SMMU context 其他字段。

**判定**

- pool IOVA 过、identity 挂：DSP/固件对 IOVA 范围有假设；保留 pool 映射。
- 两者一致：范围不是触发；不要为它改生产路径。

**风险/返回**：只改页表 IOVA；autoreset。

### H6：XPU/SCM/hyp assign

**证据**

- 厂商 `msm_audio_ion` 有 `IOCTL_MAP_HYP_ASSIGN`/`hyp_assign_phys`，目标 VMID 含 `LPASS/ADSP_HEAP/HLOS`。
- 但主线只对 **modem/mDSP stage-2** 用 SCM；ADSP 音频是 stage-1 + iommus（2026-08 LKML patch；`q6apm-dai.c:1209–1226`）。
- 本机 live DT 有 `iommus 0x1801`，`NUMS2CB=0`；没有 stage-2 context。

**最小静音实验**

- 目前没有安全的“只读 XPU 状态”入口；不建议在未确认 ABI 前调用 `qcom_scm_assign_mem`/`hyp_assign`。
- 若 H1/H2 都排除、且 fault 签名显示“SMMU 翻译成功但内存访问被拒”，再考虑用 ADSP 已拥有的 carveout 做对照；那是更大的改动，需要单独设计。

**判定**：在拿到 fault 签名前，H6 只是背景风险，不是本次首要假设。

### H7：IPCC 路由

**证据/实验/判定**：见 §5。低概率；用一次 `zizhan_ipcc_route=0` 对照排除。

## 7. 建议的下一个最小实验

> **2026-11-18 调整**：h/i/j 已读（§9.7、§11.0）：i（2000 ms）通过、4 条 `done` 约 11 ms；h/j 挂，j 在 send 后 2 s 内无 DONE。本节的 SMMU 0/16 KiB A/B 只在确认首块 DONE 不是“host 超时/失败收尾”问题后使用；checkpoint flush 只保留为次要扰动。

### 7.1 只读诊断改动（不改变行为）

在 `ZizhanAudioDmaMap.cpp` 的 `dma_watch` 里增加：

- `sCR0`（0x0）
- `sGFSR`（0x48）
- `sGFSYNR0/1/2`（0x4c/0x50/0x54）
- `CB_ACTLR`（cb+0x4）
- `lpass_tbu` base `0x151e9000` + 0x0 / +0x20
- 保持现有 `FSR`（cb+0x58）、`FAR`（cb+0x60）、`FSYNR0`（cb+0x68）

日志格式建议：

```
ZIZHAN_AUDIO_DMAWATCH: t_ms=... fsr=... far=... fsynr0=... gfsr=... gfsynr0=... gfsynr1=... gfsynr2=... scr0=... actlr=... tbu0=... tbu20=...
```

另加一个 default-off 的 `zizhan_dma_hold_ms`，在第一条 WRITE 后保持 map（watcher 继续跑）再 restore。如果要观察 restore 本身，watcher 应保持运行到 restore 完成之后再 stop，而不是现在的 `dma_watch_stop(); restore();` 顺序。不要在第一次实验里清 FSR/GFSR 或写 RESUME；先拿签名。

### 7.2 实验矩阵（每次只变一个变量）

| 组 | `pre_off` | WRITE 数 | amp | 目的 |
| --- | --- | --- | --- | --- |
| A | 0 | 1 条 zero | dry | 基线：0 预滚单条应过 |
| B | 16384 | 1 条 zero | dry | H1：16 KiB 单条是否挂 |
| C | 0 | 4 条（zero/tone 顺序同 mode 34） | dry | 区分“单条”与“4 次 transfer 时序” |
| D | 0 | 4 条 | hold/repeat + checkpoint | B 组：复现 stage 10 后的 `pulse_run`/第二条 WRITE；checkpoint 只保留日志 |
| E | 0 | 1 条 zero | dry + `ipcc_route=0` | H7 对照 |

每组都：

- `zizhan_audio_prm=34`（或一个只做零写的专用 mode）
- `zizhan_pulse_dry=1`（A/B/C/E）
- `zizhan_dma_watch=1`
- `pong_adsp=2`、`pong_audio=0`
- `pong_autoreset=210`、`pong_autoreset_sb=150`
- 不写任何分区；rear USB-A；电源键 3 s 回 fastboot

### 7.3 判定表

| 观察 | 解释 | 下一步 |
| --- | --- | --- |
| B 挂、A 过；`FSR.TF` + `FAR≈base+0x10000` | 首写整区预取越过 64 KiB | 预滚改 0；或扩 UEFI extent 到 128 KiB；再验 4 条 |
| B 挂、A 过；无 fault | 不是 SMMU 越界；可能是 DSP 对 16 KiB 地址/描述符的行为 | 查 DSP 命令/格式；试 H3 token |
| `FSR.ASF` + `FAR≈0x1_f9e3b000` | 高 tag 没被剥离/输入宽度不匹配 | 查 DSP tag 语义；不要改 SMR mask |
| `GFSR.USF=1` + `GFSYNR1≠0x1801` | 未匹配 SID | 查 tag/stream；补正确 SMR 或修 tag |
| `GFSR.USF=1` + `GFSYNR1=0x1801` | 全局 fault 的 SID 对，但 SMR/S2CR 状态不对 | 查发布顺序/读回；H2 |
| 无任何 fault，C/D 仍挂 | SMMU 不是直接触发；B 组（D）指向 pulse/第二条 WRITE | 先看 h/i/j 的 `done`/`fail` 与 flush 标记；2 s 内无 DONE 时转 H13（DSP/GLINK/SMMU 消费路径），不要先归因于 100 ms 超时 |
| E 与 A 相同 | IPCC 路由不是触发 | 排除 H7 |
| E 只在 route1 挂 | IPCC/GLINK 路径相关 | 查 GIC trigger、32 条预算、共享 receiver |

### 7.4 边界

- 这次只写文档；不实现、不构建、不碰手机。
- 下一次实验需要新的内核/包、设备窗口和返回路径；不能在本次无人值守窗口执行。
- 第一次失败保留现场，不重复同一轮；每次只变一个变量。
- 任何 `PASS` 必须是“真实启动返回 + 新鲜 klog + 预期日志”，不能把“已派发/已入队”当 PASS。

## 8. 未决问题

> **2026-11-18 n18 后**：checkpointed 挂死应按 §9 的 **A/B** 分组回答：A = 首块 WRITE 已发、DONE 未回；B = 首块 DONE 后 `pulse_run`/第二条 WRITE。klog flush 是次要扰动，不是主因；以下 SMMU/16 KiB/amp 问题在 A/B 的安全收尾之后仍有必要。

1. ADSP 在第一次 WRITE 时到底读 `buf_size` 还是整个 `MEM_MAP` region？没有 DSP 固件源码/总线 trace，只有几何相关性。
2. 16 KiB 3/3 挂是否真的是“越界 fault”，还是 DSP 对非零预滚地址的另一种行为？需要 A/B 单条实验。
3. `rep` 的 stage 10 挂是否在 `pulse_run` 的哪一步、第二条 WRITE 的 DONE 前后？需要 `done|fail sequence= us=` 日志（新诊断内核 064f32a5）。
4. `hold` 的 stage 9 挂是否在首块 WRITE_DONE 未回，还是 send 本身/GLINK 失败？需要 h/i/j 的 send/done 日志；现有 klog 不能回答。
5. `CB_ACTLR[73]` 的实际值是多少？是否继承了固件旧值？只读一次即可回答。
6. `sCR0` 的 `GFIE/USFCFG` 在当前 boot 是否仍为 `0x200406`？只读一次即可回答。
7. `lpass_tbu` 是否在挂死前进入 halt/ECATS？只读 base+0x0/+0x20 可回答。
8. ASID `0x1801` 在 16-bit/8-bit 硬件上的实际截断行为？读 TTBR/ID2 可回答；当前 `active_banks=0` 只是映射时刻的状态。

## 9. 夜间实测对照（2026-11-18 n18）

### 9.1 轮次、分组与挂点

`$ZIZHAN_SRC/work/m1-firstsound/n18-results.jsonl` 的 a/b/c/d 四臂（同一内核 `build-ckpt/kernel.sha256`，同一 OS/UI/helper，均为临时 `fastboot boot`、自动返回、无分区写）：

| 臂 | 实际 boot args（摘要） | 轮次 | 结果 |
| --- | --- | --- | --- |
| a | `zizhan_audio_prm=34`、`zizhan_pulse_dry=1`、`zizhan_dma_watch=1`、`zizhan_audio_checkpoint=1`、`zizhan_ipcc_route=1`；未给 `zizhan_pre_off`，即默认 16 KiB | a-1、a-3 通过；a-2 挂 | 2/3 过 |
| b | a 臂 + `zizhan_pulse_nopup=1` | b-1、b-2 都挂 | 0/2 过 |
| c | a 臂去掉 `zizhan_ipcc_route`（route0） | c-1 挂、c-2 过 | 1/2 过 |
| d | `zizhan_audio_prm=33`、`zizhan_dma_watch=1`、`zizhan_ipcc_route=1`，无 checkpoint | d-1 过；d-2 无效 | 1 个有效过 |

**d-2 不能算音频挂死**：`logs/n18-d-2.txt` 的 CONSOLE 只有头、没有正文（`size=0x42904`），DIAG 的 `sig=41400244` 与正常 `43474244` 不符、`size=0x4000ED8`；`n18-d-2.cycle.out` 明确 `klog not written (state 0, flushes 0, reason 0)`，没有 `dma_watch`、没有 audio 行。它是 **ramoops/早期启动损坏**，不是 mode33 的 WRITE 挂死。mode33 的唯一有效轮 d-1 通过。不要把 d-2 计入“mode33 也挂”。

**先撤回一个错误推断**：`amp_checkpoint()`（`ZizhanSe15Prepare.cpp:61–68`）的顺序是 `IOLog(stage=N)` → `pong_klog_flush(6)` → `IOLog(stage=N flushed=...)`。`flushed=` 行在 flush **完成之后**才打印，因此它**不可能出现在同一次 flush 写进 UFS 的 klog 里**；只有下一次 flush 才会把它带上。klog 尾部停在 `stage=N` 是正常现象，**不能**说明 flush 没返回。n18-results 里由 klog 统计出的 `writes`/send 行也不能用来判断 checkpoint 之后的顺序；checkpoint 写在 send 之前，send 行本来就可能不在这次 klog 数据里。

**现场屏幕证据（协调者，优先级高于 klog 尾部）**：

| 轮 | 屏幕最后看到 | 分组 |
| --- | --- | --- |
| amp-pulse-1117-ckpt | `stage9 ... us=14257`（flushed 行）→ 一条 `ZIZHAN_AUDIO_WRITE: send` → 重启 | **A**：第一条 WRITE 已发出，未见 WRITE_DONE |
| amp-pulse-1117-hold | `stage9 ... us=20594` → 卡住 | **A**：第一条 WRITE 已发出，未见 WRITE_DONE |
| amp-pulse-1117-rep | `stage10 ... us=15980` → 卡住 | **B**：stage 10 后在 `pulse_run`，第二条 WRITE 前后 |
| amp-pulse-1116-v1 | 无 checkpoint/klog | 未知；但**没有**任何 klog flush 也挂死 |
| amp-pulse-1117-klog | 无 checkpoint，最后 DATA_PIN | 未知；周期 klog，和 A/B 不能直接归类 |

按同一代码路径，n18 的有效轮可以这样归组：

| 组 | 轮次 | 最后的 checkpoint | 第一条 WRITE | 说明 |
| --- | --- | --- | --- | --- |
| **A** | ckpt、hold（屏幕确认）；n18-a-2、b-2（klog-only，A 候选） | stage 9 | **已发，未见 DONE** | ckpt/hold 有屏幕 send/flushed 证据；a-2/b-2 没有 send 行，只能按同一代码路径归为 A 候选，不能单独证明 send 已发 |
| **B** | n18-b-1、c-1、rep | stage 10 | 已 send+rx | 第一条 DONE 后进入 `pulse_run`，第二条 WRITE（sequence 1）前后 |
| **C（通过）** | n18-a-1、a-3、c-2、pre0、loud | stage 9/10 都有 flushed | 4 条 WRITE 全 send+rx | 同一 token、SMMU context、16 KiB/0 地址都能完成 4 写 |
| 其他 | n18-d-1 通过 2 写；n18-d-2 无效 | — | — | d-2 是 ramoops/早期启动损坏，不能算 mode33 挂死 |

**A 组的关键新事实（协调者核对）**：PRM 29–33（同样 amp core 运行、同样在 sample 49 做 WRITE）历史上共 6 轮 12 条 WRITE，**0 挂**；n18 新增 mode 26/27/28 共 10 轮，**0 挂**。只有 PRM 34 挂（约 8/14）。PRM 34 与 33 在第一条 WRITE 处的一个实质差异是：`write_commands()` 里 **mode 34 独有 100 ms 的 WRITE_DONE 超时**（其他模式 2 s，`ZizhanAudioPrm.cpp:351`）。**但 h/i/j 表明 2000 ms 也会挂（j），所以这个差异不是充分条件**。如果 host 活着到超时，`write_commands()` 返回 false，`zero_transfer()` 返回 0，amp `stop_fail`；当前 `f385720` 随后在 PRM line 473/476 短路并 `break`，保留 graph/clock/map，不执行 `pcm_commands(true)`（见 §9.6 H10、§11.3）。h/j 都在超时前挂死，因此“超时后失败收尾”不是已发生事实。

**SMMU 侧**：所有有 `dma_watch` 的有效 n18 轮都是 `fsr=400 gfsr=0`，没有变化、没有 fault；通过轮最后有 `DMAWATCH: end polls=... fsr=400 gfsr=0`，挂死轮没有 end 行。**SMMU fault 不是 A/B 组的证据**；A 组是 WRITE 已发、DONE 未回；B 组是第一条 DONE 已回、第二条 WRITE 前后。

> **A 组的证据强度**：`amp-pulse-1117-ckpt`/`hold` 有现场屏幕的 `flushed=` + `send`，可以确认首块 WRITE 已发；`n18-a-2`/`b-2` 只有 klog，klog 尾部没有 send 行，不能排除 send 行未落盘，也不能单独证明 send 已发出。把它们放在 A 是“同一 stage 9/同一代码路径”的归类，不是已证事实。`h-1`/`h-2` 同样只有 klog、没有 send 行，见 §11.0。

### 9.2 H8 撤回：klog flush 是次要扰动，不是主因

上一版把“klog 尾部没有 `stage=N flushed=`”当成 flush 未返回，这是错误的：

- `amp_checkpoint()` 的顺序是 `IOLog(stage=N)` → `pong_klog_flush(6)` → `IOLog(stage=N flushed=...)`；`flushed=` 在 flush 完成后打印，**同一次 flush 的 klog 不可能包含它**，只有下一次 flush 才会带上。
- 现场屏幕明确看到 ckpt 的 `stage9 ... us=14257`（flushed 行）之后还有 WRITE send，hold 的 `stage9 us=20594`、rep 的 `stage10 us=15980` 也都是 flushed 行之后才卡住。**flush 返回了，WRITE 已发出。**
- `amp-pulse-1116-v1` 没有任何 checkpoint/klog flush，同样挂死；这直接排除“checkpoint flush 是必要条件”。

因此 **H8（“checkpoint 同步 4 MiB flush 不返回”）撤回**。`pong_klog_flush(6)` 仍然是一个**次要扰动**：它确实在音频/amp 线程里同步写 4 MiB（`PongUfsTransport.cpp:288,327–342`；`PONG KLOG ... 1025 blocks`），持 `gHost.lock`，而 audio map 同时持有 `pong_smmu_lease_lock`（`ZizhanAudioDmaMap.cpp:129`）；UFS 的 `completeTransfer` 也用 `gHost.lock`（`:696–713`），Wi-Fi 初始化还要拿同一把 SMMU lease。它会改变时序、放大 UFS/APFS 启动窗口的延迟，所以应当移出关键路径或改成有界/异步；但现场证据表明它不是本次挂死的直接原因，不能再用它解释 A/B 组。

**正确的主线**：

- **A 组**：第一条 WRITE（pre-roll，sequence 0）已发出，`WRITE_DONE` 未回；`amp-pulse-1117-ckpt`/`hold` 的屏幕证据属此组。
- **B 组**：第一条 WRITE 已 `DONE`，进入 stage 10 后的 `pulse_run`，在第二条 WRITE（sequence 1）前后挂；`rep` 的 `stage10 us=15980` 属此组。
- **差异线索（已降级）**：PRM 29–33 的 12 条 WRITE 0 挂、n18 mode 26/27/28 的 10 轮 0 挂，只有 PRM 34 约 8/14 挂；PRM 34 在 `write_commands()` 里独有 **100 ms WRITE_DONE 超时**（`ZizhanAudioPrm.cpp:351`），其他模式是 2 s。但 i（2000 ms）通过、j（2000 ms + flush）仍挂，且 j 在 2 s 内没有 DONE；**超时值不是充分条件**。下一步是分离 j 的 send flush 与 DSP 消费，见 §11.0。

**为什么 SMMU 还是干净的**：A 组是 WRITE 已发但 DONE 未回，B 组是第一条 DONE 已回、第二条 WRITE 前后；两种情况都没有报告 `FSR/GFSR` 故障，`dma_watch` 也没有变化。SMMU fault 仍然不是当前证据；但“DSP/GLINK 在 DONE 前挂死”或“j 的 send flush 未返回”都可能不留下 SMMU fault。Qualcomm 平台文档说 unclocked access 会触发 fatal NoC error，但本案还没有抓到这类日志（见 §11 B/C）。

### 9.3 其他子系统的活动：没有唯一触发器

按现场屏幕和代码路径分组：**A = 第一条 WRITE（pre-roll sequence 0）已发出、未见 DONE**（ckpt、hold；n18 a-2/b-2 同路径）；**B = 第一条 DONE 已回、stage 10 后在 `pulse_run` 的第二条 WRITE 前后**（b-1、c-1、rep）；**C = 通过、4 条 WRITE 全 send+rx**（a-1、a-3、c-2、pre0、loud）。klog 的 send 行不能作为 A 组“未发”的证据：checkpoint 写在 send 之前，send 行可能不在这次 klog 数据里。

按每个锚点的前 ~300 行/整段 live log 对照（脚本在 `$ZIZHAN_SRC/work/codex-1118/parse_logs2.py`，原始摘要 `log-summary2.jsonl`）：

| 子系统 | 挂死轮的观察 | 通过轮的观察 | 判断 |
| --- | --- | --- | --- |
| UFS/klog | 锚点前最后一个 `PONG_UFS IO t=30s`；SystemOS/Data 大量读写；`tx_flush`/APFS 行；当前 checkpoint 的 `flushed=` 不在本次 klog 中（正常） | 同样有 t=30s/35s 和大量 UFS 活动；`flushed=0` 存在 | **不是唯一触发器**；4 MiB FUA flush 仍是次要扰动（会改变启动窗口时序），但现场已看到 flushed 行和 send，不能拿它解释 A/B |
| SpringBoard/launchd | a-2/c-1/ckpt 的 `Pong UI18: native home request accepted=1` / `main queue wake completed backlight=1 lockscreen=1` 在锚点前 2–7 行；b-1/b-2/hold/rep 没有 home/wake 行 | a-1/a-3/c-2/loud 的 home/wake 在锚点后 26–88 行；pre0 在锚点前 27–28 行；`amp-tone-1116-v1` 在锚点前 3–4 行且通过 | 不是唯一项；只是 startup 窗口的邻近事件。`amp-tone` 是清楚的负控制 |
| GPU ADRENO/DCVS/BO/scanout | `PONG ADRENO: DCVS started` 在很早（约锚点前 2000–2600 行）；最后一个 `PONG GPU DCVS` 约在锚点前 500–1600 行（取决于该轮采样/日志量）；`Pong Metal: real Vulkan-backed device created` 常见于锚点前 28–103 行（a-3 为 -225；hold/rep 无）；没有 `PONG DBUF: flip` | 同样的 DCVS/Metal 行；首个 `PONG DBUF: flip`/`GPU frame completed` 在锚点后 2353–2795 行 | 不是唯一项；GPU 首次 DCVS 报告不紧邻挂点；首个 UI flip 在挂死前未发生，但这只是挂死的结果。没有证据表明 GPU DCVS 是触发器 |
| Wi-Fi | `Pong WiFi: waiting for en0 link`、`wifid[...]` 在 a-2/c-1/hold 的锚点前 2–12 行；b-1/b-2/rep/ckpt 较远 | c-2 通过轮的 wifid 也在锚点前 2 行；a-1/a-3/loud 也有 wifid | 不是唯一项；Wi-Fi 与 SpringBoard 启动同窗，不能单独归因 |
| IPCC | sample 3 `at_ns≈44.2–44.9 s`，`unknown=0`、`budget=0`；挂死轮没有 sample 4（因为日志在 ~50 s 结束） | sample 3 相近，sample 4 `at_ns≈54.2–54.9 s` 存在 | 计数健康；route0/1 都挂；不是 IPCC 丢消息/风暴。sample 4 缺失是日志终止，不是计数异常 |
| ADSP/GLINK | A 组第一条 WRITE 已发、DONE 未回；B 组第一条 WRITE 已 send+rx，第二条 WRITE 前后卡住；没有 DSP fatal/GLINK 错误 | C 组 4 条 WRITE send+rx | A 是**首块 DONE 未回**，B 是**首块 DONE 后第二块**；不是简单的“DSP 读共享内存立刻挂” |
| UFS transport | `PONG_UFS` 启动行正常、`PONG KLOG 1025 blocks`；t=30s 统计 `busy=0ms`、`longest=29–50ms`；`tx_flush` | 同样的启动行和 t=30/35s 统计 | 没有 UFS error 行；klog flush 是次要扰动，不是 A/B 的主因 |
| 电源/autoreset | `PONG_AUTORESET: SpringBoard up at 17s`；210 s deadline；`pong_klog_period` 在 n18 包中未设置 | 同样 | pinger 的停止是下游；不能据此证明“pinger 先停”，也不能反推 checkpoint flush |

**关键负控制**：`amp-tone-1116-v1` 在 home/wake 后 3–4 行就 send（`-4/-3`），且**通过**；`pre0` 在 home/wake 后 27–28 行 send，也通过。这直接否定了“只要音频 WRITE 紧邻 SpringBoard home/backlight 就挂”的简单时序假设。挂死与 home/wake 的接近只是同一 startup 窗口内的相关，不是充分条件。

### 9.4 挂死时刻相对开机的时间分布

两个独立计时器把同一锚点夹在同一个窗口：

| 轮 | 锚点前最后一个 `PONG_UFS IO` | 锚点前最后一个 IPCC sample | 锚点后第一个 IPCC sample | 锚点后第一个 `PONG_UFS IO` |
| --- | --- | --- | --- | --- |
| n18-a-1 过 | `t=30s` | sample 3 `at_ns=44.18s` | sample 4 `54.21s` | `t=35s` |
| n18-a-2 挂 | `t=30s` | sample 3 `44.28s` | 无（日志终止） | 无 |
| n18-a-3 过 | `t=30s` | sample 3 `44.75s` | sample 4 `54.77s` | `t=35s` |
| n18-b-1 挂 | `t=30s` | sample 3 `44.79s` | 无 | 无 |
| n18-b-2 挂 | `t=30s` | sample 3 `44.30s` | 无 | 无 |
| n18-c-1 挂 | `t=30s` | route0，无 stats | — | 无 |
| n18-c-2 过 | `t=30s` | route0，无 stats | — | `t=35s` |
| n18-d-1 过 | `t=30s` | sample 3 `44.39s` | sample 4 `54.48s` | `t=35s` |
| amp-pulse-1117-ckpt 挂 | `t=30s` | sample 3 `44.79s` | 无 | 无 |
| amp-pulse-1117-hold 挂 | `t=30s` | sample 3 `44.79s` | 无 | 无 |
| amp-pulse-1117-rep 挂 | `t=30s` | sample 3 `44.49s` | 无 | 无 |
| amp-pulse-1117-pre0 过 | `t=30s` | sample 3 `44.78s` | sample 4 `54.83s` | `t=35s` |
| amp-pulse-1117-loud 过 | `t=30s` | sample 3 `43.90s` | sample 4 `53.99s` | `t=35s` |

读法：

- **所有 checkpointed live 挂死都在 `PONG_UFS IO t=30s` 之后、`t=35s` 之前；都在 IPCC sample 3（≈44–45 s）之后、sample 4（≈54–55 s）之前。** 两个计时器的零点不同（UFS monitor 与 IPCC `at_ns` 有固定偏移），所以不要把 30 s 和 44 s 当成同一时钟；它们只是独立地把锚点夹在同一个 ~10 s 窗口。`amp-pulse-1117-klog` 没有 checkpoint，另列为周期 klog 旁证；`1116-v1` 没有 klog，仍未知。
- **通过轮也在同一个窗口**（a-1/a-3/c-2/pre0/loud 的锚点同样在 t=30–35 s、sample 3–4 之间），所以“时间窗”本身不区分挂/过；区分是 **A/B 的首块/第二块 WRITE_DONE 是否返回**，以及 send flush 是否返回。
- **h/i/j 新轮也落在同一窗口**：h-1/h-2/j 的最后一个 `PONG_UFS IO` 都是 `t=30s`，没有 `t=35s`；i 继续到 `t=165s` 并完成 4 条 WRITE。挂死不是“i 的 DONE 稍慢”，而是 h/j 在同一个启动窗口内提前终止。
- **不是 GPU DCVS 首次报告**：`PONG ADRENO: DCVS started` 在早期（约锚点前 2000 行），第一个 `PONG GPU DCVS` 在锚点前约 500–700 行；通过/挂死都有，且没有紧邻。
- **首个 `PONG DBUF: flip`/`PONG SCANOUT: GPU frame completed` 在所有通过轮都出现在音频锚点之后**（+2353～+2795 行）；挂死轮没有 flip。这只能说明挂死发生在 **SpringBoard 的首个 UI frame 之前**，不能反推是 GPU/flip 导致。
- 这个窗口对应 **SpringBoard/launchd/Metal/wifid/APFS 启动 + 首批 UFS I/O**；`PONG_AUTORESET: SpringBoard up at 17s` 更早。**挂死与 startup 的 UFS 负载同窗，而不是与某个单一 SpringBoard/GPU 事件同点。**

### 9.5 AP 侧 MMIO/时钟路径核对

用户关心的“AP 是否在访问未开时钟/未上电寄存器”逐项核对：

**UFS klog（次要扰动；不是 A/B 的主因）**

- `amp_checkpoint` 在音频/amp 线程同步调用 `pong_klog_flush(6)`；`pong_klog_flush` 取 `gHost.lock` 并写 **4 MiB FUA**（`PongUfsTransport.cpp:288,327–342`）。
- UFS block I/O 的 `completeTransfer` 也取 `gHost.lock`（`:696–713`）；SpringBoard/launchd/APFS 正在 t=30 s 附近做大量 UFS 读写（`PONG_UFS IO t=30s`、`tx_flush`）。
- `gHost.lock` 是普通 `IOLock`，**`pong_klog_flush` 的 `IOLockLock` 没有超时**；`hostWrite4kLocked` 每 128 KiB 只有 1 s 命令超时（`RootCommands.h:303–314`），最坏 32 块 ×1 s = 32 s，加上锁等待/APFS 竞争可超过 60 s 冷咬窗口。这不是“未开时钟寄存器”，而是 **长同步 UFS I/O + 无超时锁 + startup I/O 竞争**。
- 但现场屏幕已看到 flushed 行和 send，`amp-pulse-1116-v1` 无 checkpoint 也挂死；因此它是**时序扰动/次要放大**，不能解释 A/B 组的主挂点。保留它是为了说明为什么“去掉 checkpoint”仍应作为卫生改进，而不是把它当根因。
- 音频线程在 flush 期间仍持有 **全局 `pong_smmu_lease_lock`**（`ZizhanAudioDmaMap.cpp:129`，直到 `:167–173` 才释放）。Wi-Fi 的 `PongWifiPlatform.cpp:432` 也要拿同一把 lease；如果 Wi-Fi 正在启动，它会等待。**这是一条可解释的锁链：audio → klog flush → gHost.lock；audio 同时持有 SMMU lease → Wi-Fi/其他 SMMU writer 等待。** 但“锁链如何让 Gunyah 冷咬”仍未被直接日志证明；不能把 UFS 锁链写成唯一机制。
- `PongBootReset.h:176–184` 的 pinger 在 `pong_klog_period>0` 时也会调用 `pong_klog_flush(5)`；n18 包没有设置 `pong_klog_period`，所以 n18 的 pinger 不走这条 flush。**11-17 的 `amp-pulse-1117-klog` 设置了 `pong_klog_period=5`，最后停在 DATA_PIN、没有 WRITE send；它可能是周期 klog flush 的独立问题，也可能只是 send 行未落盘**。不能把它和 n18 的 A/B 混为一组。

**SE15 / Geni I2C**

- `ZizhanSe15Prepare.cpp:98–146` 映射 SE15（0x880000）、I2C SDA/SCL（0xf138000/0xf139000）、reset（0xf101000）、TLMM 0xf100000+pin；`zizhan_geni::prepare` 在 setup 时检查 `GENI_CLK_CFG & 1`（`ZizhanGeniTiming.h:38–63`）。
- `run_state()`/`pulse_run()` 在每次传输前**不再重读 `GENI_CLK_CFG`**；`GeniI2c` 的 `prepare` 只做一次时钟检查。`poll_until()` 有 20 ms 软件预算（`ZizhanCs35l41.c:12–20`），`finish_transfer` 超时会调用 `geni_i2c_timeout`（`:22–43`）。
- SE15 属于 AP/AOSS QUP 域，ADSP 的 PRM 改的是 LPASS 时钟，不是 SE15；没有代码路径表明 ADSP 会关 SE15。若 SE15 时钟真的被关，`geni_i2c_poll` 会在 20 ms 预算后失败，而不是无限等待；只有 **AXI 读本身卡住** 才会越过软件预算。日志里没有 SE15/GENI 的 error/timeout 行；**没有证据表明 run_state/I2C 在访问未开时钟的 SE15**。

**TLMM（SCK/WS/data）**

- `Page::open`/`read`/`write`（`ZizhanSe15Prepare.cpp:19–41`）用 `kIOMapInhibitCache` 映射 TLMM，`read` 用 `dmb oshld`、`write` 用 `dsb oshst`；data pin connect 在 transfer 之前（`:218–226`）。
- TLMM 是 always-on；音频路径在 `write_commands()` 等待期间不再写 SCK/WS/data pin，只有 `pulse_run` 之后恢复。**没有 AP 在 WRITE 等待时访问 TLMM 的路径**；ADSP 的 TDM 与 AP 的 pin mux 已在 connect 前协调。

**SMMU 0x15000000**

- `dma_watch` 每 1 ms 只读 context FSR/FAR/FSYNR0 和 GFSR（`ZizhanAudioDmaMap.cpp:107–120`），不写 SMMU；audio map 的写只在 install/restore。
- SMMU 是 apps SMMU 常驻块；没有 clock-gate 检查，但 SMMU 访问在 install/restore 已经成功；dma_watch 没有 timeout/错误。**没有证据表明 SMMU 在未开时钟时被访问**。
- audio map 持有全局 lease；这是 §9.2 的次要锁链，不是未上电寄存器。

**IPCC 0x0ed18000**

- `ZizhanIpcc.cpp:47–56` 在 IRQ 里 `RECV_ID`/`RECV_CLEAR`；`PongAdsp.cpp:121–145` 旧路径同样；寄存器映射 `kIOMapInhibitCache`，没有 clock-gate 检查。IPCC 是 AOSS always-on。
- route0（c-1 挂、c-2 过）与 route1（a/b 挂/过）都存在；IPCC 计数 `unknown=0/budget=0`。**没有证据表明 IPCC 是共同触发**；它需要一次 route0 对照，而 n18 已经有 route0 挂/过，说明 route 不是决定项。

**LPASS 时钟**

- audio 路径通过 GPR/PRM 请求 LPASS core/macro clock，AP 本身不直接写 LPASS clock 寄存器；AP 的 `PONG GPU DCVS`/display 是 GX/DISPCC，不是 LPASS core。`PONG_AUTORESET`/SpringBoard 的 home/backlight 也不直接改 LPASS 时钟。
- **没有发现 AP 在 WRITE 期间访问未上电 LPASS 寄存器的代码路径。** 如果 ADSP 的 PRM 时钟被外部事件影响，日志应先出现 PRM/ADSP 错误；`flushed=` 缺失不是证据（它本来就在下一次 flush 才写）。

### 9.6 对 §6 假设清单的修正

> **2026-11-18 02:36 更新**：下表的 H9–H13 排序是根因发现前的排除记录；最终根因是内核栈溢出（§12）。H13 的“首块 WRITE 路径挂住 SoC”是症状描述，不是机制。

| 假设 | 夜间数据 | 修正 |
| --- | --- | --- |
| H1 16 KiB 越过 64 KiB | a-1/a-3 都是默认 16 KiB 且 4 写通过；a-2 也是 16 KiB，属 A 组；ckpt 16 KiB、hold 0 都挂 | **不再支持“16 KiB 是确定性触发”**；保留为真实 WRITE 的潜在边界风险，但当前 A/B 不是它 |
| H2 SMMU fault/terminate | 所有有 dma_watch 的 n18 轮 `fsr=400 gfsr=0`，无变化；A 组是 WRITE 已发/DONE 未回；B 组是首块 DONE 后第二块 | **不再支持 SMMU fault 是当前挂死原因**；SMMU 编程差异仍是潜在风险，但没有本次故障签名 |
| H3 WRITE/MEM_MAP token | 同一 token 编码在 a-1/a-3/c-2/pre0/loud 的 4 写全部成功；A/B 的首块 token 也相同 | **不是当前挂死原因**；仍是协议一致性 bug，保留为后续修复 |
| H4 amp/TDM/pulse | B 组（b-1/c-1/rep）是第一条 WRITE DONE 之后、stage 10 后的 `pulse_run`，第二条 WRITE（sequence 1）前后；A 组是第一条 WRITE | **B 组重新把 amp/TDM/pulse 纳入候选**；A 组在首块，不是 pulse；amp/TDM 的“无声”仍需独立验证 |
| H5 IOVA 范围 | a-1/a-3 用 identity 0xf9e3b000 通过 4 写；无 pool IOVA 对照 | 没有证据；保留为未测差异 |
| H6 SCM/XPU | live DT iommus、NUMS2CB=0；ADSP stage-1；无 SCM assign | 没有证据；不要加 |
| H7 IPCC | route0/1 都挂/过；unknown/budget=0 | 不是共同触发；route0 对照已完成 |
| **H8（撤回）同步 4 MiB klog flush / UFS 锁** | 现场屏幕已看到 `stage9 us=14257` flushed 行 + send；hold `us=20594`、rep `us=15980` 也是 flushed 后卡住；1116-v1 无 checkpoint 也挂 | **撤回**；`flushed=` 本来只在下一次 flush 落盘，klog 尾部不能证明 flush 未返回。仅保留为次要时序扰动 |
| **H9（降级）mode34 100 ms WRITE_DONE 超时过短** | i（2000 ms）通过，4 条 `done us≈11 ms`；h-1/h-2（100 ms）挂；j（2000 ms + flush）也挂，且 `send sequence=0` 后没有 `done`/`fail` | **不是充分原因**：正常 DONE 远小于 100 ms；j 证明放宽到 2 s 也不保证。100 ms 仍是 code smell，但不能单独解释挂死 |
| **H10（降级/未证）未完成 WRITE 时拆 graph/UNMAP/关时钟** | j 在 2 s 超时前已挂，未进入失败收尾。当前 `f385720` 在 `!amp_pass` 时 line 473 短路、line 476 `break`，line 496 的 `pcm_commands(true)` 不执行；graph/clock 未释放，SMMU map 由 `zizhan_audio_dma_map_retain()` 保留，只有 amp core 关闭和数据脚恢复。协调者描述的 STOP/CLOSE/释放时钟路径与此不符，需以设备内核 `064f32a5` 实际代码为准 | 仍是协议风险（mainline 不等待 DONE；Qualcomm NOC 文档称 unclocked 访问可致命），但不是 j 的已证触发；不能写成“已发生” |
| **H11（降级）首块 WRITE_DONE 的启动/预缓冲延迟 >100 ms** | i 的首块 DONE 为 11 ms；j 在 2 s 内没有 DONE；现有样本没有“100–2000 ms 才 DONE” | 没有证据支持“首块只是稍慢”；A 组是 **DONE 不回且整机先挂**，不是 100 ms 预算不够 |
| **H12（次要，j 待分离）klog flush 时序扰动** | 4 MiB FUA / `gHost.lock` / SMMU lease 仍在音频线程；j 的 `send` 行已落盘，但没有 flush 后确认行，无法判断 send flush 是返回后 DSP 才挂，还是 flush 自身未返回 | 卫生问题，需移出关键路径；j 的归属未定，不能把 H12 排除也不能当主因 |
| **H13（新，已被 §12 取代）首块 WRITE 路径本身在 DONE 前挂住 SoC** | j 的 `send sequence=0` 已发出、2 s 内无 DONE/fail，92 s 后看门狗复位；i 用同样地址 `1f9e3f000`、同样 token 编码，首块 11 ms 完成；SMMU `fsr=400 gfsr=0` 无变化 | 症状描述；最终原因是 `IOLog`→kqueue 栈溢出（§12），不是 DSP/GLINK/SMMU 消费竞态 |

### 9.7 结论与下一步

**可以直接下的结论**：

- 11-17 的“5/7 ADSP WRITE 挂死”应按 **A/B** 分组：A = 第一条 WRITE 已发、DONE 未回；B = 第一条 DONE 后、stage 10 后的 `pulse_run`/第二条 WRITE。**不是** checkpoint flush 观察者效应；H8 撤回。
- **SMMU FSR/GFSR、IPCC 计数在 n18 挂死前都没有异常**；没有证据支持 SMMU stage-1 fault 或 IPCC 风暴。A 更像“DSP/GLINK 在首块 DONE 前挂”，B 是“首块 DONE 后第二条 WRITE/pulse”；失败收尾/UNMAP/关时钟的风险只在 host 活着到超时时才可能发生，且需要新内核验证。这些都不一定留下 SMMU fault。
- **音频协议本身在 n18 的通过轮完成了 4 条 WRITE send+rx**（a-1/a-3/c-2/pre0/loud）；之前 pre0/loud 的无声音不是“WRITE 没成功”，而是 amp/TDM/时序问题。
- **PRM 29–33（12 条 WRITE）0 挂、n18 mode 26/27/28（10 轮）0 挂，只有 PRM 34 约 8/14 挂**；mode34 独有 100 ms WRITE_DONE 超时，但 h/i/j 显示 2 s 也会挂，超时值不是充分条件。
- **h/i/j 的新对照**：i（2000 ms、无 flush）4 条 WRITE 全部 `done us≈11 ms`，含 16 KiB 首块；h-1/h-2（100 ms）与 j（2000 ms + send flush）都在 92 s 看门狗复位前挂死。j 的 `send sequence=0` 已落盘、无 `done`/`fail`；若 send flush 已返回，这就是 A 组“DONE 不回、整机先挂”的直接实例；若 flush 未返回，则是 H12。现有日志缺少 flush 后确认行，不能二选一。
- **d-2 是无效的 ramoops/早期启动轮**；不能作为 mode33 挂死证据。mode33 的有效轮 d-1 通过。
- **11-17 `amp-pulse-1116-v1`（无 checkpoint，klog 未写）和 `amp-pulse-1117-klog`（周期 klog 5 s，最后 DATA_PIN）仍是独立问题**；1116 证明“无 flush 也能挂”，klog 可能是周期 flush 或 send 行未落盘，均不改变 A/B 主线。

**下一步（最小、单变量）**：

1. **先分离 j 的三种可能**：send 返回后 DSP 消费、send flush、GLINK 收包。最小改动是在 send flush 之后加一行 `ZIZHAN_AUDIO_WRITE: sent-flushed`；若该行出现后才挂，A（H13）成立；若不出现，j 归 H12。
2. **重跑 i 条件（2000 ms、无 flush）至少 3 次**：单轮 i 通过不能证明 2 s 稳定；若仍偶发挂死，H9/H11 基本排除，转 H13 的 DSP/GLINK/SMMU 分段。
3. **按 §11 C 的安全顺序做失败收尾**：WRITE 超时/失败时先停新写、发 FLUSH/STOP/EOS、等 DONE/stop ack，再 UNMAP/CLOSE/释放时钟；超时则保留 map/图/时钟/缓冲直到复位。不要用“100 ms 到了”作为拆资源的理由。
4. **保留 klog 作为次要卫生项**：把 checkpoint 改成 **有界、非阻塞**：
   - 只写本次新增的 console 行（或 ≤64 KiB），不每次重写 4 MiB；
   - 从独立低优先级 worker 写，不在音频/amp 线程写；
   - 用 `IOLockTryLock`/有界重试，不让 `pong_klog_flush` 无限等 `gHost.lock`；
   - 不在持有 `pong_smmu_lease_lock` 的区间内做 4 MiB UFS 写（先释放 lease，或把 checkpoint 移到 map 之外）。
5. 然后回到真实音频：amp 保持 + 连续数据 + 可闻测试；SMMU 只在“安全收尾后真实 WRITE 仍挂”时按 §7 的只读诊断重开。
6. 不要用 d-2 复跑 mode33；若需要 mode33 证据，先确认 ramoops/console 有效、有 `dma_watch`/klog，再计入。

**边界**：本次仍只读日志和源码；没有手机操作、没有构建、没有分区写。下一步需要新内核/包和设备窗口，不能在本次 03:00 前无人值守执行。

## 11. WRITE_DONE 语义与失败收尾调研（2026-11-18）

（编号按协调者要求保留为 §11；原 §10 参考已移到 §13，因此本节前没有 §10。）

### 11.0 h/i/j 实测对照（2026-11-18 01:36–01:52）

> 这些轮的最终根因见 §12：`IOLog`/kqueue 路径把 16 KiB 内核栈用尽；本节保留 WRITE_DONE 语义与“send 后没有 done”的记录。

这几轮由协调者运行；本文只读 `$ZIZHAN_SRC/logs/n18-{h,i,j}-*`，没有碰手机。各轮都是 `zizhan_audio_prm=34`、`zizhan_pulse_dry=1`、`zizhan_ipcc_route=1`、`pong_klog_period=2`，source revision `f385720`，只改 WRITE_DONE 预算和 flush：

| 轮 | 设置 | 结果 | 关键证据 |
| --- | --- | --- | --- |
| n18-h-1 | 默认 100 ms，无 send flush | 挂；92 s 后看门狗复位 | klog 没有 `ZIZHAN_AUDIO_WRITE: send`，也没有 `done`/`fail`；最后一组音频行是 `ZIZHAN_AUDIO_PULSE: flags dry=1 nopup=0` |
| n18-h-2 | 同 h-1（重复） | 挂；92 s 后看门狗复位 | 同样没有 send/done/fail；最后音频行仍是 `ZIZHAN_AUDIO_PULSE: flags dry=1 nopup=0` |
| n18-i-1 | `zizhan_write_ms=2000`，无 send flush | 通过；4 条 WRITE 全完成 | `sequence=0` 地址 `1f9e3f000`，`done ... us=11079`；`sequence=1..3` 分别 `11082/11075/11067 us`；最后 `DMAMAP: PASS installed=0`、`PRM: PASS` |
| n18-j-1 | `zizhan_write_ms=2000`，`zizhan_write_flush=1` | 挂；92 s 后看门狗复位 | klog 最后一条音频行是 `send ... token=7a1b0001 address=1f9e3f000 ... sequence=0`；没有 `done`/`fail`，也没有后续 `rx` |

**这组数据说明什么**

- **正常首块 DONE 远小于 100 ms**：i 的首块（16 KiB 预滚、sequence 0）11 ms 完成，后续块也在 11 ms 左右。H9 的“100 ms 不够”因此不能作为唯一解释；H11 的“首块天然要 >100 ms”也没有被样本支持。
- **2 s 预算也不保证**：j 用 2000 ms，仍在看门狗前挂死；如果 `send` 之后的 flush 已返回，那么 j 是 A 组的直接实例——**DONE 根本没回，不是回得晚**。如果 flush 没返回，j 属于 H12（klog flush 自身），不是 DSP 消费。现有诊断没有“send flush 已完成”的确认行，二者不能从这份 klog 区分。
- **h 不能单独定性**：h-1/h-2 都没有 send 行，可能是第一条 WRITE 尚未发出、send 行未落盘，或挂死发生在 send/flush 之前；两次重复只能说明这个 100 ms 分支可重复挂死，不能把它当成“100 ms 超时后收尾挂死”的证据。
- **同一地址、同一 token 编码既可通过也可挂**：i 用 `1f9e3f000` 通过，j 用同一地址在 send 后挂；H1（16 KiB 必然越界）和 H3（token 编码必然错）都不能解释这种差异。差异更可能在 MEM_MAP 句柄、SMMU 表/上下文状态、GLINK 投递或 DSP 消费时序。

**日志解释的边界**：`h`/`j` 的 klog 都来自 UFS 副本，`n18-j-1.cycle.out`/`n18-results` 的 `klog_reason` 是 `periodic`，不是 `write`；这既可能是 send flush 写完后又有一次 2 s 周期 flush，也可能是 send flush 写到数据块后、header 前挂住。没有“flush 返回”标记时，不能把 `send` 行当成“send flush 已返回”的证明。

### 11.1 A：WR_SH_MEM_EP 的首块 WRITE_DONE 何时返回

**API 契约（AudioReach 公开头文件）**

- `fwk/api/modules/wr_sh_mem_ep_api.h:62–77`：客户端把数据写入 `DATA_CMD_WR_SH_MEM_EP_DATA_BUFFER_V2` 指定的缓冲；模块从该缓冲读取；**“Once the buffer is consumed, the module raises DATA_CMD_RSP_WR_SH_MEM_EP_DATA_BUFFER_DONE_V2”**。
- 同文件 `:227–260`：DONE 表示 **“the referenced write buffer has been fully consumed and is available to the client”**；`data_status` 表示是否成功消费。
- 同文件 `:307–334`、`:354–394`：EOS 是独立的 `DATA_CMD_WR_SH_MEM_EP_EOS`，`EOS_RENDERED` 表示流末尾已被最终端点渲染/丢弃；**不是每个 WRITE 都需要 EOS**。

**SPF reference implementation（AudioReach/audioreach-engine）**

- `fwk/spf/containers/gen_cntr/ext/wr_sh_mem_ep/src/gen_cntr_wr_sh_mem_ep.c:341–452`：`gen_cntr_copy_gpr_client_input_to_int_buf()` 把共享内存数据拷入 container 的 **internal buffer**；只有 `actual_data_len == 0` 且该外部缓冲没有被 internal input 持有时，才调用 `gen_cntr_free_input_data_cmd()` 释放并回 DONE。
- 同文件 `:443–452`：如果 internal input 仍持有该缓冲（`GEN_TOPO_BUF_ORIGIN_EXT_BUF` 且 `actual_data_len` 非 0），DONE **延后**，等 process 后再释放。
- 同文件 `:1481–1559`：`gen_cntr_free_input_data_buffer_cmd_gpr_client_v2()` 在拷贝/消费完成后生成 `DATA_CMD_RSP_WR_SH_MEM_EP_DATA_BUFFER_DONE_V2`，填回原地址/句柄/status，并在发送前 dec refcount/invalidate cache。
- 同文件 `:769–947`：`gen_cntr_input_data_buffer_set_up_gpr_client_v2()` 解析 `data_buf_size`、映射/失效 cache；零长度或非整数 PCM 会立即返回（带错误 status），正常缓冲进入拷贝流程。
- 同文件 `:1009–1019`：container 的 input queue 同时处理 `DATA_BUFFER_V2` 和 `EOS`；EOS 走独立的 EOS 处理，不与每个 WRITE_DONE 合并。

**因此，首块 WRITE_DONE 的语义是**：

1. **不是“播放完”**；它是共享内存缓冲被 DSP **完全读取/拷入内部缓冲**，client 可以回收该缓冲。
2. **首块可能被延迟**：如果 graph 刚 START、internal buffer 还没空间、或下游/内部 input 仍持有该缓冲，DONE 会推迟到下一次 process。**100 ms 不是协议保证**；协议层不能排除首块超过 100 ms。但“可能延迟”不等于本次实测：i 的首块是 11 ms，j 是 2 s 内完全不回。
3. **mainline q6apm 没有 100 ms 超时**：`q6apm.c:480–500` 的 `q6apm_write_async()` 只是异步入队；`q6apm.c:591–610` 在收到 DONE 时更新 `hw_ptr`/`snd_pcm_period_elapsed`；`q6apm-dai.c:423–445` 的 `ack` 只负责按 ALSA 指针排入 WRITE，不等待固定毫秒数。**vendor/reference client 等待的是 DONE 事件，不是 100 ms。**

**来源限制**：`audioreach-engine` 是公开的 generic SPF reference；SM8475 的实际 ADSP 镜像不是公开源码，具体调度可能不同。但 `wr_sh_mem_ep_api.h` 的 API 契约与 `gen_cntr_wr_sh_mem_ep.c` 的 reference 行为足以说明：**DONE 不是固定 100 ms 内必回**。

**与 h/i/j 的关系**：i 证明这条 graph 的正常首块 DONE 约 11 ms；j 证明存在“send 已发出、2 s 内 DONE 完全不回”的样本；h 没有 send 行，不能用于判断 DONE 延迟。因此 A 的准确表述是：**DONE 可能不回，而且 SoC 在 host 预算耗尽前就挂死**；不是“DONE 比 100 ms 慢一点，host 提前放弃”。这也意味着把 `zizhan_write_ms` 从 100 改到 2000 只能排除 host 超时，不能修复 DSP/GLINK/SMMU 侧的挂死。

### 11.2 B：有未完成 WRITE 时 STOP/CLOSE/UNMAP/关时钟

**mainline q6apm 的实际收尾顺序**

- `q6apm-dai.c:570–588` `q6apm_dai_close()`：若 stream 曾启动，先 `q6apm_graph_stop()`，再 `q6apm_free_fragments()`，最后 `q6apm_graph_close()`。
- `q6apm-dai.c:860–878` `q6apm_dai_compr_free()`：`q6apm_graph_stop()` → `q6apm_free_fragments()` → `q6apm_unmap_memory_fixed_region()` → `q6apm_graph_close()` → `snd_dma_free_pages()`。
- `q6apm-dai.c:447–473` `trigger STOP`：只设置 `state=STOPPED`、`queue_ptr=0`；**不发送 GRAPH_STOP**，真正 STOP 在 close 路径。
- `q6apm.c:828–840` `q6apm_graph_stop()`：发送同步 `APM_CMD_GRAPH_STOP`，等待 basic result；等待在 `q6apm.c:97–121` 的 `audioreach_graph_mgmt_cmd()` → `q6apm_send_cmd_sync()`，超时是 **5 s**（`audioreach.c:617–620`）。但 `q6apm_dai_close()`/`q6apm_dai_compr_free()` **忽略** `q6apm_graph_stop()` 的返回值，STOP 超时后仍继续 free/UNMAP/CLOSE。
- `q6apm.c:792–803` `q6apm_graph_close()` 只做 kref/port 释放，**没有每块 WRITE_DONE 的等待**。
- `q6apm.c:372–377` `q6apm_free_fragments()` → `audioreach.c:1547–1562` `audioreach_graph_free_buf()`：在 `graph->lock` 下直接把 `rx_data.buf`/`tx_data.buf` 设为 NULL 并 `kfree`；它不等 DSP 的 per-buffer DONE。
- `q6apm-lpass-dais.c:185–203` `q6apm_lpass_dai_shutdown()`：先 `q6apm_graph_stop()`，再 `q6apm_graph_close()`；`q6i2s_lpass_dai_shutdown()`（`:300–305`）在这之后才 `q6apm_lpass_dai_disable_clocks()`。顺序是 **STOP → CLOSE → 关时钟**，不是先关时钟。

**已知竞态/失败模式**

- **CVE-2026-72257**（`ASoC: qcom: q6apm: fix NULL pointer dereference in graph_callback`）：`q6apm_free_fragments()` 释放 `rx_data.buf` 后，DSP 的 late `DATA_CMD_RSP_WR_SH_MEM_EP_DATA_BUFFER_DONE_V2` 仍可能到达；`graph_callback()` 在旧代码里读 `buf[token].phys` 会 NULL deref。修复只是**加 NULL 检查**，不是“等所有 DONE 再 free”。这直接说明 mainline 的 close/free 路径**不保证** outstanding WRITE 已归零；它容忍 late DONE（避免 AP kernel crash），但 DSP 侧缓冲生命周期仍然没有显式握手。
- **`q6apm-lpass-dai: close graph on prepare errors`**（上游 commit `be1fae62cf25…`）：错误路径不 close 会把 graph 留在 DSP；下一次加载同一 graph 会失败，板子要重启。说明“失败时草率停止/留下图”会造成 DSP 状态不一致。
- **`q6apm-dai: reset queue ptr on trigger stop`**（2026-03 patchset）：STOP 不重置 `queue_ptr` 会向 rpmsg 洪泛写请求。说明“STOP 时仍有在途 WRITE”需要显式清队列/等 DONE，而不是只改状态。
- **`q6apm: Add support for early buffer mapping`**（LKML 2026-02）：把 MAP 提前到 pcm_new、UNMAP 放到 pcm destruct；说明 map/unmap 的生命周期要与 PCM/stream 生命周期对齐，不能在仍有 WRITE 时随意 UNMAP。
- **Qualcomm 平台文档（RB5，`Erroneous transaction on bus`）**：`NOC_ERROR`/timeout 是 fatal error，包括 **unclocked bus access**；NOC 向 TrustZone 发 FIQ。XPU violation 也明确列出“device controller 因时钟不可用而不工作”。这给出“DSP/码流在时钟关闭后取数 → NoC/XPU fatal → 整机复位”的硬件机制，但它是平台级描述，不是本案的直接日志。

**AudioReach APM 的 close 序列**

- `fwk/spf/apm/core/src/apm_cmd_sequencer.c:202–213`：子图处于 `STARTED` 时，`GRAPH_CLOSE` 的处理序列先发 `SPF_MSG_CMD_GRAPH_STOP`，再发 `DISCONNECT`、`CLOSE`；这是“先停再关”的方向，但**没有 per-buffer DONE 的显式等待**。container STOP 负责把内部缓冲/外部缓冲处理掉。
- `gen_cntr_wr_sh_mem_ep.c:1568–1634` 的 flush 路径会对队列中的 `DATA_BUFFER_V2` 调 `gen_cntr_free_input_data_buffer_cmd_gpr_client_v2(..., is_flush=TRUE)`，后者在 `:1481–1559` 生成带 status 的 `DATA_BUFFER_DONE_V2`；EOS 则走独立的 `:1600–1628` 分支。也就是说，**GRAPH_FLUSH 比直接 UNMAP 更接近正确顺序**，但 host 仍要等对应的 DONE/ack，不能把“发了 FLUSH”当成“缓冲已归还”。

**结论（B）**：

- 厂商/主线**没有**在 `close` 里“等所有 WRITE_DONE”的硬性主机等待；它先发 STOP，再 free fragments/UNMAP/CLOSE，并假设 STOP 会让 DSP 停止取数。
- API 契约要求 client 在 DONE 之前不得复用/释放该缓冲；mainline 的 late-DONE CVE 证明这个假设在竞态下会破。**如果 AP 在 DSP 仍持有首块缓冲时 UNMAP/关 LPASS 时钟/恢复数据脚，DSP/codec DMA 可能访问已撤销的映射或已关的时钟。** 公开源没有找到直接的 SoC 挂死/NoC 日志；Qualcomm 平台文档只说明这条路径在硬件上是 fatal，不能证明本案已经发生。
- **与 h/i/j 的边界**：j 在 2 s 超时前就挂死，因此没有走到 `q6apm_dai_close()` 式的失败收尾；B 不能解释 j。H10 仍是“如果 host 活着走到超时”的风险，不是 j 的已证机制。i 通过则说明 STOP/CLOSE/UNMAP 的完整成功路径本身可以走通。

### 11.3 C：PRM34 失败路径的安全收尾顺序（方案，只写不改）

**触发**：首块 WRITE（pre-roll sequence 0）在 `write_commands()` 超时、传输失败，或 **DONE 一直不回但 AP 还活着**。

**先核对当前 worktree 的失败路径（很重要）**：

- 当前 `f385720`/本 worktree 在 `write_commands()` 返回 false 后，`zero_transfer()` 返回 0，amp `stop_fail` 关闭 core，`prepare()` 仍会 assert reset 并恢复 SCK/WS/data pin，`amp_pass=false`。
- 回到 PRM 后，line 473 的 `!o.silent.amp_pass` **短路**了整个条件，line 476 直接 `break`；因此 line 496 的 `pcm_commands(true)`（GRAPH STOP/CLOSE）和 line 476 的 `memory_commands(true)` 都**不会执行**。graph/clock 没有释放，SMMU map 由 `zizhan_audio_dma_map_retain()` 保留。
- 所以当前实际行为不是“失败后立即 STOP/CLOSE/UNMAP/关时钟”，而是“关 amp core、恢复数据脚、保留 graph/clock/map 直到复位”。这比协调者描述的路径更保守。协调者若在内核 `064f32a5` 中改了 line 473/476 或失败收尾，应以该内核的实际代码为准；本节其余部分是设计建议，不是现状描述。

**安全顺序**：

| 步 | 动作 | 条件/原因 |
| --- | --- | --- |
| 1 | 停止发新 WRITE，保留 graph、PRM clock/votes、SMMU map、UEFI 缓冲；amp core 可先关闭/assert reset，但 SCK/WS/data pin 的 mux 恢复应等 graph 停止后 | 不再增加在途缓冲；DSP 仍可能需要这些资源；pin mux 在 DSP 仍可能驱动 I2S 时切换会增加争用 |
| 2 | 发送 `GRAPH_FLUSH` 或 `GRAPH_STOP`（优雅场景可先 `EOS`） | 让 DSP 停止/丢弃/返回在途缓冲；不要先 UNMAP |
| 3 | 有界等待所有在途 `WRITE_DONE`（或 `EOS_RENDERED`/stop ack） | 首块用 **≥2 s** 的预算；DONE 是缓冲被消费的唯一可靠信号 |
| 4 | 确认 outstanding=0 后，`MEM_UNMAP` | DSP 不再持有缓冲，AP 才能撤销映射 |
| 5 | `GRAPH_CLOSE` | 图关闭在 UNMAP 之后；不要先关图再指望 DSP 返回缓冲 |
| 6 | 释放 PRM LPASS clock/votes | 时钟必须在 DSP 不再取数之后关 |
| 7 | 恢复 SCK/WS/data pin mux，assert amp reset | 数据脚在时钟/图停止后恢复 |
| 8 | 退休/恢复 SMMU context，释放 `pong_smmu_lease_lock` | 最后释放共享 SMMU 资源 |

**超时/不确定**：

- 第 3 步超时（DSP 仍不返回 DONE/EOS/stop ack）→ **不要**执行 4–8；保留 map/缓冲/graph/clock/数据脚，标记 `uncertain=1`，要求复位。保留资源是唯一安全选项：DSP 仍可能 DMA。
- 第 4 步 UNMAP 失败/不确定 → 保留 map/lease；不要继续 CLOSE/clock-off。
- 第 6 步 clock release 失败 → 保留 clock，保留 map；不要用“已发 release”冒充时钟已关。

**超时值**：

- 首块 WRITE 不要用 100 ms 作为拆资源的触发器；`wr_sh_mem_ep_api.h` 没有 100 ms 保证，reference 会把 DONE 推迟到 internal buffer 有空/下游消费。正常样本是 11 ms（i），但这只说明通常很快，不能把 100 ms 当成协议上限。**建议首块 `zizhan_write_ms=2000`**，或者用无固定超时 + 看门狗保留资源的方案。
- 稳态后的后续块可以使用更短超时，但失败路径仍必须走“等待/保留”，而不是立即拆资源。
- `zizhan_write_flush=1` 的失败 flush（reason 8）应放在**安全收尾之后/之外**，不要让它持着 SMMU lease/时钟/图去写 4 MiB，避免把次要扰动放大成新的阻塞。
- **如果 AP 在超时前已经挂死（j 的情况），本节的收尾根本不会执行**；此时唯一的安全动作是保留资源并让看门狗复位，不能指望 host 侧“先 FLUSH 再 UNMAP”的代码救场。

**与 h/i/j 的对应**：

| 轮 | 设置 | 实测/判定 |
| --- | --- | --- |
| h | 默认 100 ms；h-1/h-2 两次 | 都挂；klog 没有 send/done/fail。不能证明“100 ms 后收尾”发生，可能是 send 前/未落盘。 |
| i | `zizhan_write_ms=2000` | 通过；首块 `done us=11079`，4 条全完成。H9（100 ms 太短）不被支持。 |
| j | 2000 ms + 每次 send/done flush | `send sequence=0` 后无 done/fail，92 s 挂死。若 send flush 已返回，则是 A（DONE 不回）；若未返回，则是 H12。没有 `sent-flushed` 标记，不能归入 B/H10。 |

**建议的代码改动（只设计，不在本窗口实现）**：

- `ZizhanWriteState` 增加 outstanding 计数和 tri-state（done/timeout/transport）；`write_commands()` 超时返回“未完成”而不是直接进入 `stop_fail`。
- PRM34 失败路径新增 `zizhan_audio_prm_fail_safe()`：先停新写 → FLUSH/STOP/EOS → 有界 drain DONE → 再 `memory_commands(true)`/`pcm_commands(true)`/`dma_map_end`/PRM release；任一步超时则 retain。
- `zizhan_amp_native_pulse_run()` 的失败返回不能直接触发 `pcm_commands(true)`/clock release；把安全收尾集中到 PRM owner。
- 首块 mode34 默认 `zizhan_write_ms=2000`；100 ms 只作为稳态诊断，不作为默认。
- `pong_klog_flush` 改为有界/异步/console-only，移出 SMMU lease 区间；这是 §9.2 的次要卫生项。

### 11.4 一手来源

- AudioReach engine：`https://github.com/AudioReach/audioreach-engine`
  - `fwk/api/modules/wr_sh_mem_ep_api.h:62–77`、`:227–260`、`:307–334`、`:354–394`
  - `fwk/spf/containers/gen_cntr/ext/wr_sh_mem_ep/src/gen_cntr_wr_sh_mem_ep.c:341–452`、`:443–452`、`:769–947`、`:1009–1019`、`:1481–1559`
  - `fwk/spf/apm/core/src/apm_cmd_sequencer.c:202–213`
- mainline：`sound/soc/qcom/qdsp6/q6apm.c:97–121,372–377,477–515,591–610,792–803,828–840`；`q6apm-dai.c:423–445,447–473,570–582,860–878`；`q6apm-lpass-dais.c:185–203,300–305`；`audioreach.c:617–620,1547–1562`
- Qualcomm 平台文档：`https://docs.qualcomm.com/bundle/publicresource/topics/80-88500-4/72_Erroneous_transaction_on_bus__error_and_timeout_.html`（NOC_ERROR/timeout 包含 unclocked bus access，FIQ 到 TZ；XPU violation 包含时钟不可用）
- CVE-2026-72257：`https://app.opencve.io/cve/CVE-2026-72257`（late buffer-done 与 `q6apm_free_fragments` 的 NULL deref）
- `q6apm-lpass-dai: close graph on prepare errors`：上游 commit `be1fae62cf253a5b67526cee9fbc07689b97c125`
- `q6apm-dai: reset queue ptr on trigger stop`：2026-03 patchset `https://patchew.org/linux/20260309065137.949053-1-srinivas.kandagatla@oss.qualcomm.com/`
- `q6apm: Add support for early buffer mapping on DSP`：`https://lkml.iu.edu/2602.0/07700.html`
- 本机 h/i/j 日志（仓库外，相对 `$ZIZHAN_SRC`）：`logs/n18-h-1.klog.txt`、`logs/n18-i-1.klog.txt`、`logs/n18-j-1.klog.txt`、`logs/n18-{h,i,j}-1.cycle.out`、`logs/n18-{h,i,j}-1.supervisor.json`、`logs/n18-{h,i,j}-1.source.diff`

## 12. 根因：内核栈溢出（2026-11-18 02:36 更新）

### 12.1 证据链

**完整 panic**

- `$ZIZHAN_SRC/logs/n18c-p-1.klog.txt`（PRM34 dry，boot args 含 `zizhan_write_ms=2000`、`zizhan_write_probe=1`）保留了 reason 2 的 panic，而不是只有冷咬：
  `panic(cpu 5 caller 0xfffffff00944b230): Invalid kernel stack pointer (probable overflow). at pc 0xfffffff008d5a0d4, lr 0xfffffff009070894`。
- `Panicked thread: ... tid: 200`，任务为 `pid 0: kernel_task`；panic 里还有 `CORE 5 is the one that panicked`。
- 协调者用 `work/m1-firstsound/build-wp/kernel` 的 `atos` 符号化出的调用链是：
  `Call_continuation → (PRM) worker → prepare → zizhan_cs35l41_native_pulse_transfer → silent_run → zero_transfer → write_commands → IOLog → _IOLogv → os_log_with_args → _os_log_with_args_internal → log_queue_log → log_payload_send → __firehose_buffer_tracepoint_reserve → oslogwakeup → selwakeup → knote → knote_post → knote_enqueue → kqueue_threadreq_initiate`。
- 这条链的最后一帧正好是 `kqueue_threadreq_initiate`；也就是说，不是 DSP 返回了坏数据，而是 **AP 在打印“WRITE send/rx/done”这类日志时把内核栈用完了**。

**帧大小（旧 build-wp，`xcrun llvm-objdump -d --disassemble-symbols=...` 读 `sub sp` 序言）**

| 帧 | 大小 | 说明 |
| --- | ---: | --- |
| `worker`（PRM） | `0x1330` = 4912 B | 内联了所有 packet builder |
| `prepare` | `0x1950` = 6480 B | `ZizhanAmpIdentity/Snapshot/SafeOff/...` 等 ~5 KiB 回执在栈上 |
| `silent_run` | `0x470` = 1136 B | amp/TDM/pulse 运行 |
| `zero_transfer` | `0x50` = 80 B | 回调包装 |
| `write_commands` | `0x540` = 1344 B | WRITE V2 packet + `bytes[512]` |
| `IOLog`→`kqueue_threadreq_initiate` | `0x710` = 1808 B | `IOLog 0x20 + os_log_with_args 0x50 + internal 0x320 + log_queue_log 0x140 + log_payload_send 0x70 + firehose 0x90 + oslogwakeup 0x10 + selwakeup 0x20 + knote 0x40 + knote_post 0x60 + knote_enqueue 0x30 + kqueue_threadreq_initiate 0x40` |
| **合计** | **`0x3d90` = 15760 B** | 16 KiB = 16384 B，余量只有 624 B |

624 B 的余量不足以覆盖栈对齐、异常/中断帧和 `os_log` 内部的临时对象；而且这条链只有在 `logd` 正在等日志、`IOLog` 走 `oslogwakeup → selwakeup → kqueue` 时才会出现，所以是**间歇**的。溢出后 panic 自身也可能写不完，于是大多数轮表现为 Gunyah 冷咬；只有 `n18c-p-1` 这次幸运地留下了完整 panic。

**为什么 PRM33 也会挂**

- 协调者报告 `n18c-q-1`（PRM33）也挂过。`prepare`、`silent_run`、`write_commands` 和 `IOLog` 链在 PRM33 同样存在；PRM33 只是没有 100 ms 这个额外差异，不能把根因限定为 PRM34。

### 12.2 为什么之前看起来像 SMMU/ADSP/flush

- A 组的“WRITE send 后没有 DONE”是**日志链在 send 行之后溢出**；`send` 行能落盘，`rx/done` 行没有机会打印，看起来就像 DSP 没有回 DONE。
- B 组的 stage 10 之后挂死，也只是栈溢出发生在 `pulse_run`/第二条 WRITE 的某条 `IOLog` 上；不是第二条 WRITE 的 DSP 语义。
- `pong_klog_flush` 的 4 MiB FUA 写会占用同一线程、持 `gHost.lock`，并且让 `IOLog` 更容易撞上 `logd` 等待；它是**时序放大器**，不是根因。`amp-pulse-1116-v1` 没有任何 checkpoint/klog flush 也挂死，已经排除了“flush 是必要条件”。
- SMMU `FSR=0x400/GFSR=0` 一直干净，是因为根本没有走到 SMMU fault；DSP 侧也没有返回错误，AP 是在自己的日志路径上溢出的。

### 12.3 被哪些轮否定

| 旧假设 | 否定它的证据 | 结论 |
| --- | --- | --- |
| H1：16 KiB 预滚越过 64 KiB | n18-a-1/a-3、i（2000 ms）都用 16 KiB 首块并通过；SMMU 无 fault | 不是根因；几何风险仍可保留为独立边界测试 |
| H2：SMMU fault/terminate 把 SoC 挂住 | 所有有 `dma_watch` 的轮 `fsr=400 gfsr=0`；panic 是内核栈 | 不是本次根因；SMMU 编程差异仍是独立风险 |
| H8：checkpoint 同步 4 MiB flush 不返回 | `flushed=` 在 flush 完成后才打印；1116-v1 无 checkpoint 也挂；panic 栈链不含 `pong_klog_flush` | 撤回；flush 只是放大时序 |
| 100 ms WRITE_DONE 超时过短 | i（2000 ms）通过、4 条 `done≈11 ms`；j（2000 ms + flush）仍挂；panic 发生在 `write_commands` 的 `IOLog` | 不是根因；100 ms 只是 code smell |
| H13：首块 WRITE 路径本身挂住 SoC | `n18c-p-1` 的 panic 直接落在 `IOLog`→kqueue；同一路径的 `write_commands` 在 i 中正常完成 | H13 是症状描述，已被 §12 的栈溢出取代 |

### 12.4 修复与当前状态

- `8d18d71`（`fix(audio): keep the amp/WRITE path inside the 16 KiB kernel stack`）：
  - `ZizhanSe15Prepare.cpp` 的 amp 回执改为 `static struct Receipts`，每次进入 `memset` 清零；
  - `ZizhanAudioPrm.cpp` 的 `tdm_commands`/`pcm_commands`/`memory_commands`/`batch_round`/`runtime_round`/`graph_round`/`dma_observe` 加 `noinline`，把 packet builder 移出 worker 的永久帧；
  - build-fix 读回：`prepare 0x360`、PRM worker `0x540`、`write_commands 0x4e0`、`silent_run 0x470`、`zero_transfer 0x50`、IOLog 链 `0x710`，合计 **0x1950 = 6480 B**。
- `1b535fd`：默认关闭的 WRITE 延迟/slow probe 与 ADSP fatal klog；本 panic 轮的 `zizhan_write_probe=1` 来自这里。
- `a259237`：porting 记录。
- 修复内核 `work/m1-firstsound/build-fix/kernel` 的 sha256 是 `b698195262435c57427defdce8f7f9f9a61e92e26f1ba6db169e53704188a603`；协调者在 02:33 后启动 r（PRM34 dry）、s（PRM33）、t（PRM34 dry+checkpoint）静音验证。
  - `n18d-r-1`（PRM34 dry，`zizhan_write_ms=2000`、`zizhan_write_probe=1`）：**PASS**；4 条 WRITE 全部 `send→rx→done`，`done us=11070/11859/11081/10221`，最后 `DMAMAP installed=0`、`PRM PASS`。
  - `n18d-s-1`（PRM33）：**PASS**；2 条 WRITE 全部 `done us=11373/10616`，最后 `DMAMAP installed=0`、`PRM PASS`。
  - `n18d-t-1`（PRM34 dry+checkpoint，`zizhan_audio_checkpoint=1`）：**PASS**；4 条 WRITE 全部 `send→rx→done`，`done us=11060/12271/10057/11056`；`stage=9/10` 的 `flushed=0` 都出现（`us≈14–21 ms`），最后 `DMAMAP installed=0`、`PRM PASS`。checkpoint 观察者不再是变量。

### 12.5 残余风险

- 修复只压缩了 audio/PRM 这条链；`nativeAdrenoMailboxPoller` 仍有 3696 B 的单帧，和 `runCore→dispatch→submit→retireCompleted→release` 链合计约 6688 B；若错误路径再进 `IOLog`，约 8496 B。详见 `docs/research/2026-11-18-kernel-stack-audit.md`。
- 这份根因文档的证据来自 `n18c-p-1` 的 panic、r/s/t 的 fix 轮和 build-wp/build-fix 的只读反汇编；没有手机操作、没有构建。当前状态：**栈帧 HOST_VERIFIED，r/s/t DEVICE_VERIFIED（PRM34 dry、PRM33、PRM34 dry+checkpoint）**。

## 13. 参考

### 13.1 本机 live DT（仓库外，相对 `$ZIZHAN_SRC`）

- `work/hardware-20261115/audio-dma-live-dt-v1.dts:29241–29255`：`msm-audio-ion`（SID `0x1801`、sid-mask `<0 0xf>`、pool `0x10000000+0x10000000`）。
- `work/hardware-20261115/audio-dma-live-dt-v1.dts:18960–18978`：apps SMMU（`qcom,context-fault-retry`、`num-context-banks-override`、`num-smr-override`、`handoff-smrs`、全局/上下文 IRQ）。
- `work/hardware-20261115/audio-dma-live-dt-v1.dts:19034–19041`：`lpass_tbu`（`0x1800` + `0x400`）。
- `work/hardware-20261115/audio-dma-live-dt-v1.dts:12500–12528`：fastrpc SID `0x1803/0x1804/0x1805` 与 `"stall-disable","HUPCF"`。
- `work/hardware-20261115/audio-dma-live-dt-v1.dts:11298–11321`：`qcom,gpr`、`audio-pkt`、`q6prm`。

### 13.2 厂商 audio-kernel（公开 SM8475/taro-S 镜像）

- `https://github.com/ASUS-SM8475/vendor_qcom_opensource_audio-kernel/blob/taro-S/dsp/msm_audio_ion.c`
  - `#L36`：`MSM_AUDIO_SMMU_SID_OFFSET 32`
  - `#L263–L280`：`msm_audio_ion_get_phys()`，`*addr |= smmu_sid_bits`
  - `#L830–L913`：probe，`sid = args[0] & sid_mask`、`sid << 32`
  - `#L574–L585`、`#L680–L750`：`hyp_assign_phys` 与 `IOCTL_MAP_HYP_ASSIGN`

### 13.3 主线 Linux

- `https://github.com/torvalds/linux/blob/master/sound/soc/qcom/qdsp6/q6apm-dai.c`
  - `#L42`：`SID_MASK_DEFAULT 0xF`
  - `#L548–L551`、`#L670–L673`：`dma_buffer.addr | (sid << 32)`
  - `#L1203–L1226`：`iommus` 与 modem/mDSP SCM 路径互斥
- `https://github.com/torvalds/linux/blob/master/sound/soc/qcom/qdsp6/q6apm.c`
  - `#L210–L250`：`MEM_MAP` 包
  - `#L480–L500`：`WRITE V2` 包与 token
  - `#L576–L610`：`WRITE_DONE` 的 token 低 16 位索引
- `https://github.com/torvalds/linux/blob/master/sound/soc/qcom/qdsp6/audioreach.h`
  - `#L121–L135`：`apm_cmd_shared_mem_map_regions` / region payload
  - `#L244–L260`：`apm_data_cmd_wr_sh_mem_ep_data_buffer_v2`
  - `#L413–L420`：`apm_cmd_header`
- `https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sm8450.dtsi`
  - `#L2802–L2812`：`q6apmdai` 的 `iommus = <&apps_smmu 0x1801 0x0>`
- `https://github.com/torvalds/linux/blob/master/drivers/iommu/arm/arm-smmu/arm-smmu.c`
  - `#L1148–L1155`：SCTLR/CFRE/CFCFG/HUPCF/ASIDPNE 默认
  - `#L910–L945`：Waipio `context-fault-retry`
  - `#L979–L1015`：fault handler 清 FSR、RESUME/TERMINATE
  - `#L3332–L3380`：reset、sGFSR、USFCFG
- `https://github.com/torvalds/linux/blob/master/drivers/iommu/arm/arm-smmu/arm-smmu-qcom.c`
  - `#L217–L266`：最后一个 S2CR 的 bypass quirk 测试
  - `#L760–L850`：QSMMUv500 TBU halt/resume
  - `#L2086–L2156`：DT `qcom,actlr` 到 `CB_ACTLR`
- `https://github.com/torvalds/linux/blob/master/drivers/iommu/arm/arm-smmu/arm-smmu.h`
  - `#L151–L170`：SCTLR 位
  - `#L215–L240`：FSR 位
  - `#L104–L126`：SMR/S2CR/CBAR/CBA2R
- `https://github.com/torvalds/linux/blob/master/drivers/iommu/io-pgtable-arm.c`
  - `#L1012–L1078`：coherent walk 的 TCR/MAIR

### 13.4 2026-08 stage-2/SCM 澄清

- `https://lkml.rescloud.iu.edu/2608.3/03890.html`：`qcom,q6apm-dai: add stage-2 (SCM/VMID) access model`。要点：ADSP 是 stage-1 + `iommus`；只有 modem/mDSP 的 stage-2 保护上下文才用 `qcom,vmids` + SCM assign。

### 13.5 本仓库代码

- `drivers/PongPower/ZizhanAudioDmaMap.cpp:107–178`：watch、extent、identity range、config、install、end、address。
- `drivers/PongAdreno/ArmSmmu4K/ArmSmmu4KContext.c:50–70`：CBAR/CBA2R/TCR/TCR2/TTBR/MAIR/SCTLR 编码。
- `drivers/PongAdreno/ArmSmmu4K/ArmSmmu4KProgram.c:26–60`、`:86–141`：TLB sync、disabled context、fault 检查。
- `drivers/PongPower/ZizhanAudioWrite.h:20–46`：WRITE V2 body 与 token。
- `drivers/PongPower/ZizhanAudioMemory.h:17–38`：MEM_MAP/UNMAP 与 token。
- `drivers/PongPower/ZizhanAudioPrm.cpp:334–371`：`write_commands`、`zero_transfer`、pre-roll。
- `drivers/PongPower/ZizhanAudioPrm.cpp:441–480`：mode 34 的 map/transfer/restore 顺序。
- `drivers/PongPower/ZizhanCs35l41.c:1194–1211`：stage 8/9/10/11/12 的实际位置。
- `drivers/PongPower/ZizhanIpcc.cpp:47–56`、`:115–140`：IPCC IRQ/drain/subscribe。
- `drivers/PongPower/ZizhanIpccRouter.h:40–56`：clear-before-dispatch、32 条预算。
- `drivers/PongPower/PongAdsp.cpp:120–145`、`:155–165`、`:450–466`、`:705–717`：IRQ、wait、attach、send。
- `drivers/PongPower/QcomGlink.c:421–455`：GLINK send 的 non-blocking 重试。
- `drivers/PongPower/ZizhanAdspWait.h:10–18`：arm-then-take。
- `drivers/PongPower/ZizhanAudioBatch.c:35–43`：batch slot 地址/sequence。
- `$ZIZHAN_SRC/uefi-pong/Platforms/Nothing/DarwinBootPkg/Driver/DarwinBoot.c:746–755`、`:792–794`：64 KiB `EfiReservedMemoryType` 分配与 chosen 填写（仓库外）。

### 13.6 既往记录

- `docs/progress/STATUS.md`：M1 行，5/7 挂死、2 轮跑通、下一步。
- `docs/progress/2026-11-15-audio-silent-owner.md`：mode25/26/27 的 SMMU 实际 ID、slot/bank、原 SCTLR、ASID 守卫、CBAR restore panic。
- `docs/progress/2026-10-31-frame-timeline.md`：g149 `sCR0=0x200406`、g151 CB3 `SCTLR=0xe0`/`FSR=0x400`。
- `docs/progress/2026-11-15-ipcc-routing.md`：route1 8 条统计（441 IRQ/95 s、unknown/budget=0）。
- `$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`：11-17 22:30 以后的 7 轮表、checkpoint、SMMU 恢复 panic。
- `$ZIZHAN_SRC/work/hardware-20261115/amp-dmaobs-v1-device-review.json`：`ID0=0x4c017e71`、`ID1=0x6000004b`、`ID2=0x5111`、slot112/bank73、原 SMR/S2CR。
- `$ZIZHAN_SRC/work/hardware-20261115/amp-dmalease-v2-device-review.json`：`fault=400` 与 `masked_fault=0`。
