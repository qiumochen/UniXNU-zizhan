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

## 凌晨（11-20 00:00 起，用户授权的无人值守计划；全部静音，未写分区）

### 蜂窝 MODEM-01：基带完整初始化，读出固件版本（离线模式）

| 轮 | 改动 | 结果 |
| --- | --- | --- |
| modem-7 | 诊断轮 `sched_preemption_disable_debug_mode=1`，观察 60 s | 不再 panic；基带运行 40 s 后自身看门狗报 `DOG detects stalled initialization` |
| modem-8 | GLINK（SMEM 478/479/480，IPCC 客户端 2 信号 0）+ IPCRTR 通道 + QRTR v1（APPS 节点 1） | HELLO 交换；基带公布 SSCTL（43）与 servreg 通知（66） |
| modem-9 | 最小 pd-mapper（服务 0x40，实例 0x101），按 `modemr.jsn` 回答 `tms/servreg` → `msm/modem/root_pd`（实例 180） | 基带两次查询（tms/servreg、tms/pddump_disabled）；40 s 仍看门狗 |
| modem-10/11 | rmtfs（服务 14）：2.5 MB 共享缓冲放 CVP 保留区，`MP_ASSIGN` 给 HLOS+MSS_MSA 读写（成功）；QRTR 实例须为 0x1（version 1 instance 0） | 基带打开 modem_fs1/fs2、申请缓冲；首个读请求因 iovec 地址理解错被拒 → 基带报 `rmts_read_iovec failed` |
| modem-12 | iovec 的 `phys_offset` 是绝对物理地址 | **初始化完成**：rmtfs 读 2.5 MB，基带公布 45 个 QMI 服务（WDS 1、DMS 2、NAS 3、WMS 5、VOICE 9、UIM 11、LOC 16、WDA 26、PDC 36、DPM 47 等），打开 DS/DATA1–4 等通道；60 s 无看门狗；停止请求 0.5 s 应答 |
| modem-13 | 只读 QMI DMS：Get Revision、Get Operating Mode | 固件 `MPSS.DE.2.0.c1-00108-WAIPIO_GEN_PACK-1.16412.27.18599.21`（2023-02-24），工作模式 3 = OFFLINE |

- EFS 分区（LUN 5）整块读进内存影子，基带写回只改内存；没有写任何存储。
- 全程未发会改变状态的 QMI（没有 Set Operating Mode、NAS、WDS），基带离线，无射频发射。
- 下一步（阶段 2，需插 SIM 并经用户同意）：UIM 读卡状态、NAS 注册、然后接 CommCenter 桥（QcomBaseband）与 IPA 数据面。

### 蓝牙 BT-01：卡在 BlueTool 的内置初始化流程

- J171 的 AppleBluetooth 驱动类是 `BTReset`，提供 `/dev/btreset|btwake|btpoweroff`（打开时拉 GPIO，关闭时放开）；`AppleBluetoothModule` 只用于 PCIe 蓝牙，与本机无关（更正前一节）。
- 已加上三个控制节点（只记录），名为 `bluetooth` 的 nub 已发布并注册；BlueTool 仍只打印 `Device not open yet`，从未打开节点或串口。
- 本 IPSW 没有 `/etc/bluetool` 脚本和 `.hcd`，BlueTool 用二进制内置脚本按芯片选择流程；需要它的 os_log（选平台/芯片）才能继续，下一步先把 BlueTool 的 os_log 导到控制台。

### 温控分级（THERM-01 体验改进）

- 策略：每个过热读数多压 4 档（`zizhan_thermal_step`，0 = 旧的一步到底），超过阈值 +10 °C 才直接压到最低档；释放仍是每 3 个读数放一档。
- therm-s1（危急余量 5 °C，测试阈值 CPU 70 °C、90 s 负载）：负载一开 1 s 内 63→75.7 °C，直接越过危急线压到最低档 → 余量太窄。
- therm-s2（余量 10 °C）：76.1 °C → 4 档、73.8 °C → 8 档、70.2 °C → 12 档，之后放到 10 档并在持续负载下稳定在 65–67 °C，**没有再一下压到最低档**。主机测试覆盖逐级、滞回保持、危急一步到底、GPU 饱和和 step 0。

### 本轮未做

- 首帧细分（SpringBoard 提交 → backboardd 首次 GPU 提交）需要给 CoreAnimation 加测量点，留到下一轮；内屏触控轮询改短需要真实手指才能测出效果，也留待在场。
- 蓝牙按“连续多轮无进展就换方向”的规则挂起，下一步见上文。

## 11-20 第二轮无人值守（用户确认的 8 小时计划；全部静音，未写分区）

### 蓝牙：iOS 18 的串口蓝牙是内核控制，不是 tty（仍挂起）

- 反汇编 BlueTool 的 `device -D`：`socket(PF_SYSTEM, SOCK_STREAM, SYSPROTO_CONTROL)` → `CTLIOCGINFO("com.apple.uart.bluetooth")` → `connect`，数据走 read/write，波特率等用 `getsockopt/setsockopt(SYSPROTO_CONTROL, TIOCGETA/TIOCSETA)` 传 termios。
- 补丁 0023 注册了同名内核控制（非特权、流式），收发接到现有 UART 桥；UART 桥加了厂商命令垫片：主机发来的 OGF 0x3f（博通私有）命令直接回 Command Complete，不转给 QCA6490。
- bt-9：内核控制注册成功（id 11），但 BlueTool 从未连接；它自己“判断传输方式”的日志也没出现，说明 `device` 命令根本没执行。BlueTool 在开机约 3 s 就运行，而内核控制要等固件下载完（约 3 min）才注册，很可能第一次失败后整段脚本就中止了。
- 已连续 5 轮无实质进展，按规则挂起。下一步：①开机即注册内核控制，固件就绪前先缓存；②给 BlueTool 的 socket/open/connect 加内核跟踪，看清它真正走的路径。

### 蜂窝：基带稳定运行 8 分钟，不再需要放宽抢占阈值

- modem-6 的 panic 根因：基带加载 108 MB 正好撞上第一帧上屏时的控制台释放（`beginGraphics` 一次性清屏，关抢占），清屏超过 6 ms 阈值。基带改为等到第一帧上屏后再过 10 s 才启动。
- modem-16（play69，正常内核参数，观察 480 s）：基带一直 ready，GLINK/rmtfs 稳定，最后 stop-ack 0.5 s，CPU 最高 55 °C，无 panic。日志环形缓冲在长时间运行中覆盖了开头，DMS/UIM 应答见 modem-13。
- 新增 `zizhan_modem_watch_s`（30–1800 s）；监督脚本要求 `pong_autoreset < 900`。

### 首帧 UX-01：GPU 时间线对齐后的分段

- ux-d：跟踪窗口按 SpringBoard+40 s 开，但合成触摸要到开机约 221 s 才开始，没有重叠。ux-e 改为 `pong_gtrace_delay=185`，窗口 225.7–263.8 s 覆盖 8 次手势（跟踪本身有开销，总时延 88.6 ms 只看比例，不作性能比较）。
- 逐次拆分（Began → backboardd 第一次 GPU 提交 / GPU 执行 / 交换请求）：

| Began→提交 | GPU 执行 | Began→交换请求 |
| --- | --- | --- |
| 19.8 | 2.2 | 20.2 |
| 43.1 | 10.0 | 44.0 |
| 19.5 | 8.6 | 20.2 |
| 43.7 | 9.9 | 44.6 |
| 20.4 | 5.9 | 20.9 |
| 35.6 | 12.2 | 35.7 |
| 20.2 | 8.5 | 20.3 |
| 42.8 | 9.9 | 43.0 |

- 结论：慢在 CPU 侧（SpringBoard 处理手势 + CoreAnimation 提交 + backboardd 准备），GPU 执行只有 2–12 ms；交换请求紧跟首个提交。CPU 段**交替**为约 20 ms 与约 43 ms，合成手势是左右交替翻页，说明翻向某一页时工作量翻倍，与内核开关无关。
- 下一步：确认慢的是哪一页（翻向小组件页还是图标页），再看那一页在 SpringBoard 里做了什么（图片解码、布局）。本轮不改代码。

### 蓝牙：系统调用跟踪证明卡点在传输层之上

- 诊断补丁 0026（仅 `zizhan_bt_probe>=4`、仅 BlueTool/bluetoothd、每进程最多 200 行）记录 socket/connect，以及 /dev 与 bluetooth/uart 相关路径的 open。
- bt-11：BlueTool 被拉起 15 次，bluetoothd 读了蓝牙配置文件，但**两者都没有调用过 socket，也没有打开任何 /dev 节点**。内核控制、tty、btreset 节点都还没轮到被用到。
- 因此问题在更上层的平台/初始化路径选择。bt-10 里 BlueTool 与 bluetoothd 都读 MobileGestalt 缓存文件，平台判断可能来自缓存而不是我们按进程给出的 product-id。下一步：反推 BlueTool `-R` 启动时的平台判断，核对 MobileGestalt 缓存内容。

