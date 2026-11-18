# ZizhanAudio 系统音频服务 I1–I4：实现与静音设备验证（2026-11-18）

上接 [D 服务迭代计划](../plan/2026-11-17-d-service-iterations.md) 与 [服务接线设计](../plan/2026-11-17-zizhan-audio-service.md)（方案 A）。全部默认关闭；设备轮全程 AMP/BST/TX/数据脚/输出关闭，只做临时 `fastboot boot`，不写分区，不装 HAL。

## 实现

| 提交 | 内容 |
| --- | --- |
| `7012d91` | I1：`ZizhanAudio` IOKit 服务与 `ZizhanAudioUserClient`（`drivers/PongPower/ZizhanAudioService.cpp`），不触碰 Pong 类/符号。只有 `zizhan_audio_service=1` 启动；`pong_audio` 非 0 或其它 PRM 模式时 SKIP。发布控制页 v2（4096 B）与 48k/2ch int16 环（262144 B）。判定逻辑在 `ZizhanAudioServiceGate.h`（纯 C，主机测试）。 |
| `5b187b4` | I3/I4：PRM **mode 40** 注册为服务的图 owner，在取 PRM lease 前等待每次开机一次的 Start（≤170 s）；随后跑 mode 38 已验证的图，用服务的环：图 START 后发布 Running + 新 anchor，稳定水位覆盖第一个完整周期才 Ready，4 个有限周期（欠载有界等待），周期间响应 Stop，结束清 Ready/Running。`zizhan_audio_selftest=1`：内核线程扮演 HAL 生产者。HAL 插件 `userland/ZizhanAudioPlugIn.c`（设备在控制页有效时出现、StartIO 等 Running、DoIOOperation 写完再发布 `out_written`）。CoreAudio 主时钟修正移到 `ShengAudioHostClock.h`，ZizhanAudio 也提供 selector 4。 |
| `b030c5b` | `compile_zizhan_audio()`：按需构建 `ZizhanAudio.driver`，默认镜像不调用。 |

主机：`cs35l41-service-gate.py`（gate、控制页、请求、handoff、起始帧）、`cs35l41-prm-platform.py` 新增 mode 40 默认关/无 Start 用例、`zizhan-audio-plugin.py`（协议 + iOS 18 编译）；clang 与 gcc:14；完整 daily-use host exit 0。

## 设备（静音，均约 198 s 正常回 Android，无 panic）

| 轮 | 内核 | boot-arg | 结果 |
| --- | --- | --- | --- |
| svc-a-1/2 | 7ff2b561 | （无） | 无任何 `ZIZHAN_AUDIO_SERVICE` 行 |
| svc-b-1/2 | 7ff2b561 | `zizhan_audio_service=1` | `published … hz=19200000 ticks_q32=1717986918400`（=400·2³²）；5 s 后 `check registry=1 control_valid=1 state=0` |
| svc-c-1/2 | 7ff2b561 | `+ zizhan_audio_prm=27` | `SKIP gate=4`（PRM 冲突）；PRM27 自身 PASS |
| svc2-d-1/2 | 5f8247eb | `zizhan_audio_service=1 zizhan_audio_prm=40` | 服务发布；PRM40 等待 Start，无 lease、无图命令（日志在 150 s 截止落盘，早于 170 s 超时） |
| svc2-e-1/2 | 5f8247eb | `+ zizhan_audio_selftest=1` | **PASS**：start taken（约 28 s）→ `GRAPH: RUNNING` → `running anchor` → `first_written=16230/16297 start_frame=16320 ready_after=1 underrun_waits=2 pack=4 packets=4 done_acks=4 frames=1920 drained=1 ready_cleared=1`，12 图 ACK，DMAMAP 恢复，PRM PASS |

## 边界与下一步

- 未安装 HAL；未经 CoreAudio/audiomxd；无功放、无声音、无 App 播放结论。
- I5（需用户授权）：候选 `work/userland-22H374/audio-hal-v1`（statusbar-work-v1 + `/pong/audio-hal18/ZizhanAudio.driver`，7 个 OS 块，仅主机准备），安装/回滚脚本 `work/m1-firstsound/audio-hal-v1-os-operation.py` 未运行。
- 之后：带功放的 HAL 流（首个可闻 App 声音）须用户在场。
