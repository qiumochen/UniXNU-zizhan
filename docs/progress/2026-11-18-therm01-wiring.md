# THERM-01 内核接线（默认关闭）——2026-11-18

状态：**主机实现 + 主机测试**。分支 `codex/2026-11-18-therm01`，基于
`wip/2026-11-17-audio-sensors` 的 `8913eef`。本轮没有构建内核、没有写手机、
没有启动设备轮；下面的设备判据都是待派轮次，不是结果。

## 1. 改了什么

| 文件 | 改动 |
| --- | --- |
| `drivers/ArmDTPlatform/ZizhanThermalService.h` | 新增周期服务：1 s 两 bank 只读采样、策略计算、模式/日志、CPU/GPU 档位发布、有界负载 |
| `drivers/ArmDTPlatform/ZizhanThermalPolicy.h` | 新增 boot-arg 解析规则（`zizhan_thermal_tune`）和负载停止状态（deadline / 75°C） |
| `drivers/ArmDTPlatform/ZizhanTsensProbe.h` | `zizhan_thermal` 非 0 时跳过旧的 one-shot `zizhan_tsens`，避免两个线程同时读 TSENS |
| `drivers/ArmDTPlatform/ArmDTPlatform.cpp` | 启动服务；定义 `pong_cpu_thermal_level` / `pong_gpu_thermal_level` |
| `drivers/ArmCpu/PongCpuPolicy.h` | 按档位收窄三个 EPSS 域的 LTU 上限（LUT 是升序，档位从最高行往下取）；触摸 boost 的 `setTop` 也受温控限制；温控下压不等负载滞后，32 ms 内生效；温控期间 L3 跟随最快域、boost 也不能抬高；档位变化时打印真实 state/selected |
| `drivers/PongAdreno/PongAdrenoPlatform.cpp` | DCVS 每轮把 `top` 收窄到 `13 - gpu_level`，`floor` 再取 `min(boot_floor, top)` |
| `tools/zizhan/tests/thermal-policy.c` | 新增 boot-arg、测试阈值、stale/invalid 保持、负载 deadline/75°C 停止的用例 |

默认行为：不写 `zizhan_thermal` 或写 `0` 时，服务不建线程，CPU/GPU 仍读档位 0，
即原策略完全不变。服务只读 TSENS 的 SROT/TM；不写 TSENS 阈值/IRQ/reset，不
改真实 panic，不发布伪造的 thermal 服务。

## 2. Boot-arg

| 参数 | 缺省 | 允许值 | 行为 |
| --- | --- | --- | --- |
| `zizhan_thermal` | 0 | 0 / 1 / 2 | 0 不启动；1 = 1 s 采样 + 策略计算 + 日志，`apply=0` 不改频率；2 = 同 1，并把 CPU/GPU 档位发布给 EPSS/DCVS |
| `zizhan_thermal_test_cpu=<°C>` | 0（默认 110） | 40..100，且必须低于默认 110 | 仅替换 CPU 封顶阈值；恢复阈值 = 测试值 − 5°C |
| `zizhan_thermal_test_gpu=<°C>` | 0（默认 95） | 40..100，且必须低于默认 95（实际 40..94） | 仅替换 GPU 封顶阈值；恢复阈值 = 测试值 − 5°C |
| `zizhan_thermal_load=<秒>` | 0 | 1..120，且 `zizhan_thermal` ≥ 1 | 首个呈现帧后再等 3 s，起 4 个内核忙线程；到时或任一有效读数 ≥75°C 立即停止 |

任何一项越界（包括 `test_gpu=95..100`、`load>120`、`load` 单独给出、`mode>2`）
都会拒绝整组参数：服务打印 `SKIP invalid-boot-arg` 后不启动，不会带着一半阈值
运行。`zizhan_thermal=0` + `load` 打印 `SKIP load-without-mode`。

命令行长度：`zizhan_thermal=2` 16 B，`zizhan_thermal_test_cpu=55` 26 B，
`zizhan_thermal_load=60` 22 B（未计分隔空格）；同一轮不要再带已被替代的
`zizhan_tsens=1`。
打包前仍按 `ci_boot_ufs.sh` 的 ≤1020 B 检查，不用 `ALLOW_LONG_CMDLINE=1`。

## 3. 日志与判读

```text
ZIZHAN_THERMAL: RUNNING mode=2 period_ms=1000 cpu_clamp=55000 cpu_release=50000 gpu_clamp=95000 gpu_release=90000 test_cpu=55 test_gpu=0 load_s=60 tsens-ro=1 apply=1
ZIZHAN_THERMAL: sample t_us=... mode=2 cpu=20 gpu=0 max_cpu=61200 max_gpu=43800 valid=ffff/ffff stale=0 reason=CPU_HOT
ZIZHAN_THERMAL: summary max_cpu=61200 max_gpu=43800 cap=20/0 mode=2 samples=10 stale=0 invalid=0 apply=1
ZIZHAN_THERMAL: load start seconds=60 threads=4 hot_stop_mc=75000 desktop=first-flip
ZIZHAN_THERMAL: load stop reason=deadline elapsed_ms=60012 threads=4
Pong CPU: thermal level 20 state <a>/<b>/<c> MHz selected <d>/<e>/<f> MHz
PONG GPU DCVS: ... level 13 (900 MHz) ...      （既有 10 s 日志）
```

- `cpu=`/`gpu=` 是策略档位（0 不封顶，CPU 20 / GPU 12 最低），`apply=` 表示是否真正发布。
- `state` 是 EPSS `0x20` 的真实时钟读回（可能还在过渡），`selected` 是刚写入 `0x320`
  的档位；封顶时两者应下降，恢复且仍有负载时应回升。
- `HOLD_STALE` / `INVALID` 表示样本不新鲜或读数不全时封顶保持；策略只有在两 bank
  都有效、该组全有效、连续 3 个新鲜样本低于恢复阈值时才降一档。
- 负载的 75°C 判据用所有有效传感器（含 CPU/GPU），每秒随采样检查一次；任一有效
  读数达到 75°C 即置停止标志并打印 `load stop reason=hot`。

## 4. 建议的设备轮（待派）

每一轮都要独立返回路径（`pong_autoreset` / panic reset / 电源键），只做
`fastboot boot` 临时启动，不写分区；失败时保留首份日志，不重跑同一输入。

| 轮次 | 命令行增量 | 观察 | 判据 |
| --- | --- | --- | --- |
| T1 待机只读 | `zizhan_thermal=1` | 桌面后待机 120 s | `RUNNING ... apply=0`；约 120 条 `sample`、12 条 `summary`；`valid=ffff/ffff`、`stale=0`；无封顶动作、无 panic、正常返回 |
| T2 有界负载 | `zizhan_thermal=1 zizhan_thermal_load=60` | 桌面后待机，负载自动起停 | `load start ... threads=4`；温度上升但采样/汇总连续；约 60 s 后 `load stop reason=deadline`；若先出现 `reason=hot`，安全停成立但 60 s 未跑满，保留首份日志，T3 仍按待机 +8 选阈值 |
| T3 封顶→恢复 | `zizhan_thermal=2 zizhan_thermal_test_cpu=<T1 待机 max_cpu/1000 + 8> zizhan_thermal_load=60` | 负载期间触发，负载结束后自然降温 | 日志出现 `cpu=20`、`reason=CPU_HOT`；`Pong CPU: thermal level 20` 的 `selected` 低于封顶前（下压 32 ms 内生效）；随后至少一次 `cpu` 递减和 `reason=RELEASE`，负载仍在时 `state/selected` 回升；全过程无 panic、正常返回 |

T3 的 `test_cpu` 必须在 40..100 且低于 110；若 T1 待机 +8 超过 100，改用 100。
从 20 档完全恢复到 0 需要 60 s 的连续新鲜冷样本；T3 的判据是**至少一次真实
封顶和一次恢复**，不是整轮回到 0。恢复后的 `selected` 取决于实际负载；若负载
已结束、CPU 空闲，频率保持低档不是失败，但档位日志仍应按 3 个新鲜样本一档地
递减。GPU 封顶在 T3 不要求触发；若要单独验，
用 `zizhan_thermal_test_gpu=<待机 GPU + 8>`（≤94）另开一轮。

“桌面出现”以首个呈现帧（`pong_zc_flipped_tag`）+ 3 s 为代理；没有该符号的精简
构建退化为 root 挂载后 5 s。这是设计选择，尚未在设备上验证；若 60 s 内没有
呈现帧，负载打印 `SKIP desktop-timeout` 并继续只读采样。

## 5. 主机证据与未完成项

- 本分支提交：`3a09da1`（服务/封顶接线）、`dbc1265`（测试阈值）、`13fa32f`
  （有界负载）、`8633de8`（主机测试）、`1c704d3`（EPSS 读回日志）、
  `2458668`（温控下压跳过负载滞后）、`817e42f`（温控结束后 L3 回到 boot top）、
  `1cbd489`（L3 复位不被共享域投票覆盖，boost 不越过温控）、`5cb1e09`
  （修正升序 EPSS LUT 的档位方向，并加 level 20/0/单调的主机回归）、
  `58c4086`（SPDX 改为仓库统一的 BSD-2-Clause-Patent）、`15bdeab`
  （daily-use 主机检查加入 thermal-policy / display-power）。
- `python3 tools/zizhan/tests/thermal-policy.py`：Mac `cc`（Apple clang 21）
  0 failure。
- `zizhan-ci:gcc14-clang` 内 `gcc-14` 与 `clang` 19 均以
  `-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined` 编译并运行，
  0 failure。
- `python3 tools/zizhan/mac/check-daily-use-host.py`（已含 `thermal-policy.py`
  和 `display-power.py`）：exit 0，约 81 s。
- **未做**：内核构建（按本轮边界由主线统一构建）、`fastboot boot`、任何设备
  轮、频率读回的真机确认。主机测试只覆盖纯策略头，内核侧改动仅做过源码审查；
  实际 EPSS LUT 行数、DCVS 档位与 4 线程的升温幅度都要等 T1–T3 的真实日志。
- **推测**：4 个 4 ms 忙/1 ms 睡的线程足以在 60 s 内形成可见温升；若 T2 的
  `max_cpu` 几乎不动，先保留日志，再讨论增加线程数或改为全核忙循环。

**未完成与后续想法（本轮不扩展）**

- 内核构建与 T1/T2/T3 仍由主线执行；本页所有“判据”都还没有设备证据。
- 实际 EPSS LUT 行数、CPU rank 落到多少 MHz、DCVS 档位与 GPU 温升，需要
  T1–T3 的真实 `state/selected` 和 `PONG GPU DCVS` 日志才能确认。
- 负载目前固定 4 线程、4 ms 忙 + 1 ms 睡；若 T2 温升不足，再评估线程数/占空比，
  本轮不改。
- “桌面”代理是首个呈现帧 + 3 s；若走 CPU 合成、没有 `pong_zc_flipped_tag`，
  负载会 SKIP。是否改用更直接的 WindowServer/服务信号留待设备观察。
- boot-arg 的格式错误可能被 `PE_parse_boot_argn` 当成 0/缺省，当前无法区分
  “未给”和“解析失败”；越界数值会被拒绝，但拼写错误会回落到默认阈值。
- 32 路逐秒全量日志、GPU 单独测试阈值轮、NTC/PMIC 传感器、thermalmonitord/HID
  发布、vendor thermal conf 解密和 suspend 都不在本版范围。

## 2026-11-19 GPU 封顶单独验证（therm-g1，静音）

`zizhan_thermal=2 zizhan_thermal_test_gpu=55 zizhan_thermal_load=60 pong_touch_synth=2`（kernel build-play31）：

- GPU 传感器到 56.8 °C 时 `reason=GPU_HOT`、`gpu=12`（最低档），之后 `PONG GPU DCVS` 一直停在 level 1（220 MHz），合成滑动照常（busy 37–46%）。**GPU 封顶生效。**
- 合成滑动持续加热，GPU 一直在 54–57 °C，没低于 50 °C 的释放阈值，所以这一轮没有看到释放（cap 一直 0/12）。
- 4 线程负载 2 s 后因 80.9 °C `reason=hot` 自动停止；CPU 最高 93.5 °C（本轮 CPU 阈值保持默认 110 °C）。无 panic。

## 2026-11-19 GPU 封顶 → 释放（therm-g2，静音）

同样的 `test_gpu=55`，去掉合成滑动，只靠 60 s CPU 负载加热（负载 8 s 后在 75.3 °C 按 `reason=hot` 自停）：

- 55.2 °C `GPU_HOT` → `gpu=12`，峰值 65.1 °C 期间一直保持最低档；
- 降到 50 °C 以下后 `reason=RELEASE` 每约 3 s 释放一档：12 → 11 → 10 → 9 → 8 → 7 → 6 → 5 → 4（本轮到时结束，未见到完全回 0）；
- 无 panic。**GPU 的封顶和逐级释放都已在设备上成立。**
