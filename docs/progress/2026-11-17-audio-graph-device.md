# 20小时窗口：音频单图生命周期与零数据实机资格

源码基点`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`加未提交工作树。当前20小时窗口从2026-11-17 00:11:55（UTC+8）开始；本轮只做默认关闭的临时启动，未安装OS/UI，未commit/push。当前队列见[STATUS](STATUS.md)，计划见[20小时计划](../plan/2026-11-17-20h-unattended.md)。

## 结论

**AUDIO-STREAM-01 / DEVICE_VERIFIED（限定）**：链式双图`device MFC 0x07001015 → SAL 0x07001010 → TDM sink 0x0700100e`与`stream WR_SHMEM 0x07001000 → MFC input`在同一owner下完整走通。设备轮A实际12个图命令全部status0、真实MAP句柄、两图PREPARE/START、STOP/CLOSE、UNMAP/native-idle恢复和PRM释放；设备轮B在同一图上完成4×480帧/2880字节零数据WRITE、4个实际V2DONE、1920帧和完整清理；设备轮C用固定48k/2ch/24-bit packed 1kHz/taper非零数据完成同样的4 WRITE/4 V2DONE、1920帧、pattern准备/清除和完整清理。AMP/BST/TX、数据脚和音频输出始终关闭；本轮不称App播放或首声。

## 设备轮A：图生命周期

- 运行`audio-graph-1117-v4`，包907B，boot args含`zizhan_audio_prm=35`、`pong_adsp=2`、`pong_audio=0`、`zizhan_ipcc_route=1`；无CS35L41/SE15/attended输入。
- 实际12个命令：device OPEN/SET_CFG、stream OPEN/SET_CFG、device PREPARE/START、stream PREPARE/START、stream STOP/CLOSE、device STOP/CLOSE，全部`status=0`。
- 实际MAP句柄`b0d56218`；SMMU install/restore PASS，`stream_restored=1/context_retired=1/prior_context_restored=0`。
- `ZIZHAN_AUDIO_GRAPH: PASS acks=12 mapped=1 restored=1 memory_phase=3`；`ZIZHAN_AUDIO_PRM: PASS ... request_ack=1 release_ack=1 held=0 uncertain=0`。
- 新鲜日志1,406,230字节，Android完成PASS，无panic/fatal/ADSP失败，无音频输出。
- raw full kernel `ebcfecc8f91ac7c26cb1df300c5e40624384fe7d155badd0b9687cc6aa2ed5f0`；packaged kernel `a1585da1a83ce61ab7ee83869b0f1775565c3f539ce1c207f5af9a785e589741`；package image `2f8be04e6e83da984116d72b6f1e580275a39d12d1072eb84751a5e271c26cc5`。

## 设备轮B：有限零数据

- 运行`audio-graph-1117-v5`，包907B，`zizhan_audio_prm=36`；同一图、同一映射、同一owner。
- 先验证UEFI 64KiB预像全零；4个不同2880字节/480帧槽位，实际WRITE/真实V2DONE；确认1920帧、4个batch ACK、zero_verified1。
- `ZIZHAN_AUDIO_GRAPH_DATA: PASS acks=12 batch_acks=4 frames=1920 bytes_each=2880 zero_verified=1 mapped=1 restored=1 memory_phase=3`；`ZIZHAN_AUDIO_PRM: PASS`。
- 新鲜日志1,369,874字节，Android完成PASS，无panic/fatal/ADSP失败；AMP/BST/TX/数据脚/输出仍关闭。
- raw full kernel `0867d8a170365dc036b5b8fbdcb383986e3eae20c4efb557937e85fe82aae93b`；packaged kernel `6a220ff68291266779d37c65d361c04f516419be284071f1e2ecd9d1d72ca3ce`；package image `eaef1f8abdf1fe5331e457263b1ca26e08e511bd3a2bf14f2e5ffc1dad5fe337`。

## 设备轮C：有限非零48k数据

- 运行`audio-graph-1117-v6`，包907B，`zizhan_audio_prm=37`；同一图、同一映射、同一owner。
- 先验证64KiB零预像，向4个槽位写入固定48k/2ch/24-bit packed 1kHz/-36dBFS/1ms taper pattern；`ZIZHAN_AUDIO_TONE48: PASS clear=0 ... reads=32768 writes=2880 bytes=11520 frames=480 slots=4`。
- 4个非零2880字节WRITE/4个实际V2DONE、1920帧；随后`clear=1`再次验证并清零；`ZIZHAN_AUDIO_GRAPH_DATA: PASS ... tone_prepared=1 tone_cleared=1 ... data=known48 audio_output=0`。
- 12图ACK、UNMAP/native-idle、PRM PASS、Android完成PASS；无panic/fatal/ADSP失败，AMP/BST/TX/数据脚/输出仍关闭。
- 新鲜日志1,266,177字节；raw full kernel `7a451d4a2b51c45c08332b4663ca7bb37077da94f3205304d21bc74e3f9e3ac1`；packaged kernel `76d8e43e12eac672bda51ed3475af36b50a37bfa25611b38218a919e9f8a95a1`；package image `9d841e89710aad2e4bbe8527fddb4a43537de8d55aa6ccf2fd470d3db43413ef`。

## 被保留的负结果

- `audio-graph-1117-v1`（旧30/18 media param）：device/stream OPEN和device CFG status0，WR_SHMEM SET_CFG超时；acks3/mapped0。说明旧参数尺寸不是最终形态。
- `audio-graph-1117-v2`（32/36但alignment/interleaved字节01/01）：同样超时；说明尺寸修正后仍有格式布局差异。
- `audio-graph-1117-v3`（vendor uint16 alignment 01/00）：实际收到`BASIC_RESULT opcode=0x02001005 token=0x7a300004 src=0x6181 dst=0x2103 payload=8`，但owner错误要求`src_port=1`而丢弃，随后超时；acks3/mapped0。
- `audio-graph-1117-v4`（接受模块src回复）：图生命周期PASS。

这些负结果保留原始日志和包，不把后续PASS倒写成旧轮PASS。

## 未完成

- 已有有限非零48k测试数据，但没有App/系统音频服务、Ready端点、麦克风或首声。
- 没有把实际CoreAudio HAL环生产者接入mode图；当前B是有限零数据映射，不是持续播放。
- 没有AMP/BST/TX/GPIO124数据脚或保护程序负载运行；这些仍停在前一阶段已验输出关闭边界。

下一动作：C可选非零48k测试数据（输出关闭）；D默认关闭HAL环生产者/运行时接线；P7 UI-STATUS-03独立收口。首次扬声器输出继续停叫用户。

## 后续窗口内补记

- C已由`audio-graph-1117-v6`/mode37完成：固定48k/2ch/24-bit 1kHz/taper pattern准备、4非零WRITE/4 V2DONE/1920帧、pattern清除、UNMAP/native-idle/PRM清理和Android完成PASS；AMP/BST/TX/输出仍关闭。
- P7已由`statusbar-clean-1117-v1`完成：UI-only安装/wholeOS读回、Home+电池设置页135×120全宽采样、电池132/135列；当前安装已切换为`statusbar-work-v1`，原weather回退保留。见[UI记录](2026-11-17-ui-status-03.md)。
- D主机前置新增`ZizhanAudioStream.h`：48k/2ch int16 HAL环→2880B槽位→有限WRITE/DONE、欠载门、next_frame和stop/drain；`cs35l41-stream.py`ASan/UBSan及完整日用host通过。尚未接实际HAL服务/内核owner/Ready，未上机。
