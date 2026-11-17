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
| amp-pulse-1117-klog | 16 KiB | 无 | 无 checkpoint，最后 DATA_PIN | DATA_PIN 后、第一条 WRITE 前；可能是周期 klog flush | 挂 |
| amp-pulse-1117-ckpt | 16 KiB | 无 | stage 9 | stage 9 checkpoint flush（WRITE 未发） | 挂 |
| amp-pulse-1117-pre0 | 0 | 无 | 全通过 | — | 跑通，-36 dBFS，无声 |
| amp-pulse-1117-loud | 0 | `tone_x=8` | 全通过 | — | 跑通，-18 dBFS，无声 |
| amp-pulse-1117-rep | 0 | `tone_repeat=10` | stage 10 | 第一条 WRITE 返回后、stage 10 checkpoint flush | 挂 |
| amp-pulse-1117-hold | 0 | `pulse_hold=300` | stage 9 | stage 9 checkpoint flush（WRITE 未发） | 挂 |

> 上表的“推断挂点”已按 2026-11-18 n18 夜间轮修正：stage 行先于 checkpoint 的 `pong_klog_flush(6)`；缺少 `stage=N flushed=` 时，挂点在 flush，不在 WRITE 或 `pulse_run`。详见 §9.1。

因此：

- **16 KiB 预滚的 3/3 轮都挂（11-17 小样本；n18 a-1/a-3 同为 16 KiB 且 4 写通过，见 §9.6）**，且当时都停在第一条 zero WRITE 附近；
- **0 预滚、无额外 hold/repeat 的 2/2 轮跑通**（其中 `loud` 另加了 `tone_x=8` 增益，严格说不是完全相同的输入）；
- **0 预滚、加 hold/repeat 的 2/2 轮也挂**，其中 `rep` 停在第一条 WRITE **返回之后**的 stage 10 checkpoint flush；`hold` 停在 stage 9 checkpoint flush、第一条 WRITE 未发。

这 7 轮把“偏移”“amp 保持/重复”“amp pulse 路径”混在一起，不能据此宣布“与偏移无关”。当时最具体、最可检验的触发**假设**是：**16 KiB 预滚让 ADSP 的首写/首读越过 64 KiB MEM_MAP 边界**；最具体的放大**假设**是：**SMMU 故障之后没有任何 owner 清 FSR/GFSR、恢复或管理 TBU**。这两条可以同时成立：前者制造故障，后者把故障变成整机挂死。[11-17 推断；已被 §9 夜间数据否定/削弱]

**建议的下一个最小实验**：先按 §9.7 去掉 checkpoint 的同步 4 MiB `pong_klog_flush(6)`（或改成 console-only/≤64 KiB、非阻塞 worker），用同一 a 臂（mode34、dry、16K、route1）重跑；若不再挂，H8 得到直接验证。只有在“无 checkpoint 的真实 WRITE 仍挂”时，才回到 §7 的 **0 与 16 KiB 单条 zero WRITE + SMMU/GFSR/TBU 只读诊断**。

> **2026-11-18 夜间实测修正**：n18 的 checkpointed 挂死全部停在 `ZIZHAN_AUDIO_CHECKPOINT: stage=N` 之后、对应的 `stage=N flushed=` 行之前；stage 9 的挂死没有 WRITE send，stage 10 的挂死已经 send+rx。直接证据指向 **`pong_klog_flush(6)` 的同步 4 MiB UFS 写**，而不是 ADSP WRITE 或 SMMU fault。详见 §9。前文 §6 的 H1/H2/H4 因果性不再成立，保留为历史假设；不要把本节之前的“16 KiB 边界/terminate/amp pulse”当作已证根因。

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
- **stage 10 挂** = 第一条 WRITE 已经返回，且 `run_state` 已经返回（stage 10 日志在这之后）。**但 stage 10 行之后还有一次 checkpoint flush；只有 `stage=10 flushed=` 出现后才进入 amp 的 `zizhan_pulse_run()`。** 缺少 flushed 行时，挂点在 checkpoint flush，不在 `pulse_run`。

`rep` 轮停在 stage 10，是“WRITE 返回后、stage 10 checkpoint flush”；`hold`/`ckpt` 停在 stage 9，是“stage 9 checkpoint flush、第一条 WRITE 未发”。这两类不应该合并成同一个“ADSP 处理 WRITE 挂死”。[事实]

> **2026-11-18 修正**：`rep` 的 stage 10 **没有 `flushed=` 行**，所以它停在 stage 10 checkpoint flush，不是 amp `pulse_run`；`hold`/`ckpt` 的 stage 9 也没有 flushed 行，第一条 WRITE 从未发出。逐条以 §9.1 的表为准。

### 4.2 16 KiB 与 0 的混淆

mode 34 的 `zero_transfer`（`ZizhanAudioPrm.cpp:360–371`）顺序是 `{2,0,3,1}`：

- chip 2/3：`silence=true`，地址 = `base + preroll`，数据是 zero；
- chip 0/1：`silence=false`，地址 = `base`，数据是 tone。

所以 mode 34 的 zero 预滚**不是** 4 个连续 slot；它只是把同一个 `base+preroll` 地址写两次（chip 2 和 chip 3），tone 写 `base` 两次。`ZizhanAudioBatch` 的 `stride=0x3000`、4 个 slot 是 mode 36/37 的 batch 路径（`bytes=2880`），不是 11-17 mode 34 的预滚。[事实]

在 `preroll=16384`、`bytes=11520` 时，第一条 zero WRITE 覆盖 `[base+16384, base+27904)`；它本身没有到 64 KiB 末尾。**但如果 ADSP 在第一次 WRITE 时按 `MEM_MAP` 的整个 64 KiB region 做初始化/预取，而不是只读 `buf_size`，那么 `base+16384 + 65536 = base+81920`，越过 `base+65536` 的映射末尾 16 KiB。** 0 预滚时，`base+0 + 65536 = base+65536`，恰好落在映射末尾，不越界。这个几何关系与“16 KiB 3/3 挂、0 预滚无额外变量 2/2 过”吻合。[推断，需单条 16 KiB WRITE 验证]

反之，0 预滚 + hold/repeat 的 2/2 挂不能用这个几何解释：第一条 zero WRITE 在 `base+0`，整区预取也恰好不越界。`rep` 的 stage 10 在 WRITE 返回之后，但它的 `stage=10 flushed=` 行缺失，实际挂点是 **stage 10 checkpoint flush**，不是 `pulse_run`；`hold`/`ckpt` 的 stage 9 也没有 flushed 行，WRITE 未发。它们不是“amp/TDM/pulse 已经运行”的证据；amp/TDM 仍可能是“无声”的独立问题。`hold` 的全局 `zizhan_amp_pulse_hold_us` 在 boot arg 解析时已经设置，但 `pulse_ops` 要到 stage 10 之后才构造；它是否在 stage 9 之前改变了 amp/TDM 状态，目前未知。[事实 + 未知]

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

- `rep` 轮停在 stage 10：第一条 WRITE 已返回，但 `stage=10 flushed=` 行缺失；挂点在 stage 10 checkpoint flush，**不是** `zizhan_pulse_run()`（`ZizhanCs35l41.c:1207–1211` 的 pulse 在这行 flush 之后才执行）。
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

> **2026-11-18 调整**：先做 §9.7 的“去掉 checkpoint 同步 4 MiB flush”对照；本节的 SMMU 0/16 KiB A/B 只在无 checkpoint 的真实 WRITE 仍挂时使用。当前证据显示 checkpointed 轮根本没有发出 WRITE，或第一条 WRITE 已成功。

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
| D | 0 | 4 条 | hold/repeat + checkpoint | H8：复现 stage 10 checkpoint flush；若要去掉观察者效应，D 组应关闭 checkpoint |
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
| 无任何 fault，C/D 仍挂 | 若 C/D 带 checkpoint，先按 H8 处理；SMMU 不是直接触发 | 关闭 checkpoint 后重跑；再转 H3/H4（token/amp/TDM） |
| E 与 A 相同 | IPCC 路由不是触发 | 排除 H7 |
| E 只在 route1 挂 | IPCC/GLINK 路径相关 | 查 GIC trigger、32 条预算、共享 receiver |

### 7.4 边界

- 这次只写文档；不实现、不构建、不碰手机。
- 下一次实验需要新的内核/包、设备窗口和返回路径；不能在本次无人值守窗口执行。
- 第一次失败保留现场，不重复同一轮；每次只变一个变量。
- 任何 `PASS` 必须是“真实启动返回 + 新鲜 klog + 预期日志”，不能把“已派发/已入队”当 PASS。

## 8. 未决问题

> **2026-11-18 n18 后**：checkpointed 挂死已定位到 §9 的同步 klog flush；以下 SMMU/16 KiB/amp 问题只在“去掉 checkpoint 的真实 WRITE 仍挂”时优先，不要再用 checkpointed 轮回答它们。

1. ADSP 在第一次 WRITE 时到底读 `buf_size` 还是整个 `MEM_MAP` region？没有 DSP 固件源码/总线 trace，只有几何相关性。
2. 16 KiB 3/3 挂是否真的是“越界 fault”，还是 DSP 对非零预滚地址的另一种行为？需要 A/B 单条实验。
3. `rep` 的 stage 10 挂是否完全在 checkpoint flush 的 UFS 写，还是 `gHost.lock` 等待？需要把 checkpoint 的 `flushed=` 与 UFS 命令日志对齐。
4. `hold` 的 stage 9 挂是否只是 checkpoint flush？去掉 checkpoint 后若仍在 stage 9，才需要查 hold 参数/amp-TDM 状态；现有 n18 数据不能回答。
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

所有 checkpointed 轮的关键是 **`stage=N flushed=` 行是否出现**。`amp_checkpoint()`（`ZizhanSe15Prepare.cpp:61–68`）先打 `stage=N`，再同步调用 `pong_klog_flush(6)`，最后才打 `stage=N flushed=...`。实际日志：

| 轮 | stage 9 | stage 9 flushed | WRITE send | WRITE rx | stage 10 | stage 10 flushed | 结论 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| n18-a-1 | 3026 | 3027 | 3028 | 3031 | 3033 | 3037 | 4 writes 全过 |
| n18-a-2 | 2610 | **无** | **无** | 无 | 未到 | — | 挂在 stage 9 flush，WRITE 未发生 |
| n18-a-3 | 2735 | 2736 | 2737 | 2739 | 2740 | 2742 | 4 writes 全过 |
| n18-b-1 | 2660 | 2665 | 2666 | 2668 | 2670 | **无** | 第一条 WRITE 已成功，挂在 stage 10 flush |
| n18-b-2 | 2795 | **无** | **无** | 无 | 未到 | — | 挂在 stage 9 flush，WRITE 未发生 |
| n18-c-1 | 3004 | 3037* | 3038 | 3042 | 3044 | **无** | 第一条 WRITE 已成功，挂在 stage 10 flush |
| n18-c-2 | 2617 | 2651 | 2652 | 2653 | 2654 | 2657 | 4 writes 全过 |
| amp-pulse-1117-ckpt | 2832 | **无** | **无** | 无 | 未到 | — | 挂在 stage 9 flush，WRITE 未发生 |
| amp-pulse-1117-hold | 2735 | **无** | **无** | 无 | 未到 | — | 挂在 stage 9 flush，WRITE 未发生 |
| amp-pulse-1117-rep | 2544 | 2548 | 2549 | 2550 | 2551 | **无** | 第一条 WRITE 已成功，挂在 stage 10 flush |
| amp-pulse-1117-pre0 | 2987 | 2990 | 2991 | 2992 | 2993 | 2994 | 4 writes 全过 |
| amp-pulse-1117-loud | 2490 | 2494 | 2495 | 2496 | 2497 | 2500 | 4 writes 全过 |

`*` c-1 的 `stage=9 flushed=0 us=34240` 与控制台里的一条 launchd 行粘在同一行，但内容存在；stage 10 的 flushed 行不存在。

**结论（事实）**：

- **stage 9 挂死**（a-2、b-2、ckpt、hold）：stage 9 行之后没有 `flushed=`，**没有 WRITE send**。不是“ADSP 处理 WRITE 挂死”；第一条 WRITE 根本没有发出。
- **stage 10 挂死**（b-1、c-1、rep）：stage 9 flush 完成，第一条 WRITE 已 send+rx，stage 10 行之后没有 `flushed=`。挂点在 **stage 10 的 checkpoint flush**，不是 `zizhan_pulse_run()`；`pulse_run` 在这行 flush 之后才执行。
- **通过轮**（a-1、a-3、c-2、pre0、loud）：stage 9/10 都有 `flushed=0`，且 4 条 WRITE 全部 send+rx。**同一个 token、同一个 SMMU context、同一个 16 KiB/0 地址都能完成 4 写**。
- **所有有 `dma_watch` 的 n18 轮**（d-2 无 watch/klog，已排除）的 `ZIZHAN_AUDIO_DMAWATCH` 都是 `fsr=400 gfsr=0`，没有变化、没有 fault；通过轮最后有 `DMAWATCH: end polls=... fsr=400 gfsr=0`，挂死轮没有 end 行（map 未释放）。SMMU 侧没有 fault 的迹象。

这不是“send 日志丢失”的推断：`AMP_CHECKPOINT(chip,9)` 在 `transfer->once()` **之前**，`AMP_CHECKPOINT(chip,10)` 在 `zizhan_pulse_run()` **之前**；flush 不返回，后面的 WRITE/pulse 就不会执行。因此 stage 9 的 WRITE 未发、stage 10 的 `pulse_run` 未跑，是代码顺序 + 缺失 flushed 行共同证明的。[事实]

### 9.2 为什么是 `pong_klog_flush`，不是 ADSP/SMMU

`amp_checkpoint()` 的同步调用链：

```
amp_checkpoint(stage)
  IOLog("ZIZHAN_AUDIO_CHECKPOINT: stage=N")
  pong_klog_flush(6)          // 同步，阻塞在调用线程
  IOLog("... stage=N flushed=...")
```

`pong_klog_flush()`（`drivers/NativeUfs/PongUfsTransport.cpp:327–342`）：

- `IOLockLock(gHost.lock)`（非 panic 路径）
- 从 ramoops `0x85200000` 拷贝 **4 MiB**（`kKlogBytes=0x400000`，`kKlogData4k=1024`）到 UFS 的 pinned klog 区
- 以 `kDataMax=131072`（128 KiB）为一块，调用 `hostWrite4kLocked()` 做 **WRITE(10) + FUA**
- 最终写 1 个 header block；`PONG KLOG` 启动行明确写着 `1025 blocks`（1 header + 1024 data）
- `hostWrite4kLocked()` 每块给 1 s 超时（`RootCommands.h:303–314`），但 **`IOLockLock(gHost.lock)` 没有超时**

这条 flush **不是被动观察者**：它在音频/amp 线程里执行，执行时：

- audio map 已经从 `zizhan_audio_dma_map_begin()` 取得 **全局 `pong_smmu_lease_lock`**（`ZizhanAudioDmaMap.cpp:129`），并且要到 `zizhan_audio_dma_map_end()` 才释放（`:167–173`）；
- UFS 的 `PongUfsBlock::completeTransfer()` 也使用同一个 `gHost.lock`（`PongUfsTransport.cpp:696–713`）；
- SpringBoard/launchd/APFS 正在做 SystemOS/Data 的 UFS I/O：`PONG_UFS IO t=30s` 的 `SystemOS/Data` 计数很大，日志附近有 `tx_flush`/APFS 行；
- Wi-Fi 初始化也在这个窗口，`PongWifiPlatform.cpp:432` 同样要拿 `pong_smmu_lease_lock`。

因此最直接的读法是：**音频线程在 `pong_klog_flush(6)` 里被 UFS/`gHost.lock` 卡住 → 没有 `flushed=` → 音频线程不释放 SMMU lease → Wi-Fi/其他 SMMU writer/启动路径等待；如果 UFS/NoC 的阻塞扩散到 AP，Gunyah 冷咬在约 60 s 后发生。** 后半段（UFS 命令、`gHost.lock` 等待、NoC 背压、pinger 如何停止）没有直接日志，仍属推断；前半段（没有 `flushed=`、没有 WRITE、或 WRITE 已成功）是事实。

**为什么 `dma_watch`/FSR/GFSR 干净**：stage 9 挂死时 WRITE 未发出；stage 10 挂死时第一条 WRITE 已成功、DSP 已回 rx。两种情况下都没有 ADSP 共享内存 DMA 在进行，SMMU 自然没有 fault。之前把“`fault=400`”当作故障、把“stage 9 后第一条 WRITE 挂”当作 ADSP 读共享内存，都是被 checkpoint 的同步 flush 误导。

**4 MiB 有多大**：`PONG KLOG` 启动行是 `1025 blocks`；每次 checkpoint 都把整个 4 MiB ramoops（console+diag）重新写一遍，不是只写新增 console（`PongBootReset.h:183` 的注释写“~1 MB”，但 `kKlogBytes=0x400000` 是 4 MiB，以代码为准）。通过轮的 `flushed=0 us=13–20 ms` 说明正常时 UFS 能很快完成；挂死轮没有这个完成时间，说明 flush 没有返回，而不是“返回了但音频失败”。klog 文件里的 `reason 6`/300–321 KB 是 **上一次成功 checkpoint** 的头和部分数据；当前 stage 的 header 是最后写的，挂死时没有更新，这也和“flush 未返回”一致。

### 9.3 其他子系统的活动：只有 UFS/klog 是共同项

按“WRITE 是否实际发出”分组：**A = stage 9 挂、0 send**（a-2、b-2、ckpt、hold）；**B = stage 10 挂、1 send+rx**（b-1、c-1、rep）；**C = 通过、4 send+rx**（a-1、a-3、c-2、pre0、loud）。只有 B 的第一条 WRITE 和 C 的 4 条 WRITE 真正走到 ADSP；A 的第一条 WRITE 未发。下面的对照以这三组为骨架。

按每个锚点的前 ~300 行/整段 live log 对照（脚本在 `$ZIZHAN_SRC/work/codex-1118/parse_logs2.py`，原始摘要 `log-summary2.jsonl`）：

| 子系统 | 挂死轮的观察 | 通过轮的观察 | 判断 |
| --- | --- | --- | --- |
| UFS/klog | 锚点前最后一个 `PONG_UFS IO t=30s`；SystemOS/Data 大量读写；`tx_flush`/APFS 行；**当前 checkpoint 的 `flushed=` 缺失** | 同样有 t=30s/35s 和大量 UFS 活动；`flushed=0` 存在 | **唯一共同且紧贴锚点的事件是 checkpoint 的 4 MiB FUA flush**，不是 `PONG_UFS IO` 本身；正常 UFS 统计不能排除它 |
| SpringBoard/launchd | a-2/c-1/ckpt 的 `Pong UI18: native home request accepted=1` / `main queue wake completed backlight=1 lockscreen=1` 在锚点前 2–7 行；b-1/b-2/hold/rep 没有 home/wake 行 | a-1/a-3/c-2/loud 的 home/wake 在锚点后 26–88 行；pre0 在锚点前 27–28 行；`amp-tone-1116-v1` 在锚点前 3–4 行且通过 | 不是唯一项；只是 startup 窗口的邻近事件。`amp-tone` 是清楚的负控制 |
| GPU ADRENO/DCVS/BO/scanout | `PONG ADRENO: DCVS started` 在很早（约锚点前 2000–2600 行）；最后一个 `PONG GPU DCVS` 约在锚点前 500–1600 行（取决于该轮采样/日志量）；`Pong Metal: real Vulkan-backed device created` 常见于锚点前 28–103 行（a-3 为 -225；hold/rep 无）；没有 `PONG DBUF: flip` | 同样的 DCVS/Metal 行；首个 `PONG DBUF: flip`/`GPU frame completed` 在锚点后 2353–2795 行 | 不是唯一项；GPU 首次 DCVS 报告不紧邻挂点；首个 UI flip 在挂死前未发生，但这只是挂死的结果。没有证据表明 GPU DCVS 是触发器 |
| Wi-Fi | `Pong WiFi: waiting for en0 link`、`wifid[...]` 在 a-2/c-1/hold 的锚点前 2–12 行；b-1/b-2/rep/ckpt 较远 | c-2 通过轮的 wifid 也在锚点前 2 行；a-1/a-3/loud 也有 wifid | 不是唯一项；Wi-Fi 与 SpringBoard 启动同窗，不能单独归因 |
| IPCC | sample 3 `at_ns≈44.2–44.9 s`，`unknown=0`、`budget=0`；挂死轮没有 sample 4（因为日志在 ~50 s 结束） | sample 3 相近，sample 4 `at_ns≈54.2–54.9 s` 存在 | 计数健康；route0/1 都挂；不是 IPCC 丢消息/风暴。sample 4 缺失是日志终止，不是计数异常 |
| ADSP/GLINK | `PongAdsp` 到 stage 9/10；b-1/c-1/rep 的 WRITE send+rx 成功；没有 DSP fatal/GLINK 错误 | 同样 stage；4 条 WRITE send+rx | ADSP 在 stage 10 挂死前仍能处理第一条 WRITE；不是“DSP 读共享内存立刻挂” |
| UFS transport | `PONG_UFS` 启动行正常、`PONG KLOG 1025 blocks`；t=30s 统计 `busy=0ms`、`longest=29–50ms`；`tx_flush` | 同样的启动行和 t=30/35s 统计 | `busy=0ms` 是采样统计，不能排除单次 flush 卡住；没有 UFS error 行 |
| 电源/autoreset | `PONG_AUTORESET: SpringBoard up at 17s`；210 s deadline；`pong_klog_period` 在 n18 包中未设置 | 同样 | pinger 不是 checkpoint flush 的直接持有者（period=0），但它的停止是下游；不能据此证明“pinger 先停” |

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
- **通过轮也在同一个窗口**（a-1/a-3/c-2/pre0/loud 的锚点同样在 t=30–35 s、sample 3–4 之间），所以“时间窗”本身不区分挂/过；区分是 **checkpoint flush 是否返回**。
- **不是 GPU DCVS 首次报告**：`PONG ADRENO: DCVS started` 在早期（约锚点前 2000 行），第一个 `PONG GPU DCVS` 在锚点前约 500–700 行；通过/挂死都有，且没有紧邻。
- **首个 `PONG DBUF: flip`/`PONG SCANOUT: GPU frame completed` 在所有通过轮都出现在音频锚点之后**（+2353～+2795 行）；挂死轮没有 flip。这只能说明挂死发生在 **SpringBoard 的首个 UI frame 之前**，不能反推是 GPU/flip 导致。
- 这个窗口对应 **SpringBoard/launchd/Metal/wifid/APFS 启动 + 首批 UFS I/O**；`PONG_AUTORESET: SpringBoard up at 17s` 更早。**挂死与 startup 的 UFS 负载同窗，而不是与某个单一 SpringBoard/GPU 事件同点。**

### 9.5 AP 侧 MMIO/时钟路径核对

用户关心的“AP 是否在访问未开时钟/未上电寄存器”逐项核对：

**UFS klog（真正可疑的 AP 阻塞路径）**

- `amp_checkpoint` 在音频/amp 线程同步调用 `pong_klog_flush(6)`；`pong_klog_flush` 取 `gHost.lock` 并写 **4 MiB FUA**（`PongUfsTransport.cpp:288,327–342`）。
- UFS block I/O 的 `completeTransfer` 也取 `gHost.lock`（`:696–713`）；SpringBoard/launchd/APFS 正在 t=30 s 附近做大量 UFS 读写（`PONG_UFS IO t=30s`、`tx_flush`）。
- `gHost.lock` 是普通 `IOLock`，**`pong_klog_flush` 的 `IOLockLock` 没有超时**；`hostWrite4kLocked` 每 128 KiB 只有 1 s 命令超时（`RootCommands.h:303–314`），最坏 32 块 ×1 s = 32 s，加上锁等待/APFS 竞争可超过 60 s 冷咬窗口。这不是“未开时钟寄存器”，而是 **长同步 UFS I/O + 无超时锁 + startup I/O 竞争**。
- 音频线程在 flush 期间仍持有 **全局 `pong_smmu_lease_lock`**（`ZizhanAudioDmaMap.cpp:129`，直到 `:167–173` 才释放）。Wi-Fi 的 `PongWifiPlatform.cpp:432` 也要拿同一把 lease；如果 Wi-Fi 正在启动，它会等待。**这是一条可解释的锁链：audio → klog flush → gHost.lock；audio 同时持有 SMMU lease → Wi-Fi/其他 SMMU writer 等待。** 但“锁链如何让 Gunyah 冷咬”仍未被直接日志证明；不能把 UFS 锁链写成唯一机制。
- `PongBootReset.h:176–184` 的 pinger 在 `pong_klog_period>0` 时也会调用 `pong_klog_flush(5)`；n18 包没有设置 `pong_klog_period`，所以 n18 的 pinger 不走这条 flush。**11-17 的 `amp-pulse-1117-klog` 设置了 `pong_klog_period=5`，最后停在 DATA_PIN、没有 WRITE send，和 klog flush 阻塞的签名一致**；它是一个独立的旁证，不要和 n18 的 checkpoint flush 混为同一行。

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
- audio map 持有全局 lease；这是 §9.2 的锁链，不是未上电寄存器。

**IPCC 0x0ed18000**

- `ZizhanIpcc.cpp:47–56` 在 IRQ 里 `RECV_ID`/`RECV_CLEAR`；`PongAdsp.cpp:121–145` 旧路径同样；寄存器映射 `kIOMapInhibitCache`，没有 clock-gate 检查。IPCC 是 AOSS always-on。
- route0（c-1 挂、c-2 过）与 route1（a/b 挂/过）都存在；IPCC 计数 `unknown=0/budget=0`。**没有证据表明 IPCC 是共同触发**；它需要一次 route0 对照，而 n18 已经有 route0 挂/过，说明 route 不是决定项。

**LPASS 时钟**

- audio 路径通过 GPR/PRM 请求 LPASS core/macro clock，AP 本身不直接写 LPASS clock 寄存器；AP 的 `PONG GPU DCVS`/display 是 GX/DISPCC，不是 LPASS core。`PONG_AUTORESET`/SpringBoard 的 home/backlight 也不直接改 LPASS 时钟。
- **没有发现 AP 在 WRITE 期间访问未上电 LPASS 寄存器的代码路径。** 如果 ADSP 的 PRM 时钟被外部事件影响，日志应先出现 PRM/ADSP 错误，而不是 checkpoint flush 的 `flushed=` 缺失。

### 9.6 对 §6 假设清单的修正

| 假设 | 夜间数据 | 修正 |
| --- | --- | --- |
| H1 16 KiB 越过 64 KiB | a-1/a-3 都是默认 16 KiB 且 4 写通过；a-2 是 16 KiB 但 WRITE 未发出（stage 9 flush）；b/c/ckpt/hold 也不是 16 KiB 独有 | **不再支持“16 KiB 是确定性触发”**；保留为真实 WRITE 的潜在边界风险，但当前挂死不是它 |
| H2 SMMU fault/terminate | 所有有 dma_watch 的 n18 轮 `fsr=400 gfsr=0`，无变化；stage 9 挂死无 send；stage 10 挂死第一条 WRITE 已 send+rx | **不再支持 SMMU fault 是当前挂死原因**；SMMU 编程差异仍是潜在风险，但没有本次故障签名 |
| H3 WRITE/MEM_MAP token | 同一 token 编码在 a-1/a-3/c-2/pre0/loud 的 4 写全部成功；挂死发生在 flush，不在 token 解析 | **不是当前挂死原因**；仍是协议一致性 bug，保留为后续修复 |
| H4 amp/TDM/pulse | b-1/c-1/rep 的 stage 10 挂死发生在 `pulse_run()` 之前（flushed 行缺失）；a-2 等 stage 9 挂死也在 amp 输出之前 | **不是 stage 10 挂死原因**；amp/TDM 的“无声”仍是独立问题，需要可闻测试 |
| H5 IOVA 范围 | a-1/a-3 用 identity 0xf9e3b000 通过 4 写；无 pool IOVA 对照 | 没有证据；保留为未测差异 |
| H6 SCM/XPU | live DT iommus、NUMS2CB=0；ADSP stage-1；无 SCM assign | 没有证据；不要加 |
| H7 IPCC | route0/1 都挂/过；unknown/budget=0 | 不是共同触发；route0 对照已完成 |
| **H8（新）同步 4 MiB klog flush / UFS 锁** | 所有 checkpointed 挂死缺 `flushed=`；stage 9 无 WRITE、stage 10 有 WRITE send+rx；UFS t=30 s/APFS 负载同窗；4 MiB FUA 持 `gHost.lock` | **当前最强解释**；直接证据是 flush 未返回，机制（UFS 命令/锁/NoC/pinger）仍未完全证明 |

### 9.7 结论与下一步

**可以直接下的结论**：

- 11-17 的“5/7 ADSP WRITE 挂死”至少大部分是 **checkpoint 的同步 `pong_klog_flush(6)` 造成的观察者效应**；stage 9 挂死时 WRITE 未发，stage 10 挂死时第一条 WRITE 已成功。把这两类说成“ADSP 读共享内存挂死”不成立。
- **SMMU FSR/GFSR、IPCC 计数、ADSP/GLINK 在 n18 挂死前都没有异常**；没有证据支持 SMMU fault、IPCC 风暴或 DSP 读共享内存作为当前挂死的直接原因。
- **音频协议本身在 n18 的通过轮完成了 4 条 WRITE send+rx**（a-1/a-3/c-2/pre0/loud）；之前 pre0/loud 的无声音不是“WRITE 没成功”，而是 amp/TDM/时序问题。
- **d-2 是无效的 ramoops/早期启动轮**；不能作为 mode33 挂死证据。mode33 的有效轮 d-1 通过。
- **11-17 `amp-pulse-1116-v1`（无 checkpoint，klog 未写）和 `amp-pulse-1117-klog`（周期 klog 5 s，最后 DATA_PIN）仍是独立问题**；它们可能分别对应真实音频/SMMU 或 klog flush，不能由 n18 的 checkpoint 结论替代。

**下一步（最小、单变量）**：

1. **先拆掉观察者效应**：`zizhan_audio_checkpoint=0` 或把 `amp_checkpoint` 改成 **console-only**（不调用 `pong_klog_flush`）；用同一 a 臂（mode34、dry、16K、route1）重跑 2–3 轮。若不再挂，H8 得到直接验证；若仍挂，才回到真实音频/SMMU。
2. 若必须保留 klog：把 checkpoint 改成 **有界、非阻塞**：
   - 只写本次新增的 console 行（或 ≤64 KiB），不每次重写 4 MiB；
   - 从独立低优先级 worker 写，不在音频/amp 线程写；
   - 用 `IOLockTryLock`/有界重试，不让 `pong_klog_flush` 无限等 `gHost.lock`；
   - 不在持有 `pong_smmu_lease_lock` 的区间内做 4 MiB UFS 写（先释放 lease，或把 checkpoint 移到 map 之外）。
3. 然后回到真实音频：amp 保持 + 连续数据 + 可闻测试；SMMU 只在“无 checkpoint 的真实 WRITE 仍挂”时按 §7 的只读诊断重开。
4. 不要用 d-2 复跑 mode33；若需要 mode33 证据，先确认 ramoops/console 有效、有 `dma_watch`/klog，再计入。

**边界**：本次仍只读日志和源码；没有手机操作、没有构建、没有分区写。下一步需要新内核/包和设备窗口，不能在本次 03:00 前无人值守执行。

## 10. 参考

### 10.1 本机 live DT（仓库外，相对 `$ZIZHAN_SRC`）

- `work/hardware-20261115/audio-dma-live-dt-v1.dts:29241–29255`：`msm-audio-ion`（SID `0x1801`、sid-mask `<0 0xf>`、pool `0x10000000+0x10000000`）。
- `work/hardware-20261115/audio-dma-live-dt-v1.dts:18960–18978`：apps SMMU（`qcom,context-fault-retry`、`num-context-banks-override`、`num-smr-override`、`handoff-smrs`、全局/上下文 IRQ）。
- `work/hardware-20261115/audio-dma-live-dt-v1.dts:19034–19041`：`lpass_tbu`（`0x1800` + `0x400`）。
- `work/hardware-20261115/audio-dma-live-dt-v1.dts:12500–12528`：fastrpc SID `0x1803/0x1804/0x1805` 与 `"stall-disable","HUPCF"`。
- `work/hardware-20261115/audio-dma-live-dt-v1.dts:11298–11321`：`qcom,gpr`、`audio-pkt`、`q6prm`。

### 10.2 厂商 audio-kernel（公开 SM8475/taro-S 镜像）

- `https://github.com/ASUS-SM8475/vendor_qcom_opensource_audio-kernel/blob/taro-S/dsp/msm_audio_ion.c`
  - `#L36`：`MSM_AUDIO_SMMU_SID_OFFSET 32`
  - `#L263–L280`：`msm_audio_ion_get_phys()`，`*addr |= smmu_sid_bits`
  - `#L830–L913`：probe，`sid = args[0] & sid_mask`、`sid << 32`
  - `#L574–L585`、`#L680–L750`：`hyp_assign_phys` 与 `IOCTL_MAP_HYP_ASSIGN`

### 10.3 主线 Linux

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

### 10.4 2026-08 stage-2/SCM 澄清

- `https://lkml.rescloud.iu.edu/2608.3/03890.html`：`qcom,q6apm-dai: add stage-2 (SCM/VMID) access model`。要点：ADSP 是 stage-1 + `iommus`；只有 modem/mDSP 的 stage-2 保护上下文才用 `qcom,vmids` + SCM assign。

### 10.5 本仓库代码

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

### 10.6 既往记录

- `docs/progress/STATUS.md`：M1 行，5/7 挂死、2 轮跑通、下一步。
- `docs/progress/2026-11-15-audio-silent-owner.md`：mode25/26/27 的 SMMU 实际 ID、slot/bank、原 SCTLR、ASID 守卫、CBAR restore panic。
- `docs/progress/2026-10-31-frame-timeline.md`：g149 `sCR0=0x200406`、g151 CB3 `SCTLR=0xe0`/`FSR=0x400`。
- `docs/progress/2026-11-15-ipcc-routing.md`：route1 8 条统计（441 IRQ/95 s、unknown/budget=0）。
- `$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`：11-17 22:30 以后的 7 轮表、checkpoint、SMMU 恢复 panic。
- `$ZIZHAN_SRC/work/hardware-20261115/amp-dmaobs-v1-device-review.json`：`ID0=0x4c017e71`、`ID1=0x6000004b`、`ID2=0x5111`、slot112/bank73、原 SMR/S2CR。
- `$ZIZHAN_SRC/work/hardware-20261115/amp-dmalease-v2-device-review.json`：`fault=400` 与 `masked_fault=0`。
