# POWER-02 S3 熄屏/亮屏：无人值守设备证据（2026-11-19）

源码：`wip/2026-11-17-audio-sensors`，合入 Codex `f5f70f3`（S3 执行器，默认关闭）为 `04938be`；
修正 `a3bbeaa`（0021 补丁在 case 内写 `extern "C"` 不能编译，声明移到 0020 的文件作用域）；
`f3a422b` 让 `zizhan_power_inject=N`（N≤10）注入 N 轮开/关，间隔 5 s。

## 设备轮（全部静音、临时启动、自动返回 Android）

| 轮次 | 参数 | 结果 |
|---|---|---|
| pwr-d1 | `zizhan_power_observe=1 zizhan_power_inject=1 zizhan_display_power=1` | 第 1 次按键：DCS 0x28/0x10 ACK，`PASS target=off`；第 2 次：0x11/0x29 ACK，`PASS target=on`；无 quarantine |
| pwr-d2 | 同上 | 同上 |
| pwr10-d1 | 同上但 `zizhan_power_inject=10`、`pong_autoreset=240` | 20 次按键，10 次 off + 10 次 on 全部 PASS；无 FAIL/REJECT/quarantine；GPU DCVS 持续；正常返回 |

熄屏前保存的亮度分别是 15 和 0：开机后 UI 已把亮度从 307 降下来，执行器只是如实保存/恢复。亮度被降到 0 是另一个问题，单独跟进。

## 还需要人在场

- 熄屏时屏幕是否真的黑、亮屏后画面和触控是否恢复（建议直接用实体电源键按 10 次）。
- S2（停止呈现）、S4（暂停触控）仍只记录不实现；锁屏、待机与功耗不在本轮范围。

## S4 触控暂停（`b03aa4d`，01:52）

熄屏成功后，触控线程先抬起仍按下的触点，再暂停上报（FIFO 照常排空）；亮屏后恢复。按键不受影响。

| 轮次 | 参数 | 结果 |
|---|---|---|
| pwr-s4 | S3 参数 + `zizhan_power_inject=10 pong_touch_synth=3` | 10 次 off/on 全部 PASS；10 次 `paused`/`resumed`，每个熄屏期丢弃合成滑动 66 个事件（一次 23）；无 FAIL |

S2（停止呈现/VSync）仍未实现；熄屏期间 GPU/合成照常运行，影响的是功耗而不是可用性。

## 组合稳定性（02:55）

daily-1..4（`zizhan_thermal=1` + S3 10 轮 + S4 + `pong_touch_synth=3` + Wi-Fi，内核 build-play19）：每轮 10/10 off/on、10/10 pause/resume，无 FAIL、无 panic，每轮 DHCP 完成；温控 mode1 采样 160，CPU 最高 67.4 °C、GPU 65.1 °C，未封顶。今晚 S3 累计 80 次 off/on 全部 PASS。
