# 2026-11-19 晚：日用默认、外屏触控协议、蓝牙串口、首帧分析（全部静音，未写分区）

用户 19:30 要求：fairplayd 先调研（暂不接数据源）、日用配置改为默认、推送、按计划继续无人值守，看首帧 80 ms，有时间看蜂窝。

## 推送与 CI

- UniXNU-zizhan `wip/2026-11-17-audio-sensors` 推到 `7bfe8a2`；uefi-zizhan `claude/2026-11-19-m3-handoff-reset` 推到 `811f13f`（推前把 5 个提交的作者改成 GitHub noreply 地址，树内容不变，原提交留本地 tag `pre-pub-e62bef3`）。
- 首次推送 CI 两项失败，均已修：GCC 才报的告警（单行 if 未加括号、枚举与无符号混用的条件表达式、测试桩 `extern` 带初值），以及 porting.toml 未随代码更新显示/输入两块证据。修后 Display、Daily-use、Wi-Fi 通过；Porting record 因 `ZizhanAudioPrm.cpp` 归属 power 块又要求补记录，本轮一并补。

## 日用配置默认开启（`zizhan_daily`）

- `drivers/ArmDTPlatform/ZizhanDailyProfile.h`：参数缺省时温控 mode 2（CPU 90 / GPU 85 °C）、熄屏亮屏、电源请求观测、合盖跟随 mode 2、外屏时钟默认开；显式参数优先；`zizhan_daily=0` 恢复全关。测试注入器（熄屏注入、模拟合盖、音频）不在其中。
- 命令行从 1023 字节降到 864 字节。
- daily-10（build-play46，只带 `zizhan_power_inject=10`）：`ZIZHAN_DAILY: profile=1`，温控 mode 2 与合盖跟随自动启动；但注入器自己直接读 `zizhan_power_observe`，报 `SKIP observe-required`，熄屏轮数 0。
- 修正后 daily-11（build-play48，同样参数）：熄屏/亮屏 **20/20 PASS**、0 FAIL，回到 Android。

## 外屏触控 FTM5

- 对照小米 sm8475 开源内核（mayfly-s-oss）里的 ST `fts_spi` 驱动：芯片 ID 0x3640 对应源码中的 SALIXP 型号；**SPI 接口读事件 FIFO 的命令是 0x87**（0x86 是 I2C 用的），后跟 1 个填充字节和 8 字节事件。outer-5/6 用了 0x86，所以读回全 0。
- outer-7（build-play48，`zizhan_outer_probe=5`）：
  - 上电并释放复位后中断线拉低；读出 `03 00 …`（控制器就绪），读后中断线变高。
  - 发 `A0 00 01`（驱动 resume 时用的 ACTIVE 扫描设置）后中断线又变低；读出 `43 05 01 …` 和 `43 01 a0 00 01 …`（状态事件，后者回显了扫描命令），读后中断线变高。
  - 结束后供电与引脚恢复，回到 Android。
- 结论：**外屏触控的事件协议已闭环打通，不需要手指就能证明**。下一步：把外屏触点事件（0x13/0x23/0x33）接进触摸输入，需有人在场摸外屏验证坐标。

## 蓝牙串口（BT-01 第 3 步，已编译，未上机）

- 静态核对：bluetoothd 和 BlueTool 都调用 `tcgetattr/cfmakeraw/cfsetspeed/tcsetattr`，并用 select/kevent，所以需要真正的 tty。
- XNU 补丁 `0023-zizhan-bt-uart.patch`：新增 `bsd/dev/zizhan_bt_uart.c`，用 XNU 自带 tty 层（ttymalloc、线路规程、ttselect；`D_TTY` + `CDEVSW_SELECT_KQUEUE` 走 tty 的 kqueue 过滤器），节点 `/dev/cu.bluetooth` 与 `/dev/uart.bluetooth`。只有 UART 一侧调用 `zizhan_bt_uart_attach()` 时才创建节点。
- `ZizhanBtProbe.h` 新增 `zizhan_bt_probe=4`：沿用 mode 3 的固件下载，之后不关电，把 UART 交给 tty：发送走单生产者环形缓冲区（tty 先查剩余空间，只取放得下的；缓冲区满后排空时再触发续发），接收每 2 ms 轮询一次后送入 tty。
- build-play49 编译通过，内核含 `ZIZHAN_BT_UART` 字符串。**没有上机**：节点一出现，bluetoothd 就会用它初始化控制器并很可能打开射频，按约定要先问用户。

## UX-01 首帧约 80 ms：分析，暂不改

- 历史数据（g363/g377/g379，合成静止按下）：按下→Began 约 25 ms，Began→交换缓冲区约 37–42 ms，交换→面板约 12 ms；中位数约 74–80 ms。
- g379 的 GPU 跟踪：Began 到 backboardd 首个 GPU 提交约 39 ms，GPU 执行约 6 ms。慢在 CPU 侧的渲染准备。
- 已排除：L3 已投最高档；触摸期间 CPU 已升频，实测频率正常（11-01 第 12 节）。
- 新发现：内屏触控线程每 8 ms 查一次中断线（`0002-zizhan-fts.patch`，`sleep_ms(8)`），真实手指平均多约 4 ms；合成触摸不经过这里，所以上面的 74 ms 不含它。可做的小改进：中断线变低后在触摸期间缩短轮询间隔，或改成真正的 GPIO 中断。
- 按 11-06 的验收规则（同 OS/内核、单变量、至少 15 个样本、中位数降 10% 且 p90 不退步）才接受任何改动，本轮不改代码。

## 研究文档

- [fairplayd：其他项目的做法与静态核对](../research/2026-11-19-weather-fairplay.md)：没有项目在非苹果硬件上让 fairplayd 正常工作；WeatherDaemon 的 JWT 请求要 Mescal（基于 FairPlay）签名。天气保持挂起。
- [蜂窝可行性](../research/2026-11-19-cellular-feasibility.md)：QcomBaseband 在 SC7180 + iOS 17 上已做到 LTE 注册和 IPv4 数据；本机 ADSP 的 PAS 启动、GLINK、QRTR 可复用；阶段 1（启动 MPSS、只读 rmtfs、列 QMI 服务、保持低功耗不发射）可无人值守，阶段 2 需用户决定。

## CI 精简内核链接修复

推送前按惯例补编 `pong_ipados18_ci_defconfig`（关掉 Adreno 与 PongPower），发现链接失败：zizhan 服务弱引用了背光、DSI、扫描输出标记和 GENI I2C 函数，而静态内核链接不允许未定义的弱符号（11-18/19 引入，云端 macOS 内核 CI 需手动触发，所以之前没暴露）。`ArmDTPlatform.cpp` 在缺少对应组件时提供替身定义（扫描输出从不翻页、显示/背光请求一律失败），外屏触控盘点在没有 PongPower 时记 SKIP。CI 与 GPU（build-play50）两种内核都已链接通过。

## 深夜（用户 20:30 授权：蓝牙可以开射频、首帧找优化、蜂窝先做不需要 SIM 的部分；全部静音，未写分区）

### 蓝牙 BT-01

- bt-1：内核侧全通（固件下载、`/dev/cu.bluetooth` 创建），但 bluetoothd 启动约 95 ms 就 SIGSEGV，从没打开串口。
- 补丁 0024 把崩溃捕获扩到 EXC_BAD_ACCESS 和 bluetoothd/BlueTool；bt-2 的回溯与反汇编完全对上：`MGGetProductType()` 未知（`IODeviceTree:/product` 没有 `product-id`，MobileGestalt 的 437 项设备表查不到），bluetoothd 的控制器平台对象为空。
- 只改 `hw.product`/`hw.machine`（bt-3）无效。补丁 0025：仅在 `zizhan_bt_probe>=4` 时、仅对 bluetoothd/BlueTool 返回本 IPSW 里 J171（iPad7,11）的 `product-id`，其他进程不变。
- bt-4：**bluetoothd 不再崩溃**，常驻并调用 BlueTool。BlueTool 约 31 s 后 exit(1) 循环；`applog=1`（bt-6/7）显示 `marconi-bt: Unable to match bluetooth service in device tree`、`Device not open yet`。
- bt-7：ADT 节点改成 J171 形状（`bluetooth,n88`、transport-encoding 3 等）仍不匹配。BlueTool 需要的是 IOKit 服务 `AppleBluetoothModule`（真机由同名驱动匹配该节点发布，BlueTool `IOServiceOpen` 后用它上电/复位）。**下一步**：从 J171 内核缓存反推 AppleBluetoothModule 的用户客户端方法，做一个兼容服务。

### 首帧 UX-01：对照结果

| 轮 | 内核 | 变量 | 空闲首帧 median / p90 | Began→swap median |
| --- | --- | --- | --- | --- |
| ux-a | play50 | 基线 | 83.6 / 92.3 ms（14） | 36.2 ms |
| ux-a2 | play52 | 基线重复 | 82.2 / 86.4 ms（12） | 35.6 ms |
| ux-b | play50 | `pong_cpu_boost_ms=60000` | 作废：7 s 内 CPU 58→90 °C，温控压到最低档 | — |
| ux-c | play52 | `fb_swapwait=0` | 85.3 / 97.9 ms（25） | 45.5 ms，p90 393 ms |

- 基线可复现（±2%）。`fb_swapwait=0` 更差，否决；一直满频会烫到触发温控，否决。
- 合成触摸按下后分步移动，“按下→识别”的 ~28 ms 主要是合成节奏；可优化的是 Began→swap（~36 ms，CPU 侧渲染准备）与 swap→面板（~12 ms）。
- 发现的体验问题：温控一触发就压到最低档、每 ~3 s 放一档，持续高负载时界面会卡约 1 分钟；可改为分级下调。
- **下一步**：细分 SpringBoard 提交到 backboardd 首次 GPU 提交这一段；内屏触控 8 ms 轮询可改为触摸期间更短或真正的 GPIO 中断（真实手指约省 4 ms）。

### 蜂窝 MODEM-01

- 阶段 1a（modem-1，`zizhan_modem=1`）：UFS LUN 4/5 只读 + FAT 读取器，modem_b 的 30 个文件/108 MB 用 8.8 s 读出，哈希与 Android 文件一致；LUN 5 找到 modemst1/modemst2/fsg/fsc。
- 阶段 1b（`zizhan_modem=2`，`pong_adsp=0`）：
  - modem-2…5：拼接固件、电源投票（cx/mss NOM）、AOSS load_state、SMP2P 都通过，但 `PAS_INIT_IMAGE(4)` 返回 0xffcfffbb。
  - Android 日志显示 MPSS 的元数据来自 `linux,cma`（4 GiB 以下）；我们的物理连续缓冲在 0x86d…（4 GiB 以上），而 4 GiB 以下的连续分配会失败。
  - modem-6：元数据放进 iPadOS 不用的 CVP 保留区 0x9ee00000 后，**INIT_IMAGE、加载 108 MB（3.6 s）、AUTH_AND_RESET 全部成功，122 ms 后基带报 ready=1、handover=1**：基带第一次在 iPadOS 下启动。
  - 随后内核 panic：`preemption disable timeout`，回溯在 `IOMobileFramebuffer::show → PE_initialize_console → gc_enable`。modem-1…5 同样 `pong_adsp=0` 却没有出现，推测是基带启动后 DDR 带宽不足（我们没投 Linux 那张 `rproc_ddr` 票）拖慢了显示路径。panic 后自动回到 Android。
  - **下一步**：补 `rproc_ddr` 带宽投票；诊断轮放宽抢占阈值；然后 rmtfs（EFS 只读、写回只留内存）和 GLINK 上的 QRTR。全程未发 QMI、未上线，无射频发射。

