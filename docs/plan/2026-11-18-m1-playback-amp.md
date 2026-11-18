# M1 可闻连续播放：双功放随流保持的时序设计（2026-11-18，未实现）

状态：设计。前提证据：mode 34 单次脉冲链路（11-18 修栈溢出后静音 18/18）、mode 40 服务 + 连续流（2 在途 3 s 实时、starved=0，见 [服务设备记录](../progress/2026-11-18-audio-service-device.md)）。**首次可闻仍以今晚 `run-fix.sh` 的结果为准**：若脉冲都听不到，本设计先不做，先查输出通路。

## 1. 现在的功放时序（`ZizhanCs35l41.c` `silent_run`，pulse profile 18）

每颗功放**依次**完整走一遍（chip 0 再 chip 1）：

1. 校准/prerun 写读回 → ASP 格式（TDM short-sync、32 bit RX slot）→ PLL 6 步 → pup patch + GLOBAL_EN（0x2014|1）→ 64×1 ms 采样确认 PLL 锁定 → ASPRX1 开（0x4800=0x10000）→ rates/MPU → 启动 HALO 核心（0x2bc1000|1）。
2. 100 个 10 ms 采样（约 1 s）核对 halo_state=2、heartbeat 变化、mailbox、违规/看门狗寄存器。
3. 第 49 个采样：零预滚 WRITE（`transfer->once(chip+2)`）→ `zizhan_pulse_run`：音量写到 -12 dB 档 → AMP 使能（0x2018 0x3320→0x3321）→ 数据 WRITE（tone ×(1+repeat)）→ 保持 ≤300 ms → AMP 关、音量恢复。使能后 100 ms/250 ms 两道时间门。
4. 停核心 → GLOBAL 关 → pdn patch → ASPRX 关。

结果：两只喇叭先后各响一小段（≤约 170 ms 数据），中间约 1 s 静默。这是“首声”诊断，不是播放。

## 2. 播放需要的时序（新 profile，建议 19，`zizhan_audio_play_ms`）

| 阶段 | 内容 | 与现状差别 |
| --- | --- | --- |
| A. 逐颗准备 | chip 0、chip 1 各走 §1 的步骤 1–2（到 HALO 运行且 heartbeat 稳定），**不停核心、不关 GLOBAL** | 新：两颗都停在“已运行、AMP 未使能” |
| B. 播放窗 | 两颗依次写音量 + AMP 使能（同现有读回检查）；随后 mode 40 的连续流（`ZizhanAudioFlow`，2 在途）送 HAL/自测数据，持续 `play_ms`（首轮 ≤2000 ms，硬上限 5000 ms） | 新：使能期间数据连续；时间门从“250 ms 内完成一次 transfer”改为“每 50 ms 一次安全轮询 + 总时长上限” |
| C. 安全轮询（播放窗内每 50 ms，两颗都读） | IRQ1 sticky/raw（0x10010/0x10090，与现有 `zizhan_pulse_faults` 相同掩码：AMP short、BST short/OVP/UVP、过温/告警）、halo_state、heartbeat 前进、看门狗 | 任一异常或 I2C 不确定 → 立即停流、两颗 AMP 关、走 D |
| D. 收尾（逆序） | 停流并 drain → 两颗 AMP 关、音量恢复 → 停核心 → GLOBAL 关 + pdn patch → ASPRX 关 → 数据脚还原 | 同现有步骤 4，但两颗统一在最后做 |

保留的硬约束：音量固定 -12 dB 档（不做增益搜索）；数据 ≤ -18 dBFS（自测图样 -36 dBFS）；不做校准/OTP 写；`zizhan_pulse_dry=1` 时跳过所有 AMP/BST/音量写，其余全部照跑。

## 3. 验证阶梯

1. **主机**：把 `silent_run` 拆成 `amp_prepare(chip)` / `amp_play_window(chips, flow)` / `amp_teardown(chip)`；现有 `cs35l41-run.py`、`cs35l41-pulse.py` 模型扩展双颗同时运行、轮询故障注入（每类故障都必须在一个轮询周期内关 AMP）、I2C 不确定时不再访问总线。
2. **静音设备（无人值守可做）**：profile 19 + `zizhan_pulse_dry=1` + mode 40 + selftest + `play_ms=2000`：两颗 HALO 同时运行约 3 s、轮询全过、流 starved=0、收尾全部读回，回 Android。连续 ≥6 轮 0 挂（沿用 11-18 的栈与时序判据）。
3. **在场**：去掉 dry，`play_ms=1000`、-36 dBFS 图样 → 用户确认“两只喇叭同时、连续约 1 s”；再 -18 dBFS。之后才接 HAL（I5 安装后，系统提示音走同一路径）。

## 4. 风险

- 两颗 HALO 同时运行时的 I2C 访问频率上升（轮询两颗 + 流的 GLINK）：先在静音轮测 I2C 轮询耗时，必要时轮询改 100 ms。
- PRM worker 栈：新函数遵守 11-18 规则（>512 B 局部放 static，大帧函数里不直接 IOLog）。
- 播放窗内任何超时都按“不确定”处理：只做 AMP 关这一类最小动作，不重试。
