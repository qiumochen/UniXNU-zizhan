# 音频环取数、24位打包与格式接线核对

源码基点`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`加未提交工作树。静音窗口关闭后接手，只做主机实现、证据整理与双配置构建；未启动手机、未安装、未出声、未commit/push。当前任务状态见[STATUS](STATUS.md)，本机工件与进程状态见仓库外`$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`。

## 格式接线的确定部分

同一轮Android只读播放证据给出两层格式：

- AudioFlinger speaker deep-buffer：48000Hz、2声道，HAL与processing format均为`AUDIO_FORMAT_PCM_24_BIT_PACKED`，HAL frame count 1920（40ms）。
- 同一播放期间ALSA PCM32：`RW_INTERLEAVED/S24_LE/96000Hz/3ch/PREPARED`，owner为AGM服务；PCM31/33 closed。未把PREPARED或Android状态提升为RUNNING/BCLK证据。

本机Forte ACDB（本地私有，sha256`089eb5e42b180bd86bb4f12ebd250efa89fcd80d9073931d694139fe38351359`）的唯一24-bit TDM候选为：

| 项 | 值 |
| --- | --- |
| 子图/实例 | `0xb0000002` / `0x428f` |
| TDM sink | module `0x0700100e` |
| 硬件格式 | 96kHz、24-bit、3声道、fixed-point |
| TDM | slot mask `7`、4槽×32位、short sync 0、normal 0、delay 1 |
| lane | mask `2` |
| clock | 12.288MHz（96k×4×32） |

同一子图的SGIT模块表包含Media Format Converter `0x07001015`，实例`0x48ba`；实际ACDB解析报告保存在仓外（`tdm-verified-inputs-v3.json`）。这只证明TDM sink与MFC属于同一原机子图，不证明iPadOS侧连接顺序、端口或参数已被DSP接受。结合AudioFlinger 48k/2ch与硬件96k/3ch，可以确定转换应在原机ADSP MFC内完成，而不是由CPU猜一个重采样滤波器，也不能把三个通道直接当成三个扬声器。

## 本轮交付

### HAL环取数

新增`drivers/PongPower/ZizhanAudioRing.h`，复用既有系统HAL环合同：65536帧、48kHz、2声道、int16交错、按CoreAudio sample time索引。每个10ms周期精确取480帧，写2880字节、2声道、24-bit packed（每采样3字节）：

- int16到24-bit存储按`sample * 256`符号扩展；正负满幅、环绕边界和字节序有独立断言。
- `zizhan_ring_ready`是独立欠载门控；生产水位不足时拒绝打包/提交，不把缺失数据变成静音缓冲区。
- 不重采样、不复制出第三声道；48k→96k与2ch→3ch仍明确留给DSP MFC。

### 有限批次扩展

`ZizhanAudioWrite.h`新增每请求`bytes`，默认仍为11520字节；合法性要求非零、4字节对齐、不超过65536。`ZizhanAudioBatch`新增`zizhan_batch_init_ex`，允许调用者声明精确周期：

- 11520B/960帧（96kHz/3ch/32-bit）保持原行为，旧`zizhan_batch_init`仍使用该默认。
- 2880B/480帧（48kHz/2ch/24-bit packed）用于HAL前端的有限批次。
- 每个DONE只推进该批次的准确帧数；槽位、token、地址、句柄、status与metadata仍严格核对。
- 初始化增加slot extent和低4GiB边界检查；超时/错误/不确定仍保留，不归还所有权，不授权卸映射或关功放。

## 主机与构建证据

```sh
python3 tools/zizhan/tests/cs35l41-ring.py
python3 tools/zizhan/tests/cs35l41-batch.py
python3 tools/zizhan/tests/cs35l41-tdm-inputs.py
python3 tools/zizhan/mac/check-daily-use-host.py
python3 tools/zizhan/mac/cs35l41-tdm-inputs.py --acdb "$PRIVATE_ACDB" --output "$PRIVATE_REPORT"
```

- `cs35l41-ring.py`：ASan/UBSan下覆盖环绕、正负满幅24-bit字节、欠载门控、四槽WRITE/DONE、480帧累计、token/元数据/超时/失败保留。
- `cs35l41-batch.py`：原11520B/960帧核心回归通过。
- `cs35l41-tdm-inputs.py`：14项结构/大小/绑定控制，新增TDM子图MFC成员校验；实际Forte ACDB解析给出MFC实例`0x48ba`。
- 完整日用host通过，新精简/完整内核均通过，75项runtime源哈希一致。
- Porting record结构检查通过（16个结构化area；使用本地UniXNU工具与临时合并的component catalog，未修改参考checkout）。
- 精简kernel SHA256 `2405594b0552712f1ed1786e96c3eceb47ae40aac52f047d1e46fb905bf17885`（17,847,952字节）。
- 完整kernel SHA256 `f08cb6fd332b58cd4348df4ee8c6d5bdcb1edf345bfa05a06dde21ceb6fdc7df`（44,627,512字节）。

本轮runtime源哈希（与构建目录一致）：

| 文件 | SHA256 |
| --- | --- |
| `ZizhanAudioBatch.c` | `c46283bc773502598499b53798c2219b799085309b0c4e4c5225169faf4cf62a` |
| `ZizhanAudioBatch.h` | `d14a2b8dce7a060af3372f5f7b564a0a9abae542977351c80c50d2df4bc630de` |
| `ZizhanAudioRing.h` | `3a42d834a85a1b7f6fbb84bb576822d17b3a1df2893bbb822b6d24e67c0e6338` |
| `ZizhanAudioWrite.h` | `43019edbd6b52f053ea0093892af046be9955173f58cd0d67926c176270ee3df` |

## 未完成与下一动作

没有建立单图`WR_SHMEM 48k/2ch/24bps → MFC 0x07001015 → TDM sink 0x0700100e`。当前probe仍使用分离的PCM/TDM图与11520B/96k/3ch写入，系统HAL服务也没有发布zizhan设备或Ready端点；环水位尚无真实生产者的运行接线。因此本轮不宣称App播放、持续播放、首声或任何新的设备音频资格。

下一最小增量是主机侧单图计划：按公开AudioReach/SPF结构声明WR_SH_MEM、MFC、TDM sink的模块/连接/端口与MFC输入48k/2ch/24-bit packed、输出96k/3ch/24-bit参数，默认关闭并用负控制拒绝缺模块、错误端口、错误格式和错误DONE。该计划通过host及双kernel后，才准备fresh默认关闭静音设备轮；首次扬声器输出继续停叫用户。
