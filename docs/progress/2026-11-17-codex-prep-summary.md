# 今晚分支收口：codex/2026-11-17-m2-m3-prep（2026-11-17）

窗口：19:55–21:50（UTC+8）。主题从 M2/M3 方案，转到 POWER-01 输入调查与 S1/温控候选核心。**没有设备轮、fastboot、重启、安装、分区写或内核构建。**

## 提交

| 短 SHA | 一句话 |
| --- | --- |
| `592d728` | M2 温控/熄屏方案与 M3 双系统引导评估初稿 |
| `2a45437` | 更正 data-a-live 温控引用、补 A 方案工作量 |
| `ba15c6b` | 更正 ButtonsDxe 输入，补 A 方案 FDT 等价性 |
| `bace20b` | 用 initrd 尺寸证据确认 ABL 把 bootconfig 放在尾部 |
| `2965d6d` | 新增 THERM-01 策略核心与主机测试 |
| `7851e8f` | 修复 stale/backwards 不封顶的 fail-unsafe 问题 |
| `ae20907` | 定位 POWER-02 S1 的 framebuffer power 请求路径 |
| `6500692` | 新增 S1 display-power 请求状态机与测试 |
| `1a3d828` | 反汇编 powerd/thermalmonitord 的健康输入链 |
| `ffe5c6a` | 补电池健康页真实来源对照（Android/battmgr） |
| `fb0f5e0` | DSC 确认 selector 12 只带一个 scalar；分类规则未改 |
| `e6f62b5` | 用 g396 真实 32 路读数回放温控核心 |
| `7bba6f6` | 写出 THERM-01 CPU/GPU/采样接线设计 |

## 交付与证据

| 交付 | 证据级别 |
| --- | --- |
| `docs/plan/2026-11-17-m2-thermal-and-screen-off.md`：M2 方案、S1 落点/参数、THERM-01 接线 | 源码 + 设备只读 + 反汇编 + 推测 |
| `docs/research/2026-11-17-m3-bootmenu-port.md`：boot 布局、data-a-live、ButtonsDxe、FDT/initrd | 源码 + 设备只读 + 静态分析 |
| `docs/research/2026-11-17-battery-health-inputs.md`：powerd/thermalmonitord 输入与真实来源 | 源码 + 设备只读 + 反汇编 + 字符串推断 + 推测 |
| `drivers/ArmDTPlatform/ZizhanThermalPolicy.h` + `tools/zizhan/tests/thermal-policy.*` | 源码 + 主机验证（Mac cc、gcc14-clang、ASan/UBSan；g396 回放） |
| `drivers/ArmDTPlatform/ZizhanDisplayPower.h` + `tools/zizhan/tests/display-power.*` | 源码 + 主机验证（双环境，0 failure） |

## 审阅后已更正

- `progress.md` 早期时间段曾写成 20:06–21:30；已按真实 `date`/commit 时间回填并加说明。
- 漏查 data-a-live `wsl/` 温控实现；已读 `PandoraThermalHID.cpp`/`pandora_therm.c` 等并重写 M2 §1.4/§1.5。
- 曾误判 zizhan 固件“没有按键驱动”；已更正为 `ButtonsDxe` 存在，音量映射仍待设备确认。
- 温控核心 stale/backwards 样本曾不封顶；已修成“只阻止解除，不阻止封顶”，并加三类负控制。
- “PM830 GPIO6”笔误已改为 PM8350 GPIO6。

## 仍未验证

- M3 A/B 未实现；T1 未跑。入口 EL、CNTVCT、PMIC 读键、ABL 146.4 MiB field 上限均未知；ButtonsDxe 的 zizhan 音量映射未实测。
- THERM-01 未接线；CPU 百分位映射、L3、GPU top/floor 未审查；无设备负载/封顶/恢复。
- POWER-02 S1 未接线；无真实熄屏/亮屏；S1 `in[0]` 的 0/1 on/off 语义未确认。
- POWER-01 未设备确认：`Battery Service Flags` 不能由当前 battmgr/sysfs 直接提供；BatteryDataCollectionReady、UPOCount/WRa 来源未知。

## 下一步（M2 → M3）

| 顺序 | 动作 | 用户在场/授权 |
| --- | --- | --- |
| M2-1 | 实现 `ZizhanThermalService` + CPU/GPU clamp，默认关；先过主机测试 | 不需要用户；内核构建在授权窗口 |
| M2-2 | `zizhan_thermal=1` 60 s 只读诊断，再 `=2` 有界负载；验证封顶/恢复 | 需授权临时 `fastboot boot`；建议用户在场看返回 |
| M2-3 | 接 S1 display-power，先只验证 accepted/no-op/reject 日志 | 需授权 `fastboot boot`；无需分区写 |
| M2-4 | 首次真实熄屏/亮屏 10 次 | **必须用户在场** |
| M2-5 | POWER-01 只读 ioreg/notify/log 诊断 | 需授权临时启动；用户在场看 iOS 输出 |
| M3-1 | A 的 T1：UEFI + linux 条目 + live FDT + 预拼 initrd，`fastboot boot` | 需授权；建议用户在场 |
| M3-2 | A 的 T2/T3：iOS 格子、写 boot_b | **需用户书面授权写分区**，必须用户在场 |
| M3-3 | A 失败则 B 的 T1（裸机菜单 PMIC/EL/CNTVCT） | 需授权 `fastboot boot`；用户在场 |

边界：不碰音频；不改主工作树/`work/m1-firstsound/`；不 push；Apple 二进制不进 git。
