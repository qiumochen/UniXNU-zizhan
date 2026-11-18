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

## 补充：连续流（wip 42a7688，内核 1877c659）

`ZizhanAudioFlow.h`：4 个 0x3000 周期槽轮流复用，最多 1–3 个 WRITE 在途，每个 WRITE 唯一 token（同一槽上次的 DONE 不会被当成本次），单 WRITE 100 ms 预算，整流有上限。mode 40 仅在 `zizhan_audio_stream_ms=<100..10000>` 时使用它（`zizhan_audio_inflight=<1..3>`，默认 2）；不带时仍走上面已验证的 4 周期路径。自测生产者改为按时钟节拍写。主机 `cs35l41-flow.py`（clang/gcc14）。

| 轮 | 在途 | 结果（3 s，selftest，静音） |
| --- | --- | --- |
| flow-f-1、flow2-f-1 | 2 | PASS：submitted=completed=299（143520 帧 = 2.99 s，实时）；**starved=0**；peak_us 13.5 ms；verified=299 |
| flow-g-1 | 3 | PASS：299/299，starved=0，peak_inflight 实际 2（生产者按实时写，水位限住）；peak_us 19.0 ms |
| flow-h-1 | 1 | PASS（协议）但 **starved=177**：单个在途时 DSP 每个周期之间都会空 |

结论：连续播放至少要 2 个 WRITE 在途；默认 2 正确。HAL 接入后用同一路径。功放仍关闭，可闻连续播放需要“两颗功放同时开、随流保持”的新时序（ZizhanCs35l41.c 现为逐颗 1 s 核心运行 + 单次脉冲），未实现。

## 补充：双功放播放窗（PRM 41 / SE15 prepare mode 40 / CS35L41 profile 19），静音 dry 设备验证

设计见 [双功放播放时序](../plan/2026-11-18-m1-playback-amp.md)。实现：`c404fac`（`ZizhanAmpPulse.h` 拆出 arm/hold_check/disarm；`silent_run` profile 19：两颗都准备到 HALO 运行 → 一起 arm → `ZizhanAudioTransfer.stream` + 每 50 ms 安全轮询（sticky/raw 故障、HALO 状态、运行时限）→ 逆序 disarm → 两颗收尾；任何失败收尾所有仍在准备态的芯片，总线不确定时不再访问）、PRM 41 提交（服务 Start → 连接图 → 播放窗内连续流），之后三处修正：自测生产者写满 60 s（功放准备约 33 s）、轮询发现故障时即使流报告成功也判失败（主机测试发现）、流从生产者最前沿后退 4 个周期（40 ms）开始。主机：`cs35l41-pulse.py`、`cs35l41-run.py`（新增双颗保持、dry、流中故障/HALO 停止、忽略中止的流、流失败、拒绝、晚期总线失败）、`cs35l41-prm-platform.py`、`cs35l41-service-gate.py`，clang 与 gcc:14 全过。

`zizhan_audio_service=1 zizhan_audio_prm=41 zizhan_audio_selftest=1 zizhan_pulse_dry=1 zizhan_audio_play_ms=1000`（dry：不写任何 AMP/音量寄存器）：

| 轮 | 内核 | 在途 | 结果 |
| --- | --- | --- | --- |
| play-v1-d-1/2 | f56299e2 | 2 | 功放侧全过（ready 1/1、arm/disarm、torn 1/1、数据脚恢复、reset 已断言）；流未开始：自测生产者 12 s 后停写，而功放准备用了约 13+ s → 修生产者 |
| play-d-1/2、play2-d-1..4 | c60d6e11（修生产者后） | 2 | 6/6 PASS：两颗同时保持约 1.0 s，99 周期/流、19 次轮询无故障、pdn、收尾、PRM PASS；其中 play2-d-2 starved=5 |
| play3-d-1/2 | 同上 | 3 | 2/2 PASS，starved=0，但 peak_inflight 仍 2（流从生产者最前沿开始，就绪数据只有 1–2 周期） |
| play4-d-1..3 | 5176e5e3（后退 40 ms 起流） | 3 | **3/3 PASS：peak_inflight=3、waits=0、starved=0、103 周期、20 次轮询无故障** |

观察：从图启动到播放窗打开约 33 s（功放固件 RAM 经 I2C 加载），系统声音场景必须改成“服务启动时预先准备、待命不使能”。在场包 `out/play-a4`（同一臂，去掉 dry，`zizhan_audio_attended=1`），启动器 `work/m1-firstsound/run-play.sh`，仅在用户在场且单次脉冲已听到后运行。
