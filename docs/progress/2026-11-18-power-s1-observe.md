# POWER-02 S1 电源键注入观测——2026-11-18

状态：**主机实现 + 主机测试**。分支 `codex/2026-11-18-power-s1`，基于
`wip/2026-11-17-audio-sensors` 的 `5c82e53`。本轮没有构建内核、没有写手机、
没有启动设备轮；P1/P2 是待派轮次，不是结果。

目标：让 selector 12（`kFBMethodRequestPowerChange`）收到一次真实的短按电源键，
只记录调用参数，不执行任何显示/电源动作。现有 selector 12 仍直接返回成功；
`zizhan_power_observe=1` 只观测，`zizhan_power_inject=1` 只把两次短按放进现有的
`/dev/pong-buttons` 队列。

## 1. 改了什么

| 文件 | 改动 |
| --- | --- |
| `drivers/ArmDTPlatform/ZizhanPowerInjectPlan.h` | 纯计划：20 s 首帧延迟、两次 120 ms 按住、两次之间 8 s；inject/observe 门控 |
| `drivers/ArmDTPlatform/ZizhanPowerInject.h` | 默认关闭的内核线程：等 root 和 `pong_zc_flipped_tag`（首帧最多 60 s），首帧后 20 s 调 `pong_touch_post` 两次 DOWN/UP |
| `drivers/ArmDTPlatform/ZizhanPowerAudit.h` | selector 12 日志增加 `selector=12 in0= scalars= proc=`；`proc_name(proc_pid(current_proc()))` 只读 |
| `drivers/ArmDTPlatform/ArmDTPlatform.cpp` | 启动 `zizhan_power_inject` |
| `tools/zizhan/tests/power-inject.c/.py` | 主机测试：默认关、缺 observe 跳过、按住 ≤200 ms、两次间隔 ≥5 s、精确时间表 |
| `tools/zizhan/tests/power-query.py` | 给提取出的 observer 补 process-name stub，并断言新字段 |
| `tools/zizhan/mac/check-daily-use-host.py` | 列表加入 `power-inject.py` |

没有改 XNU 补丁，没有写 PMIC/显示寄存器，没有调用 `pong_button_reset_hook`，
没有改真实 panic 处理。两次按住的常量是 120 ms，编译期断言 ≤200 ms；两次
DOWN 之间 8.12 s，断言 ≥5 s。注入事件不会改变 `pong_buttons.c` 轮询的 PMIC
物理键状态，因此不会触发 3 s 长按复位；即使如此，时间表仍按短按上限约束。

## 2. Boot-arg

| 参数 | 缺省 | 行为 |
| --- | --- | --- |
| `zizhan_power_inject` | 0 | 0/缺省不启动；1 且 `zizhan_power_observe=1` 时起线程；其他值 `SKIP unsupported-mode` |
| `zizhan_power_observe` | 0 | 现有只读观测；不是 1 时注入打印 `SKIP observe-required`，不注入 |

标准启动集里的 `pong_buttons=1` 必须保留：它创建 `/dev/pong-buttons`，否则
`userland/touch.c` 没有可读节点，注入事件不会进入 HID/selector 12。P1/P2 的
增量只写上面两个参数，不要移除 `pong_buttons=1`。

## 3. 注入路径与链接依据

`patches/xnu-ipados18/0001-pong-darwin24.patch`：

- `:40-42` 把 `bsd/dev/pong_input.c`、`bsd/dev/pong_buttons.c` 加进
  `bsd/conf/files` 的 `standard` 列表；
- `:1221` 定义 `pong_touch_post(uint32_t type,uint32_t finger,uint32_t x,uint32_t y)`；
- `:3939-3941` 定义 `PONG_BUTTON_DOWN=4`、`PONG_BUTTON_UP=5`、`PONG_BUTTON_POWER=0`；
- `pong_buttons.c` 的 `look()` 使用同一个调用和 code。

链接方式：`scripts/build-xnu-ipados18.sh:820-822` 用
`-Wl,-force_load,.../libArmDTPlatform.a` 把驱动归档强制链进内核；`pong_input.c`
是内核 `standard` 文件。因此驱动侧对 `pong_touch_post` 的引用由内核符号解析，
不需要改补丁，也不需要导出 kext 符号。`ZizhanPowerInject.h` 只声明该函数和
三个 code，不包含 `sys/pong_input.h`（该头不在 Kernel.framework 的
PrivateHeaders 里）。

## 4. 日志

```text
ZIZHAN_POWER_INJECT: RUNNING presses=2 hold_ms=120 gap_ms=8000 delay_ms=20000
ZIZHAN_POWER_INJECT: press=1 phase=down t_ms=...
ZIZHAN_POWER_INJECT: press=1 phase=up t_ms=...
ZIZHAN_POWER_INJECT: press=2 phase=down t_ms=...
ZIZHAN_POWER_INJECT: press=2 phase=up t_ms=...
ZIZHAN_POWER_INJECT: DONE presses=2
ZIZHAN_POWER_REQUEST: SKIP observe-only=1 selector=12 sequence=1 in0=0 scalars=1 proc=<name> structure=0 raw=0x0/0x0/0x0 hardware-transition=UNIMPLEMENTED
```

- `t_ms` 是开机以来的毫秒；同一轮里 press1 down→up 应约 120 ms，press1 up→
  press2 down 应约 8000 ms。`IOSleep` 不是硬实时，允许少量抖动；编译期常量保证
  上限，设备日志确认实际值。
- `in0` 是 `in[0]` 的十进制；`scalars` 是 `args->scalarInputCount`；`proc` 是
  `proc_name(proc_pid(current_proc()))`，取不到时显示 `unknown`。
- `raw=...` 仍是前三个 scalar 的十六进制，保留旧判读方式。
- 两次按下应各产生**一条** selector 12 日志；如果第一次把屏幕关掉、第二次
  再打开，仍按“每次注入一条请求”判读，不把 UI 状态当作 S1 完成。

## 5. 建议设备轮（待派）

每一轮都用独立返回路径（`pong_autoreset` / panic reset / 电源键 3 s），只做
`fastboot boot` 临时启动，不写分区。

| 轮次 | 命令行增量 | 判据 |
| --- | --- | --- |
| P1 首次观测 | `zizhan_power_observe=1 zizhan_power_inject=1` | `RUNNING`；4 条 down/up 日志，两个按住约 120 ms、两次之间约 8 s；每次注入后出现 `selector=12` 且记录 `in0`/`scalars`/`proc`；无 panic；正常返回 |
| P2 稳定性 | 同 P1 再来一轮 | 同 P1；比较两轮 `in0` 是否一致、是否每次注入都恰好一条 selector 12；不一致时保留两份首份日志，不重跑同一输入 |

前置检查（不通过就不要改代码）：

- 启动集仍有 `pong_buttons=1`，日志有 `Pong keys: ... power=1 ...`；
- `userland/touch.c` 已打开 `/dev/pong-buttons`（或至少该节点存在）；
- 没有 `ZIZHAN_POWER_INJECT: SKIP observe-required/no-desktop-signal/desktop-timeout`。

若 P1 完全没有 selector 12：先查 `pong_buttons=1`、`Pong keys` 和
`/dev/pong-buttons` 的打开情况；这属于输入链未接通，不是把补丁改成直接调用
display power 的理由。

## 6. 主机证据与未完成项

- `python3 tools/zizhan/tests/power-inject.py`：0 failure。
- Mac `cc`、docker `zizhan-ci:gcc14-clang` 内 `gcc-14` 与 `clang`，均以
  `-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined` 编译并运行，
  0 failure。
- `python3 tools/zizhan/tests/power-query.py`：7 tests OK（observer 新字段）。
- `python3 tools/zizhan/mac/check-daily-use-host.py`（已含 `power-inject.py`）：
  exit 0，约 80 s。
- `ArmDTPlatform.cpp` 用匹配的 Kernel.framework SDK 做 `-fsyntax-only`：exit 0
  （只有 23 条既有 SDK 警告）；这验证了新头文件的声明和 C++ 语法，不是内核构建。
- **未做**：内核构建、P1/P2、`in[0]` 的 0/1 语义确认、注入是否真的到达 HID、
  以及触控/锁屏/亮灭屏的实际状态变化。selector 12 仍返回成功，S1 只是观测。
- **未完成/后续想法**：首帧代理是 `pong_zc_flipped_tag`，没有该符号的精简构建
  会 SKIP；注入不检查 `/dev/pong-buttons` 是否有读者，队列满时旧事件会被丢弃；
  只做两次短按，不做长按/音量键；`proc_name` 可能为空或被截断；若 P1 的
  两条请求 `in0` 不一致，先记录，不据此猜 0/1 语义。
