# M2 设备证据：THERM-01 接线与 POWER-02 S1 观测（2026-11-18）

上接 [M2 方案](../plan/2026-11-17-m2-thermal-and-screen-off.md)、[THERM-01 接线记录](2026-11-18-therm01-wiring.md)、[S1 观测记录](2026-11-18-power-s1-observe.md)。全部静音临时 boot，不写分区，均约 198 s 正常回 Android、无 panic。

## THERM-01（内核 f79fda1c，wip 5c82e53）

| 轮 | boot-arg | 结果 |
| --- | --- | --- |
| therm-t1-1 | `zizhan_thermal=1` | 161 个 1 s 样本，两 bank `valid=ffff/ffff` 为主，stale=0；`invalid` 161 中 12（偶发单个传感器缺一拍，策略按“不解除封顶”处理）。待机 CPU 约 51°C、GPU 约 50°C；**开机过程 CPU 峰值 88.4°C**（当前无温控时的真实情况）。apply=0，cap 始终 0/0。 |
| therm-t2-1 | `=1 zizhan_thermal_load=60` | 首帧时 CPU 已 85.2°C，负载在启动瞬间被 75°C 安全停（`load stop reason=hot … elapsed_ms=0`）。安全停成立；负载本身未跑起来（开机余热）。 |
| therm3-t3-1 | `=2 zizhan_thermal_test_cpu=60 zizhan_thermal_load=60` | **封顶→恢复成立**：90.7°C `reason=CPU_HOT` → level 20，`Pong CPU: thermal level 20 state 1804/2457/2707 MHz selected 300/633/787 MHz`；降到 55°C 以下后每 3 s 降一档 `RELEASE` 19→11，`selected` 随之回升（如 level 11 小核 1056 MHz）。负载同样被开机余热触发安全停。 |

结论：采样、封顶、逐级恢复和负载安全停在设备上都成立。待改：负载应在温度回落（如峰值 < 60°C）后再起，否则测不到“负载→升温→封顶”；默认阈值（110/95°C）下的真实负载封顶仍未验证。

## POWER-02 S1 观测（内核 52140311，wip 合入 codex/2026-11-18-power-s1）

`zizhan_power_observe=1 zizhan_power_inject=1`，两轮结果完全一致：

| 时刻 | 事件 |
| --- | --- |
| 开机 | `selector=12 in0=1 scalars=1 proc=backboardd` |
| 第 1 次注入短按（按住 120 ms） | `in0=0`（backboardd） |
| 第 2 次注入短按（8 s 后） | `in0=1`（backboardd） |

结论：`kFBMethodRequestPowerChange` 只带一个 scalar，**`in0=1` = 亮屏、`in0=0` = 熄屏**，调用方是 backboardd；短按电源键确实走到这里。驱动仍只记录、返回成功（`hardware-transition=UNIMPLEMENTED`）。下一步 S2/S3：按 in0 做真实的停呈现/面板关与恢复（首次真实熄屏需用户在场看屏幕）。
