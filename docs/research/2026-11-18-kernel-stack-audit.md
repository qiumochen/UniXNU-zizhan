# zizhan 内核线程栈审计（2026-11-18）

本文只读源码与 `$ZIZHAN_SRC/work/m1-firstsound/build-fix/kernel` 的反汇编；不运行手机、不构建内核、不修改主工作树或 `m1-firstsound`。审计对象是 `drivers/` 中所有 `kernel_thread_start`/`kernel_thread_start_priority` 入口，以及这些入口能直接到达的驱动内函数。

## 0. 方法与边界

- 反汇编命令：`xcrun llvm-objdump -d --disassemble-symbols=... "$ZIZHAN_SRC/work/m1-firstsound/build-fix/kernel"`；符号地址用 `xcrun llvm-nm -m`，旧 panic 用 `build-wp/kernel` 的 `atos`。
- 帧大小取函数序言的 `stp ..., [sp, #-N]!` 与 `sub sp, sp, #N`；动态 alloca（如旧 `prepare` 的 `0x1000 + 0x8f0`）按实际 `sub sp` 累加。数值是**静态近似**，不是运行时 unwind。
- 直接调用链按 `bl/b` 目标和新函数帧累加；`blr`、vtable、IOKit 回调、异常/中断帧**没有**完整展开，因此表中“最深已知链”是**下界/近似值**，不是证明不存在更深的间接路径。
- 16 KiB 内核栈是当前 XNU 配置；panic 已证明栈溢出，本文保留 1 KiB 作为“大帧”阈值、8 KiB 作为“需要单独复核的链”阈值。
- `IOLog` 路径固定记为 **L = 0x710 = 1808 B**：`IOLog 0x20 + os_log_with_args 0x50 + _os_log_with_args_internal 0x320 + log_queue_log 0x140 + log_payload_send 0x70 + __firehose_buffer_tracepoint_reserve 0x90 + oslogwakeup 0x10 + selwakeup 0x20 + knote 0x40 + knote_post 0x60 + knote_enqueue 0x30 + kqueue_threadreq_initiate 0x40`。

## 1. 结论

- **已确认的根因链**（旧 build-wp）：PRM `worker 0x1330` + `prepare 0x1950` + `silent_run 0x470` + `zero_transfer 0x50` + `write_commands 0x540` + L `0x710` = **0x3d90 = 15760 B**；16 KiB 只剩 624 B，`IOLog` 走 kqueue 唤醒时溢出。修复 `8d18d71` 后同链为 **0x1950 = 6480 B**。
- **修复后最大的残余单帧**是 `nativeAdrenoMailboxPoller` 的 **3696 B**（`input[0x700] + output[0x700]`）；叠加 `runCore→dispatch→submit→retireCompleted→release` 约 **6688 B**，若错误路径再调用 `IOLog`，约 **8496 B**，是下一优先项。
- 其次是 UFS 错误路径的 `transportFailure` **3328 B** + L（约 5.4 KiB）、`gtPoller` **2464 B** + L（约 4.3 KiB）、`zizhan_cs35l41_trim` **2704 B**（音频链约 5.5–6.7 KiB）、`slpiTransportObservation` **1920 B**（ADSP 链约 4.0 KiB）。
- 音频链已经从“接近 16 KiB”降到 **6.5 KiB**，但仍在 8 KiB 阈值附近；`trim` 的 `updates[2][98]` 仍值得静态化/堆化。
- 修复后 `n18d-r-1`（PRM34 dry）4 条 WRITE 全 `done`（10.2–11.9 ms），`n18d-s-1`（PRM33）2 条 WRITE 全 `done`（10.6–11.4 ms），`n18d-t-1`（PRM34 dry+checkpoint）4 条 WRITE 全 `done`（10.0–12.3 ms）且 checkpoint 的 `flushed=0` 都返回；三轮都 `PRM PASS`。

## 2. 所有线程入口

`kernel_thread_start` 和 `kernel_thread_start_priority` 在 `drivers/` 的入口如下。帧值来自 build-fix；旧 panic 链的旧值单独在 §3 标注。`L` 表示该路径会调用 `IOLog` 的 1808 B 尾。

| 来源:入口 | 入口帧 | 最深已知链（近似） | 合计 | 风险 |
| --- | ---: | --- | ---: | --- |
| `ArmCpu/PongCpuPolicy.h:worker` | 416 | +L | 2224 | 低 |
| `ArmDTPlatform/PongBootReset.h:pinger` | 128 | `pong_scanout_summary 240 + dbFlip 368 + dbMark 240 + klog 144 + UFS 352 + L?` | ≤3184 | 低 |
| `ArmDTPlatform/PongBootReset.h:guard` | 80 | `pong_klog_flush 144 + UFS 352` | 480 | 低 |
| `ArmDTPlatform/PongCpuPark.h:park` | 32 | `thread_bind` 等 | ~100 | 低 |
| `ArmDTPlatform/PongCpuPark.h:launch` | 80 | `IOSleep`/`thread_terminate` | ~150 | 低 |
| `ArmDTPlatform/ZizhanHallProbe.h:worker` | 208 | +L | 2016 | 低 |
| `ArmDTPlatform/ZizhanPowerAudit.h:worker` | 208 | +L | 2016 | 低 |
| `ArmDTPlatform/ZizhanTsensProbe.h:worker` | 384 | +L | 2192 | 低 |
| `NativeUfs/NativeUfs.cpp:registrationReport` | 128 | +L | 1936 | 低 |
| `NativeUfs/PongUfsTransport.cpp:ufsWorker` | 96 | `completeTransfer 176 + transportFailure 3328 + L` | 5408 | **中高（错误路径）** |
| `NativeUfs/PongUfsTransport.cpp:ufsIoMonitor` | 576 | +L | 2384 | 低 |
| `NativeUfs/PongUfsTransport.cpp:publishDeferred` | 112 | +L | 1920 | 低 |
| `PongAdreno/PongAdrenoNativeGlue.cpp:PongAdrenoPublishMetalAfterRoot` | 208 | `PublishMetalAccelerator 144 + OSMeta 112 + L` | 2272 | 低 |
| `PongAdreno/PongAdrenoPlatform.cpp:dcvs` | 1040 | `fenceReport 576 + L` | 3424 | 中 |
| `PongAdreno/PongAdrenoPlatform.cpp:worker` | 704 | `GmuStart 1280 + GmuStartPower 1136 + L` | 4928 | 中 |
| `PongAdreno/PongAdrenoScanout.cpp:shotIdleCapture` | 272 | `shotCapture 512 + L` | 2592 | 低 |
| `PongAdreno/PongAdrenoScanout.cpp:shotPrint` | 160 | +L | 1968 | 低 |
| `PongAdreno/PongAdrenoScanout.cpp:hudTick` | 112 | +L | 1920 | 低 |
| `PongAdreno/submit/NativeAdrenoPublish.cpp:nativeAdrenoMailboxPoller` | **3696** | `runCore 128 + dispatch 272 + submitRecords 80 + submit 1328 + retire 48 + release 1136` | **6688** | **高** |
| `PongAdreno/submit/NativeAdrenoSubmit.cpp:gtPoller` | **2464** | +L（`gtDump` 的 `line[2040]` 被内联） | 4272 | 中 |
| `PongPower/PongAdsp.cpp:adspThread` | 288 | `slpiTransportObservation 1920 + L` | 4016 | 中 |
| `PongPower/PongAudio.cpp:worker` | 848 | `openGraphs 592 + command 144 + next 640 + L` | 4032 | 中 |
| `PongPower/ZizhanAudioDmaMap.cpp:dma_watch` | 144 | `pong_klog_flush 144 + UFS 352` | 544 | 低 |
| `PongPower/ZizhanAudioPrm.cpp:worker` | **1344** | `prepare 864 + silent_run 1136 + zero_transfer 80 + write_commands 1248 + L` | **6480** | **高（已修复的主链）** |
| `PongPower/ZizhanCmdDb.cpp:worker` | 480 | +L | 2288 | 低 |
| `PongPower/ZizhanSe15.cpp:worker` | 176 | +L（只读 SE15 快照，不调用 `prepare`） | 1984 | 低 |
| `PongPower/ZizhanSe15Prepare.cpp:worker` | 0（尾调用包装） | `prepare 864 + trim 2704 + L` | 5376 | **中高** |
| `PongWifi/PongWifiPlatform.cpp:worker` | 976 | `pong_wifi_net_start 512 + ifnet_allocate 560 + L` | 3856 | 中 |

> 说明：`ZizhanSe15Prepare.cpp:worker` 的 0 帧是编译器把入口做成尾跳到 `prepare`；`ZizhanSe15.cpp:worker` 是独立的只读快照线程。`pinger` 的 `dbMark` 是否每次都打印取决于状态，表中给上界。

## 3. 帧 > 1 KiB 的驱动函数

以下帧来自 build-fix 反汇编；`旧` 列只在 build-wp 有对照时给出。

| 函数 | 帧 | 位置/原因 | 备注 |
| --- | ---: | --- | --- |
| `nativeAdrenoMailboxPoller` | 3696 | `NativeAdrenoPublish.cpp:206` 的 `input[0x700] + output[0x700]` | 单线程（`gPollerStarted` 保证一次），最适合静态/堆化 |
| `transportFailure` | 3328 | `PongUfsTransport.cpp:119` 的 `char text[2400]` | UFS 错误路径，调用 `IOLog` |
| `zizhan_cs35l41_trim` | 2704 | `ZizhanCs35l41.c:707` 的 `ZizhanAmpTrimUpdate updates[2][98]` | 在 `prepare` 的 amp 回执路径上 |
| `gtPoller` | 2464 | `NativeAdrenoSubmit.cpp:121` 的 `char line[2040]` 被 `gtDump` 内联 | 单线程，适合静态化 `line` |
| `pong_diagnostic_sysctl` | 2272 | `patches/xnu-ipados18/0001-pong-darwin24.patch`（非 drivers） | 只作内核级对照，不在线程入口表 |
| `slpiTransportObservation` | 1920 | `PongAdsp.cpp:611` 的 `zizhan_slpi_transport::Receipt` | 一次性启动路径，仍可静态化 |
| `prepare`（build-wp，旧） | 6480 | `ZizhanSe15Prepare.cpp:160` 的 12 个 amp 回执 + 动态 alloca | 已由 `8d18d71` 改为静态 `Receipts`，修复后 864 |
| PRM `worker`（build-wp，旧） | 4912 | packet builder 全部内联 | 修复后 1344；已加 `noinline` |
| `write_commands` | 1344 | `ZizhanAudioPrm.cpp:353` 的 `bytes[512]`/packet | 旧值 `0x540`，修复后 `0x4e0` |
| PRM `worker` | 1344 | `ZizhanAudioPrm.cpp` 的 packet builder 内联 | 旧值 `0x1330`，修复后 `0x540` |
| `NativeAdrenoSubmit::submit` | 1328 | `submit` 的本地 scratch/packet | 在 mailbox 链上 |
| `PongAdrenoGmuStart` | 1280 | GPU 启动本地表 | 在 Adreno worker 链上 |
| `batch_round` / `memory_commands` / `pcm_commands` / `tdm_commands` | 1184 各 | `ZizhanAudioPrm.cpp` 的 packet builder | 已加 `noinline`，避免永久计入 worker 帧 |
| `silent_run` | 1136 | `ZizhanCs35l41.c` 的 amp/TDM 运行 | 旧 `0x470` |
| `NativeAdrenoSubmit::release` | 1136 | `NativeAdrenoSubmit.cpp:223` | 在 mailbox 链上 |
| `PongAdrenoGmuStartPower` | 1136 | GPU 启动电源表 | 在 Adreno worker 链上 |
| `PongAdrenoBuildPowerTables` | 1104 | GPU 电源表 | 在 Adreno 启动链上 |
| `dcvs` | 1040 | `PongAdrenoPlatform.cpp:422` 的窗口/等级统计 | 在 DCVS 线程上 |

## 4. 合计 > 8 KiB 或接近的链

| 链 | 旧 | 修复后 | 判定 |
| --- | ---: | ---: | --- |
| PRM worker → prepare → silent_run → zero_transfer → write_commands → IOLog | **15760 B** | **6480 B** | 旧链就是 `n18c-p-1` 的溢出；修复后安全余量约 9.9 KiB |
| mailbox poller → runCore → dispatch → submitRecords → submit → retireCompleted → release | — | **6688 B** | 无日志下已接近 8 KiB；加 L 约 **8496 B**，是最大残余风险 |
| PRM worker → prepare → trim → IOLog | — | **6720 B** | 比正常 WRITE 链略高；`trim` 的 2704 B 是主因 |
| ufsWorker → completeTransfer → transportFailure → IOLog | — | **5408 B** | UFS 错误路径；`text[2400]` 是主因 |
| PongAdreno worker → GmuStart → GmuStartPower → IOLog | — | **4928 B** | 中风险 |

## 5. 风险排序与建议

**P0：`nativeAdrenoMailboxPoller`**

- `input[0x700]`/`output[0x700]` 移到 `static` 或 `IOMallocData`；该线程由 `gPollerStarted` 串行化，单实例，静态化不需要锁变更。
- 预计入口帧 3696→约 112 B；加上链上的 `submit/release` 仍约 3.1 KiB。
- 若保留栈数组，应至少把 `submit`/`release` 的 lifetime 分开，并禁止在该链上调用 `IOLog`。

**P1：UFS 错误路径 `transportFailure`**

- `char text[2400]` 改为 `static`/堆；UFS 传输失败路径本身已经由 `gHost.ready=false` 串行化，静态缓冲风险低。
- 预计 3328→约 928 B；`ufsWorker` 错误链 5408→约 3.0 KiB。

**P1：音频 `trim` 与 PRM 链**

- `ZizhanAmpTrimUpdate updates[2][98]` 改为 `static`/堆；`prepare` 是 boot-exclusive，生命周期只到本次 prepare。
- 预计 `trim` 2704→约 1 KiB 以下；正常 WRITE 链 6480 不变，trim 链 6720→约 5 KiB。
- 保留 `8d18d71` 的静态 `Receipts` 和 `noinline`；不要回退 `prepare` 到栈上回执。

**P2：`gtPoller`**

- `gtDump` 的 `char line[2040]` 改为 `static`/堆；`gtPoller` 单线程。
- 预计 2464→约 424 B；加 L 约 2.2 KiB。

**P2：`slpiTransportObservation`**

- `Receipt` 改为静态；该函数只在 ADSP 启动时执行一次。
- 预计 1920→约 100 B；`adspThread` 链 4016→约 2.2 KiB。

**通用**

- 给大帧函数加编译期告警（例如 `-Wframe-larger-than=1024`，仅作为告警，不改布局），并对 `drivers/` 的线程入口保留 `objdump` 帧检查。
- 不在 >1 KiB 帧的函数里直接 `IOLog`；把日志放到调用者或异步 worker。`IOLog` 的 1808 B 尾在 panic 链上是决定性的。
- 线程入口的栈使用应记录一次启动水位（canary/高水位），至少覆盖 PRM、mailbox、UFS、WiFi、ADSP 和 DCVS。
- 本轮只做只读审计；以上都是设计建议，未实现、未构建。

## 6. 复现命令

```sh
# 所有线程入口
rg -n 'kernel_thread_start' drivers

# 帧大小（示例）
xcrun llvm-objdump -d --disassemble-symbols='__ZN12_GLOBAL__N_17prepareEjP24ZizhanAudioSilentReceiptPK19ZizhanAudioTransfer' \
  "$ZIZHAN_SRC/work/m1-firstsound/build-fix/kernel" | sed -n '1,20p'

# panic 符号化（旧 build-wp）
xcrun atos -o "$ZIZHAN_SRC/work/m1-firstsound/build-wp/kernel" -arch arm64 \
  -l 0xfffffff007004000 0xfffffff009070894 0xfffffff0093ab9a4
```

## 7. 来源

- 修复 commit `8d18d71`（`fix(audio): keep the amp/WRITE path inside the 16 KiB kernel stack`）。
- 诊断 commit `1b535fd`（默认关闭的 WRITE 延迟/slow probe 与 ADSP fatal klog）、`a259237`（porting 记录）。
- 修复内核：`$ZIZHAN_SRC/work/m1-firstsound/build-fix/kernel`，sha256 `b698195262435c57427defdce8f7f9f9a61e92e26f1ba6db169e53704188a603`。
- panic 证据：`$ZIZHAN_SRC/logs/n18c-p-1.klog.txt`（reason 2；`Invalid kernel stack pointer (probable overflow)`；tid 200）。
- 源码位置：`drivers/PongPower/ZizhanAudioPrm.cpp`、`ZizhanSe15Prepare.cpp`、`ZizhanCs35l41.c`、`PongAdsp.cpp`、`drivers/NativeUfs/PongUfsTransport.cpp`、`drivers/PongAdreno/submit/NativeAdrenoPublish.cpp`、`NativeAdrenoSubmit.cpp`、`drivers/PongWifi/PongWifiPlatform.cpp`。

