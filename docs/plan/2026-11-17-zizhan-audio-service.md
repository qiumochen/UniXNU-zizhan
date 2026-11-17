# Zizhan 系统音频服务接线设计（D 增量）

状态：D设计+控制页ABI主机落地，服务/HAL未实现、未上机。前置A/B/C已由[音频单图实机记录](../progress/2026-11-17-audio-graph-device.md)取得限定DEVICE_VERIFIED：MFC/SAL/TDM与WR_SHMEM→MFC双图、真实MAP、有限零/非零48k数据、DONE和清理。当前安装为`statusbar-work-v1`，首声仍停叫用户。

## 目标与边界

最小目标是把有限图/批次接到**真实CoreAudio HAL环**，由`48k/2ch int16 ring → 2880B 24-bit packed → ZizhanAudioStream → WRITE/DONE`形成默认关闭的系统音频后端；在真实生产水位、图START和DONE都成立前不发布Ready设备。AMP/BST/TX、GPIO124数据脚和实际声音仍关闭；首声、外屏、自动切屏、GNSS现场不进入本增量。

## 现有所有权约束

- `PongAudio.cpp`定义了唯一的`PongAudio` IOKit服务、`sheng_audio_memory(type)`和`sheng_audio_request(selector)`；`PongAudioService.cpp`的用户客户端和`PongAudioPlugIn.c`都依赖这两个符号。
- `PongAudioPlugIn.c`的`SHENG_AUDIO_CLASS`为`"PongAudio"`，并校验`ShengAudioControl`的rate48000/2ch/65536帧/out_channels0等。不能把TFA9873路径设为`pong_audio=1`；也不能在另一文件重复定义`PongAudio`类/两个全局符号。
- 现有控制页只有`out_frames`（服务已消费帧），没有“HAL已写到哪一帧”的生产者水位。`ZizhanAudioStream`的欠载门需要生产者水位；没有它只能靠lead猜测，不能用零填充冒充媒体。

## 方案选择

### 方案A（建议）：独立`ZizhanAudio`服务与独立HAL bundle

- 新增`ZizhanAudioService.cpp`：类`ZizhanAudio`、用户客户端`ZizhanAudioUserClient`、独立`zizhan_audio_memory`/`zizhan_audio_request`，复用`ShengAudioAbi`布局但不复用Pong符号；不定义`PongAudio`类。
- 新增`ZizhanAudioPlugIn.c`或把`PongAudioPlugIn.c`参数化为class/bundle/UID宏；输出`ZizhanAudio.driver`，只在`enable_audio_hal`时安装到`/pong/audio-hal18`。现有`PongAudio.driver`保留为Pong回退。
- 优点：Pong/TFA与zizhan独立，符号和Ready状态不互相污染；缺点：userland构建/安装和类名多一套。
- 首轮不启用两个bundle同时暴露；zizhan服务仅在显式boot arg（例如`zizhan_audio_service=1`）下启动，默认关闭且不创建Ready设备。

### 方案B（不推荐）：给`PongAudio`加`pong_audio=3`分支

复用现有类和HAL插件，改动较少；但把zizhan图/环/服务状态塞进Pong文件，容易误触TFA和历史Pong资格，且`PongAudio`的probe/服务语义会混在一起。除非A的userland成本不可接受，不采用。

## 必须补齐的ABI/数据面

1. **生产者水位**：在控制页增加`out_written`（HAL在`DoIOOperation`写完后发布的最大sample time+frames）或新版本控制页；用序数/双读稳定校验。不能把`out_frames`（消费值）当生产值。
2. **Ready发布**：只在控制页magic/version/rate/2ch/ring/lead、服务owner、图OPEN/CFG/MAP/PREPARE/START全部成功后设置`ShengAudioOutputReady`；失败保持0并保留图/映射状态直到自动返回/reset。
3. **启动lead**：用真实生产者水位证明`next_frame+480 ≤ out_written`；不足时返回underrun，不写零。初始`next_frame`从anchor+lead或生产者水位安全处开始，必须主机测试边界。
4. **DONE与停止**：复用`ZizhanAudioStream`/`ZizhanAudioBatch`的DONE-only帧数、100ms单条/500ms整批、在途stop排空、uncertain保留；buffers_returned不授权关功放/卸映射。
5. **资源收尾**：StopIO/服务关闭时先停流/关图，再UNMAP/native-idle/PRM释放；失败保留owner，不重试同一图/包。

## 主机与设备验收

- 主机：完成服务/HAL接口的纯C/C++控制测试、`ZizhanAudioStream` host replay、完整日用host、精简/完整kernel；候选包默认关闭、无Ready、无AMP/BST/TX/数据脚。
- 设备D（若预算允许）：只验证服务绑定/默认关闭、真实HAL环有限窗口、生产水位/欠载/停止/清理；无输出、无首声、无App播放结论。每轮仍用后部USB-A、临时boot、完整前置/回退、30分钟监督、新鲜日志和Android完成。
- 首声必须在用户现场；不在无人值守中开启AMP/BST/TX或把Ready当作声音资格。

## 当前决定

本20小时窗口不直接实现方案A的服务/HAL bundle：A/B/C与UI已收口，D的服务类/ABI/生产者水位仍需单独设计与构建。保留心跳到窗口结束，下一步按方案A先做主机接口与负控制，再决定是否派单轮静音设备测试。

## 本窗口补记：控制页ABI已主机落地

`ZizhanAudioAbi.h`新增zizhan-only控制页v2：保留48k/2ch/480帧/65536环/30×480 lead，增加`out_written`与`written_seq`；`zizhan_audio_publish_written`以奇偶序发布64位水位，`zizhan_audio_written_stable`拒绝奇数/变化中的读，`zizhan_audio_has_frames`作为欠载门。`cs35l41-audio-abi.py`ASan/UBSan覆盖layout112B、ready位、480帧水位、UINT64边界、错误version/rate/channels/ring/lead及odd-seq拒绝，已加入完整日用host。尚未接内核服务、HAL插件或实际环，故不改变当前设备/音频资格。

进一步新增`ZizhanAudioService.h`服务门：只有control页有效、真实map、图START、且稳定生产者水位覆盖一个完整480帧period时才置`ShengAudioOutputReady`；`zizhan_service_stage`从稳定水位取数，欠载不填零，commit/stop/drain复用`ZizhanAudioStream`。`cs35l41-service.py`覆盖Ready前置、480→960水位推进、欠载、odd-seq失败、stop排空和Done，已加入完整日用host。仍未接IOKit服务/HAL插件。
