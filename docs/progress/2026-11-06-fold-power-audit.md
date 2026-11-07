# 外屏与电源的无人值守调查

调查基于 `6d074ef09e5777cecfa23612e626ce2c3878153e` 的设备源码和本机 Android live DT。此文记录主机调查，不是设备资格。原始 DT、镜像和操作路径保留在仓库外 handoff。

## 外屏板级输入

本机 DT 选择 L18 `38_0c_0a` 的 primary/secondary 面板。不能只读取 `qcom,dsi-default-panel`：静态默认 phandle 指向另一组节点，而本次 Android 启动参数明确选择 L18。外屏专用属性还使用 `dsi-sec-*` 名称，不能用只匹配内屏 `dsi-ctrl-num` 的脚本得出外屏没有控制器。

| 资源 | 内屏 | 外屏 |
| --- | --- | --- |
| 面板 | L18 primary，2160×1916 | L18 secondary，1080×2520 |
| 控制器 / PHY | DSI0 / PHY0 | DSI1 / PHY1 |
| DSI MMIO / span | `0x0ae94000` / `0x400` | `0x0ae96000` / `0x400` |
| DT 的 MDP interface 资源 | `0x0ae36000` / `0x300` | `0x0ae37000` / `0x300` |
| 默认 timing@0 | 60 Hz，2 LM / 2 DSC / 1 DSI | 60 Hz，1 LM / 1 DSC / 1 DSI |
| 虚拟通道 | 0 | 0 |
| DCS 亮度 | 非零最小 4，最大 2047，DBV 反转 | 非零最小 4，最大 4095，DBV 反转 |
| reset / TE GPIO | 0 / 86 | 125 / 87 |
| 触控总线 | QUP SE4 SPI，`0x990000` | QUP SE19 SPI，`0x890000` |
| 触控 IRQ / reset GPIO | 21 / 20 | 119 / 52 |
| 触控坐标上限 | X 19140、Y 21600 | X 10800、Y 25200 |

外屏 timing@0 的 DSC 是 8 bpc / 8 bpp、slice 540×40、每包 2 slices；DT 有专属供电、reset、完整 on/off/PPS 和时序命令。它们不等于固件已正确交接，不能只改分辨率、复用内屏 DSI0 初始化或复用内屏 2047 亮度上限。

Hall 在本机 DT 中是 GPIO62、active-low、`EV_SW` / code 0、15 ms debounce、可唤醒。这里只确认布线声明；尚未采样实际折叠/展开电平，也未确认它与 Android `xiaomi.sensor.fold_status` 的关系。

00:50 的 Android 只读清单进一步确认 `fts-pri` / `fts-sec` 两个独立输入设备、gpio-keys 的 `SW_LID`，以及 wakeup/non-wakeup 两个 `xiaomi.sensor.fold_status` 服务。外触控运行时 ABS_MT_POSITION_X/Y 上限为 10799/25199，与 DT 的 10800/25200 范围一致。当前仅一个静止姿态，没有物理开合输入，不能据此确定 Hall 极性与折叠状态的对应或宣布切屏通过。

当前实现的边界：

- `PongAdrenoScanout.cpp` 的面板时序命令使用已验证的 DSI0 和内屏 INTF 路径。历史固件保留的外屏 buffer 地址只是内存痕迹，不能证明外屏在扫描。
- `0002-zizhan-fts.patch` 严格核对 SE4、内屏引脚和坐标。外屏是独立 SPI 控制器和输入设备，不能只改触控缩放。
- `PongBacklight.cpp` 的硬件身份检查限定真实 L18 primary。外屏需要独立板级描述及控制器所有权；不得削弱现有校验以让外屏参数混入。

最小实现顺序：先在人工固定折叠姿态下采集 Android/固件真实路由、frame/TE 和面板状态；准备独立外屏 profile 和可回退的只读观察；确认交接后再显示 60 Hz 测试图及核对实际帧计数。显示独立通过后接 SE19 外触控及坐标逆变换，最后才做排空帧、切屏、等待新屏首帧、失败恢复和 Hall 去抖。

人工前置条件：至少需要固定外屏可见姿态并观察真实出光、触控方向与边缘；自动切屏还需可重复开合。无人配合时暂停上述真机资格，板级调查可以独立完成。

## 真睡眠与显示熄屏

`userland/sleepserver.m` 是 Sleep.framework 的作息配置服务，处理 HealthKit Sleep 模型和配置存储，不是 SoC 系统睡眠入口。其名称不能用作系统 suspend 已实现的证据。

`userland/touch.c` 有 wake-detection 模式下的 consumer HID 唤醒事件，`ui_compat18.c` 有启动时的屏幕/锁屏唤醒动作。它们可以参与显示层唤醒，但不证明 CPU、GIC、UFS、GPU 或 Wi-Fi 经过系统 suspend/resume。

`ArmCpu::quiesceCPU` 关闭当前 CPU 的 timer/GIC 接口，非启动核走 PSCI CPU_OFF；`haltCPU` 最后停在 WFI；`startCPU` 使用已有 PSCI CPU_ON 路径。当前设备层没有相应的 CPU_SUSPEND / SYSTEM_SUSPEND 恢复契约，不能把 SMP 启停当作整个系统睡眠。

在当前显示、GPU、Wi-Fi、UFS 及 ADSP 的设备适配中，本次没有找到完整的 IOKit 电源状态联动和系统级恢复闭环。单个 GPU suspend 计数、RPMh sleep/wake TCS 的保留或线程阻塞不等于这些设备具备 suspend/resume。

当前 `PongBootReset.h` 使用 pinger 与 guard 内核线程，以及 Gunyah watchdog/PSCI warm reset 返回。线程定时器可能在真正系统睡眠时停止，watchdog 在目标睡眠状态下是否继续运行也未验证。因此普通启动的自动返回结果不能直接授权无人值守真睡眠。

开始真睡眠实验前，需要：

1. 明确平台睡眠状态、固件调用约定、boot CPU 恢复入口和 timer/GIC 保存恢复；先以只读功能查询建立前提。
2. 为 UFS 的在途 I/O 与 DMA、GPU ring/BO/scanout、DSI 控制器、Wi-Fi 固件和 ADSP 各自制定有界 quiesce/resume 顺序；失败时恢复真实原状态，不伪造完成。
3. 验证一个在目标睡眠状态仍有效的独立返回机制，并在有人可物理恢复时跑首次进入/退出。
4. 固定内屏先验收，再增加外屏和折叠场景；显示黑屏、低亮度或锁屏只能分别记作显示状态。

## 电池管理状态

既有证据仍有效：原生最大容量可以读取，健康页所需的 `com.apple.thermalmonitor.ageAwareMitigationState` 与 powerd `"Battery Service State"` 尚缺真实管理来源。`PongPower.cpp` 在 `pong_adsp=2` 时启动 ADSP 电池链；默认正常包的 `pong_adsp=0` 不提供这条运行时链。调查健康页前必须先核对实际启动参数，不能把编译了电池驱动当作电池服务已经运行。

本轮未注入 notify 状态、健康属性或成功标志。电池管理调查独立于系统睡眠，后续应启用已验收 ADSP 配置，再分别检查 thermalmonitord/powerd 实际输入、服务状态及其失败原因。
