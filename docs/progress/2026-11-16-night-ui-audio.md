# 夜间静音窗口：工作UI与音频播放核心

基点`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`加未提交工作树。用户已授权21:13–23:13窗口，22:28后不启设备轮；当前队列见[STATUS](STATUS.md)。首声、外屏、用户GNSS不执行。

## UI-STATUS-03：候选准备完成，未安装

复用了已编译签名、源哈希匹配的默认Pad UI和新双kernel已通过的静音kernel，诊断/强制enable均不带，准备独立UI-only工作trial及917B新启动包，保留weather完整回退。新install state已将差异块从done表移除，避免历史安装成功记录导致跳写。候选复用已保留的系统镜像输入，未改Data或OS370。

准备耗时达到本段预算，按计划留在PREPARED，不开始安装或测试，不把包备好称已部署。手机最后已安装状态仍是原weather UI；后续安装须fresh完整核验。

## SENS-BASELINE-03：独立voter捷径不成立，保持阻塞

固定厂商版本7d10d738b2c4299f3b8549c61a01ece39f19c788的waipio.c中，CE0/SN3/SN0/SH0/MC0/ACV全为voter_idx0；idx1的同名ACV/MC0/SH0属于display节点，不是已证实空闲的SLPI专属voter。ACV主路径enable_mask8，display路径1，语义也不同。不能把索引1或同名资源直接当成安全独立票仓。

检查的设备层ADT/prelink交接入口未提供可信的本次启动六票生产者。此结论限定本次入口，不声称固件永远不可读回。下一具体解阻是提供实际启动阶段生产者及voter/DRV/set语义，或有原机依据的独立票仓证明；当前无输入，维持资源写/PAS12/remote3初始化禁止。不重采cmd-db/SMEM，转音频主线。

## AUDIO-STREAM-01：有限批次调度核心HOST_VERIFIED

新增ZizhanAudioBatch.c/.h，纳入实际power archive编译。每个新channel/mapping epoch只用一个零初始化实例，最多四个不同页对齐槽位、各11520B/960个硬件96k帧；不循环token、不在原实例重新初始化。staged标志由持有真实映射及内容证明的调用者提供，本核心不写DMA或猜格式。

复用生产WRITE/V2DONE编解码与严格地址/句柄/token/status/metadata核对。Basic success不归还所有权，只有匹配DONE才推进confirmed_frames。每条等待100ms、整批500ms，停止在途时等待确切DONE，超时/发送失败保留uncertain；buffers_returned不授权功放关闭、UNMAP或资源释放，外层仍须真实reset/保护/租约证明。

生产C核心的ASan/UBSan及完整日用host通过，覆盖四槽位/边界、无准备不发、错误或迟到/重复DONE、Basic非完成、禁止忙时重初始化、停止排空、失败保持。新精简/完整内核通过，74项runtime源哈希一致。主机回复为测试夹具，不称设备ACK。

本次按计划的格式未知退路，只交付缓冲所有权与有限调度部分；尚未接HAL环缓冲取数、实际格式转换、运行时PRM调用或音频服务发布。没有新音频包、手机播放或新增硬件资格。原机AudioFlinger48k双声道与TDM96k三通道共存，下一先明确原机ADSP图中的转换/通道映射，不能默认由CPU复制采样解决。之后接真实缓冲生产者及默认关闭运行时入口，首声仍停等现场。

本批精简kernel SHA256：`547649f93d933e9f7c54ecfce928760fef4cc4cab9bbdcdd1ca1429d774fc661`；完整：`e60b01ee8d8bd27c0da7fdc880400af8416cb1b15cdac9189a2415fa669abeab`。

窗口关闭后的接手补齐见[音频环取数、24位打包与格式接线](2026-11-16-audio-ring-wiring.md)：48k/2ch HAL环、2880B/480帧批次、TDM子图MFC核对及新双kernel；仍未接单图/HAL服务/首声。
