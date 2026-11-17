# 2026-11-17 模式38有限运行时入口实机通过（限定）

状态：`DEVICE_VERIFIED`（限定：受限自测生产者、48k/2ch/24-bit ring、四批 WIRE/DONE、Ready 门控、停止排空与清理；**未接用户态 HAL/系统音频服务，未出声**）。安装基线为 `statusbar-work-v1`，OS `5c0912ba...`、UI `f2d6393a...`、helper `14653f8d...`；本轮只做临时 `fastboot boot`，未安装、未改 Data/分区。AMP/BST/TX、GPIO124 数据脚与输出始终关闭。

## 假设与变量

- 控制：`audio-graph-1117-v5/v6` 已实机通过同一单图、同一真实映射、同一 PRM 握手和 4×2880B 零/已知48k WRITE，各 4 个真实 V2DONE/1920 帧。
- 本轮唯一变量：`zizhan_audio_prm=38`。仍用同一 `WR_SHMEM 48k/2ch/24bps → MFC 0x07001015 → TDM 0x0700100e` 图、同一 MAP 句柄路径、同一 PRM 时钟/核心持有窗口。
- 新增数据面：分配 v2 控制页与 65536 帧 int16 ring；内核内有限生产者按 480 帧推进 `out_written`（奇偶序发布）；`ZizhanAudioService` 只有在控制页有效、真实 map、图 START、且稳定水位覆盖一个 480 帧周期后发布 Ready；未覆盖周期必须返回欠载且不填零；第 4 批在途时 stop，要求其真实 DONE 后 batch 才 `done`；最后清 Ready、UNMAP/native-idle、恢复映射。
- 输出边界：不写数据脚、不启 AMP/BST/TX、不设 `pong_audio=1`、不设 `zizhan_audio_attended=1`、不装任何用户态 HAL。

## 源码与构建

- 源码改动集中在 `drivers/PongPower/ZizhanAudioPrm.cpp`：新增 mode38 `runtime_round()`（约 100 行）与 `ZIZHAN_AUDIO_RUNTIME` 汇总；复用既有 `ZizhanAudioAbi.h`/`ZizhanAudioService.h`/`ZizhanAudioRing.h`/`ZizhanAudioBatch`/`ZizhanAudioDmaMap`。
- 设备轮使用的完整 kernel：`827f5bc001f139e01573b671d1d349ffcb624279bcc8e6f5f4520ec64adb774d`（44644680B）；精简 kernel `49592feb...`（17847952B）。
- host：`cs35l41-service.py`、`cs35l41-ring.py`、`cs35l41-batch.py`、`cs35l41-audio-abi.py` 均 PASS。精简/完整 kernel 构建 PASS。
- 设备轮后仅改了一处汇总日志门控：mode38 不再进入无关的 mode30–34 `ZIZHAN_AUDIO_WIRE` 汇总；数据面不变，未重跑设备。当前源码对应完整 kernel `c183d1266f24b7225b7dfb45cdf9be1f5b0df05138de40e44f7df453d62518a3`（同 44644680B），设备证据仍以 `827f5bc0...` 为准。

## 设备轮

- 包：`audio-runtime-1117-v1`，boot args 907B；`pong_adsp=2`、`pong_audio=0`、`zizhan_audio_prm=38`、`zizhan_ipcc_route=1`；`pong_autoreset=210`；无 `zizhan_se15_prepare/observe`、无 `zizhan_audio_attended`。
- 前置：`app-preflight.py --trial statusbar-work-v1` 重新做了 live manifest/wholeOS 校验，PASS；备份清单存在且无错误；无 cycle/install/waiter/lock。
- 运行：后部 USB-A，`cycle.sh` 临时启动，200 秒回 fastboot，ramoops/klog 取回 1400664B，Android `boot_completed=1`/product `zizhan` PASS，无 panic/固件崩溃。

## 结果（日志原文）

```text
ZIZHAN_AUDIO_GRAPH: ack ... 12 条 token 7a300001..7a30000c 全部 status=0
ZIZHAN_AUDIO_GRAPH: RUNNING device_sg=4106 stream_sg=4101 mfc=6182 tdm=6281 data=0 audio_output=0
ZIZHAN_AUDIO_MEMORY: rx ... handle=b0d56218 remote=0
ZIZHAN_AUDIO_RUNTIME: PASS acks=12 ready_before=0 ready_after=1 watermark_gate=1 underrun_gate=1 pack=4 packets=4 done_acks=4 frames=1920 drained=1 ready_cleared=1 mapped=1 restored=1 memory_phase=3 handle=b0d56218 data=known48 audio_output=0 finished=1
ZIZHAN_AUDIO_DMAMAP: PASS installed=1 ...
ZIZHAN_AUDIO_DMAMAP: PASS installed=0 stream_restored=1 context_retired=1 prior_context_restored=0 ... data=0 audio_output=0
ZIZHAN_AUDIO_PRM: PASS phase=6 reason=0 sent=5 acknowledged=5 request_ack=1 release_ack=1 held=0 uncertain=0 ignored=0 finished=1
ZIZHAN_AUDIO_PRM: clock id=204 hz=12288000 attribute=4 root=0 request_ack=1 release_ack=1 held=0 uncertain=0 pads_isolated=1
```

严格 review：`work/unattended-20261117-20h/audio-runtime-v1-device-review.json`，状态 `PASS_SCOPED_MODE38_FINITE_RING_RUNTIME_ENTRY__NO_OUTPUT`；实验 `audio-runtime-v1-experiment.json`；preflight/Android/日志在 `$ZIZHAN_SRC/logs/audio-runtime-1117-v1.*`。

## 已知日志伪影

本轮日志另有：

```text
ZIZHAN_AUDIO_WIRE: FAIL callbacks=0 ... outputs_off=1
```

这是 `mode>=29` 汇总条件把 mode38 也包含进来的日志门控伪影：mode38 从不进入 mode30–34 的 WIRE 路径，该行所有计数均为零值。它不参与本轮判定；随后的纯主机修改已把该汇总排除 mode38，未因此重跑同一设备输入。严格 review 显式保留了该行并注明原因。

## 结论边界

- 这是“默认关闭的有限运行时入口”资格：控制页水位、Ready 门控、欠载拒填、4 次真实 V2DONE、stop 后末批 drain、Ready 清除和映射清理均在真机成立。
- **不是**用户态 HAL、系统音频服务、App 播放、持续播放、首声或声学资格；没有用户态生产者，也没有启用任何输出路径。
- 12 图 ACK 与 MAP 句柄 `b0d56218` 与 mode35–37 同源；mode38 的 PASS 不倒写旧轮次，也不替代缺失的 HAL 服务。

## 下一动作

1. 实现独立 `ZizhanAudio` IOKit 服务/用户客户端与 `ZizhanAudio.driver` HAL bundle，默认关闭（仅在显式 boot arg 下启动），复用本轮已验证的控制页/水位/流/批次/清理顺序；先主机接口与负控制，再双 kernel。
2. 只有在 HAL bundle 安装与真实生产者水位都具备时，才考虑再派一轮静音设备 D；首声、AMP/BST/TX、外屏/触控/自动切屏、GNSS 现场仍需用户在场或单独授权。
3. 未 commit/push。