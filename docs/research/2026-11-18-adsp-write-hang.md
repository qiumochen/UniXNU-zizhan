# ADSP 共享内存 WRITE 间歇挂死调研

日期：2026-11-18（UTC+8）。源码基线：`f385720`；写作前 worktree 干净，本文是本次唯一新增的仓库文件。本文只做只读源码、DT 与公开内核对照，不包含手机操作、不运行 `adb`/`fastboot`/任何 cycle 或安装脚本，也不构建内核或镜像。

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
| amp-pulse-1117-klog | 16 KiB | 无 | 无 checkpoint，最后 DATA_PIN | 第一条 WRITE 附近 | 挂 |
| amp-pulse-1117-ckpt | 16 KiB | 无 | stage 9 | 第一条 WRITE 内 | 挂 |
| amp-pulse-1117-pre0 | 0 | 无 | 全通过 | — | 跑通，-36 dBFS，无声 |
| amp-pulse-1117-loud | 0 | `tone_x=8` | 全通过 | — | 跑通，-18 dBFS，无声 |
| amp-pulse-1117-rep | 0 | `tone_repeat=10` | stage 10 | 第一条 WRITE 返回后、amp `pulse_run` 内 | 挂 |
| amp-pulse-1117-hold | 0 | `pulse_hold=300` | stage 9 | 第一条 WRITE 内 | 挂 |

因此：

- **16 KiB 预滚的 3/3 轮都挂**，且都停在第一条 zero WRITE 附近；
- **0 预滚、无额外 hold/repeat 的 2/2 轮跑通**（其中 `loud` 另加了 `tone_x=8` 增益，严格说不是完全相同的输入）；
- **0 预滚、加 hold/repeat 的 2/2 轮也挂**，其中 `rep` 明确停在第一条 WRITE **返回之后**、amp `pulse_run` 之前；`hold` 停在第一条 WRITE 内。

这 7 轮把“偏移”“amp 保持/重复”“amp pulse 路径”混在一起，不能据此宣布“与偏移无关”。当前最具体、最可检验的触发**假设**是：**16 KiB 预滚让 ADSP 的首写/首读越过 64 KiB MEM_MAP 边界**；最具体的放大**假设**是：**SMMU 故障之后没有任何 owner 清 FSR/GFSR、恢复或管理 TBU**。这两条可以同时成立：前者制造故障，后者把故障变成整机挂死。[推断]

**建议的下一个最小实验**（不在本次窗口执行）：只加只读诊断，做一次 **0 与 16 KiB 的单条 zero WRITE 对照**，功放 dry、不开输出、不保持长音，`zizhan_dma_watch=1`，映射保持 2 s 后再 unmask；读 klog 里的 FSR/FAR/FSYNR、GFSR/GFSYNR、`sCR0`、`CB_ACTLR` 和 LPASS TBU 状态。具体判定表见 §7。

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
| `TCR` | `0x80000000 | 0x3500 | 32` | TG1=4K，SH0=inner，ORGN0/IRGN0=WBWA，T0SZ=32 |
| `TCR2` | `0x38071` | PASIZE=36，SEP=7，AS=1（16-bit ASID），TBI0/TBI1=1 |
| `TTBR0` | `0x1801 << 48 | root_pa` | ASID `0x1801`，root 在 36-bit PA |
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
- **stage 10 挂** = 第一条 WRITE 已经返回，且 `run_state` 已经返回（stage 10 日志在这之后），挂点在 amp 的 `zizhan_pulse_run()`（或它之后的 checkpoint 11），**不是 ADSP WRITE**。

`rep` 轮停在 stage 10，是“WRITE 返回后、amp pulse 内”；`hold`/`ckpt` 停在 stage 9，是“第一条 WRITE 内”。这两类不应该合并成同一个“ADSP 处理 WRITE 挂死”。[事实]

### 4.2 16 KiB 与 0 的混淆

mode 34 的 `zero_transfer`（`ZizhanAudioPrm.cpp:360–371`）顺序是 `{2,0,3,1}`：

- chip 2/3：`silence=true`，地址 = `base + preroll`，数据是 zero；
- chip 0/1：`silence=false`，地址 = `base`，数据是 tone。

所以 mode 34 的 zero 预滚**不是** 4 个连续 slot；它只是把同一个 `base+preroll` 地址写两次（chip 2 和 chip 3），tone 写 `base` 两次。`ZizhanAudioBatch` 的 `stride=0x3000`、4 个 slot 是 mode 36/37 的 batch 路径（`bytes=2880`），不是 11-17 mode 34 的预滚。[事实]

在 `preroll=16384`、`bytes=11520` 时，第一条 zero WRITE 覆盖 `[base+16384, base+27904)`；它本身没有到 64 KiB 末尾。**但如果 ADSP 在第一次 WRITE 时按 `MEM_MAP` 的整个 64 KiB region 做初始化/预取，而不是只读 `buf_size`，那么 `base+16384 + 65536 = base+81920`，越过 `base+65536` 的映射末尾 16 KiB。** 0 预滚时，`base+0 + 65536 = base+65536`，恰好落在映射末尾，不越界。这个几何关系与“16 KiB 3/3 挂、0 预滚无额外变量 2/2 过”吻合。[推断，需单条 16 KiB WRITE 验证]

反之，0 预滚 + hold/repeat 的 2/2 挂不能用这个几何解释：第一条 zero WRITE 在 `base+0`，整区预取也恰好不越界。它们更可能是 **amp/TDM/pulse 路径** 或 **DSP 写时序** 的独立问题；`rep` 的 stage 10 已经明确在 WRITE 返回之后。`hold` 的全局 `zizhan_amp_pulse_hold_us` 在 boot arg 解析时已经设置，但 `pulse_ops` 要到 stage 10 之后才构造；它是否在 stage 9 之前改变了 amp/TDM 状态，目前未知，不能把 `hold` 轮的 stage 9 直接归因于 hold 参数。[事实 + 未知]

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

- `rep` 轮停在 stage 10：第一条 WRITE 已返回，`zizhan_pulse_run()`（CS35L41 内部 DSP）还没返回（`ZizhanCs35l41.c:1207–1211`）。
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
| D | 0 | 4 条 | hold/repeat | H4：复现 stage 10 amp 路径 |
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
| 无任何 fault，C/D 仍挂 | SMMU 不是直接触发 | 转 H3/H4；amp/TDM/协议 |
| E 与 A 相同 | IPCC 路由不是触发 | 排除 H7 |
| E 只在 route1 挂 | IPCC/GLINK 路径相关 | 查 GIC trigger、32 条预算、共享 receiver |

### 7.4 边界

- 这次只写文档；不实现、不构建、不碰手机。
- 下一次实验需要新的内核/包、设备窗口和返回路径；不能在本次无人值守窗口执行。
- 第一次失败保留现场，不重复同一轮；每次只变一个变量。
- 任何 `PASS` 必须是“真实启动返回 + 新鲜 klog + 预期日志”，不能把“已派发/已入队”当 PASS。

## 8. 未决问题

1. ADSP 在第一次 WRITE 时到底读 `buf_size` 还是整个 `MEM_MAP` region？没有 DSP 固件源码/总线 trace，只有几何相关性。
2. 16 KiB 3/3 挂是否真的是“越界 fault”，还是 DSP 对非零预滚地址的另一种行为？需要 A/B 单条实验。
3. `rep` 的 stage 10 挂是否完全在 amp `pulse_run`，还是 `run_state` 的 I2C 访问？需要更细的 amp 只读打点。
4. `hold` 的 stage 9 挂为什么在第一条 WRITE 内？hold 参数在 transfer 前是否改变了 amp/TDM 状态？需要 dry/nopup 对照。
5. `CB_ACTLR[73]` 的实际值是多少？是否继承了固件旧值？只读一次即可回答。
6. `sCR0` 的 `GFIE/USFCFG` 在当前 boot 是否仍为 `0x200406`？只读一次即可回答。
7. `lpass_tbu` 是否在挂死前进入 halt/ECATS？只读 base+0x0/+0x20 可回答。
8. ASID `0x1801` 在 16-bit/8-bit 硬件上的实际截断行为？读 TTBR/ID2 可回答；当前 `active_banks=0` 只是映射时刻的状态。

## 9. 参考

### 9.1 本机 live DT（仓库外，相对 `$ZIZHAN_SRC`）

- `work/hardware-20261115/audio-dma-live-dt-v1.dts:29241–29255`：`msm-audio-ion`（SID `0x1801`、sid-mask `<0 0xf>`、pool `0x10000000+0x10000000`）。
- `work/hardware-20261115/audio-dma-live-dt-v1.dts:18960–18978`：apps SMMU（`qcom,context-fault-retry`、`num-context-banks-override`、`num-smr-override`、`handoff-smrs`、全局/上下文 IRQ）。
- `work/hardware-20261115/audio-dma-live-dt-v1.dts:19034–19041`：`lpass_tbu`（`0x1800` + `0x400`）。
- `work/hardware-20261115/audio-dma-live-dt-v1.dts:12500–12528`：fastrpc SID `0x1803/0x1804/0x1805` 与 `"stall-disable","HUPCF"`。
- `work/hardware-20261115/audio-dma-live-dt-v1.dts:11298–11321`：`qcom,gpr`、`audio-pkt`、`q6prm`。

### 9.2 厂商 audio-kernel（公开 SM8475/taro-S 镜像）

- `https://github.com/ASUS-SM8475/vendor_qcom_opensource_audio-kernel/blob/taro-S/dsp/msm_audio_ion.c`
  - `#L36`：`MSM_AUDIO_SMMU_SID_OFFSET 32`
  - `#L263–L280`：`msm_audio_ion_get_phys()`，`*addr |= smmu_sid_bits`
  - `#L830–L913`：probe，`sid = args[0] & sid_mask`、`sid << 32`
  - `#L574–L585`、`#L680–L750`：`hyp_assign_phys` 与 `IOCTL_MAP_HYP_ASSIGN`

### 9.3 主线 Linux

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

### 9.4 2026-08 stage-2/SCM 澄清

- `https://lkml.rescloud.iu.edu/2608.3/03890.html`：`qcom,q6apm-dai: add stage-2 (SCM/VMID) access model`。要点：ADSP 是 stage-1 + `iommus`；只有 modem/mDSP 的 stage-2 保护上下文才用 `qcom,vmids` + SCM assign。

### 9.5 本仓库代码

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

### 9.6 既往记录

- `docs/progress/STATUS.md`：M1 行，5/7 挂死、2 轮跑通、下一步。
- `docs/progress/2026-11-15-audio-silent-owner.md`：mode25/26/27 的 SMMU 实际 ID、slot/bank、原 SCTLR、ASID 守卫、CBAR restore panic。
- `docs/progress/2026-10-31-frame-timeline.md`：g149 `sCR0=0x200406`、g151 CB3 `SCTLR=0xe0`/`FSR=0x400`。
- `docs/progress/2026-11-15-ipcc-routing.md`：route1 8 条统计（441 IRQ/95 s、unknown/budget=0）。
- `$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`：11-17 22:30 以后的 7 轮表、checkpoint、SMMU 恢复 panic。
- `$ZIZHAN_SRC/work/hardware-20261115/amp-dmaobs-v1-device-review.json`：`ID0=0x4c017e71`、`ID1=0x6000004b`、`ID2=0x5111`、slot112/bank73、原 SMR/S2CR。
- `$ZIZHAN_SRC/work/hardware-20261115/amp-dmalease-v2-device-review.json`：`fault=400` 与 `masked_fault=0`。
