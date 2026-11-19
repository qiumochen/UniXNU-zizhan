# 当前进度与接续入口

更新：2026-11-19（UTC+8）。本页只保留**当前**状态，限一页。11-17 之前的完整台账原文在 [2026-11-17-status-archive.md](2026-11-17-status-archive.md)；规划依据见 [接手梳理与重新规划](../plan/2026-11-17-takeover-replan.md)。管理规则见 [AGENTS.md](../../AGENTS.md#progress-management)，本机操作细节在仓库外 `$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`。

## 推进规则（11-17 起）

- 一条主线 + 最多一条纯主机支线。主线按**用户能感知的功能**验收，“限定 PASS”只算过程证据。
- 里程碑最长 1 周，结束时做一次 ≤30 分钟的集中用户验收；需要人在场的步骤提前约时间。
- 同一问题连续 6 次设备轮或 1 天没有用户可见进展 → 停下复盘，换假设或挂起，并写在本页。
- 每天收尾在 Mac 上按主题本地提交；里程碑结束后推送。本页每次更新都要保持一页。

## 基线

| 项目 | 状态 |
| --- | --- |
| 源码 | `main` = `034a0b0`（11-09 发布）；11-10～11-17 的工作在分支 `wip/2026-11-17-audio-sensors`。实验分支 `feature/native22g100-arm64e` 挂起。 |
| 启动方式 | Mac 上 `fastboot boot` 临时启动（后部 USB-A），默认 300 s 自动返回、电源键 3 s 回 fastboot。离开 Mac 不能进 iPadOS。 |
| 手机安装 | iPadOS 18.7.10 / 22H374 用户态 `statusbar-work-v1`（OS `5c0912…`、UI `f2d639…`）；回退链和 10-28 全分区备份保留。 |
| 能用 | 内屏桌面、触摸、GPU + 120 Hz + 零拷贝、Wi-Fi（选网/自动加入/2.4↔5 GHz）、手动亮度、电量和充电显示、设置、状态栏电池在右上角。 |
| 不能用 | 声音（数据已到功放，待试听）、外屏触控、传感器/定位、蓝牙射频、相机出图、蜂窝；应用内容只部分可用。熄屏/亮屏、合盖跟随、外屏显示、温控已有实现与无人值守证据，默认关闭。 |

## 当前里程碑

| ID | 里程碑 | 状态 | 验收 | 下一动作 |
| --- | --- | --- | --- | --- |
| M0 | 收口 | DONE（11-17） | 8 天未提交工作按主题提交并推到 wip 分支；本页一页；20 h 无人值守窗口关闭 | 已完成：分支 `wip/2026-11-17-audio-sensors`，`a044488` 的 Porting record 44075853448、Daily-use 44075853465、Display 44075853503、Wi-Fi 44075853551 全部 SUCCESS（macOS 云端内核为手动触发，本次未运行）。中途修了 GCC 才报的告警（只加括号/类型转换，无行为变化）。 |
| M1 | 能出声 | DOING（主线） | 用户在场：系统提示音能听到、一个 App 播放能听到、音量键能调 | 11-17 晚在场轮的间歇整机挂死已查清并修复：根因是音频 PRM 工作线程 16 KiB 内核栈溢出（功放回执大帧 + IOLog 深调用链），不是 ADSP/SMMU。修复后无人值守静音验证 18/18 轮 0 挂（修复前约 50% 挂），60 条 WRITE 全部 10–27 ms 回执。11-19 用引脚电平采样证实数据已到功放（GPIO124 有数据、功放回传 GPIO122 有数据）；之前听不到最可能是电平太低（增益 5.5 dB vs Android 18.5 dB、音量更低、左右反相）。`zizhan_amp_lvl=1`（Android 增益/音量 + 同相）空跑通过。下一步（需在场，约 5 分钟）：试听 `loud-1`（1 kHz、−18 dBFS、1 s） |
| M2 | 外屏 + 折叠 + 熄屏/亮屏 | DOING（11-18 用户定为 M1 之后第一优先） | 外屏能显示与触控、开合自动切屏；熄屏/亮屏 10 次循环可触控 | 已有：Hall 只读观察、S1 观测；**S3 熄屏/亮屏执行器 11-19 无人值守 10 轮开/关全部 PASS**（[证据](2026-11-19-power-s3-device.md)），待人工看屏与触控。11-19 新增 `zizhan_hall_observe=2`：合上内屏按一次电源键熄屏、打开再按一次亮屏（无人值守 0 误触发），待在场折叠验证；外屏资料与只读探针已备（[外屏](../research/2026-11-19-outer-panel.md)）。11-19 晚：模拟合盖全链路闭环（fold-2），外屏能显示（MISR），外屏触控芯片 FTM5 已应答；剩真实折叠（fold-3）与外屏触控事件协议 |
| M3 | 离开电脑启动 | DOING（11-18 用户授权写一次 boot、先做好备份、默认进安卓） | 不接电脑开机：默认 10 s 进安卓，菜单可选 iPadOS；安卓数据与撤销路径保留 | A′（mi8-dualboot-lab 式 magic + warm reset 交接，见 docs/research/2026-11-18-m3-warm-reset-handoff.md）：今天只读备份 + 主机构建测试镜像；写 boot_a 并切槽在用户在场时做，boot_b 原样回退 |

## 后续优先级（用户 2026-11-18 定）

M1 声音 → M3 离开电脑启动（并行，需在场写 boot）→ **外屏 + 折叠 + 熄屏/亮屏** → **天气 App** → **相机** → **温控与调度** → **蓝牙**。温控 THERM-01 已有默认关闭的实现与设备证据（封顶/恢复成立），排到第 4 位前保持默认关闭。

## 挂起（用户点名才恢复，恢复时从对应记录续）

| ID | 停在哪里 | 记录 |
| --- | --- | --- |
| APP-01 天气 | 有城市名，无温度/预报；卡在时区、定位、fairplayd、WeatherKit 认证 | [11-09 交接](2026-11-09-weather-handoff.md) |
| 22G100 原生迁移 | 两次早期内核启动；544 个导入缺口 | `feature/native22g100-arm64e` |
| SENS-01 / GNSS-01 | SLPI 固件/IPCC/cmd-db 已就绪，六项带宽基线未知，未启动 SLPI | [11-16 记录](2026-11-16-slpi-transport-resume.md) |
| HALL-01 / FOLD-01 | Hall 静态只读基线通过；外屏未实现 | [Hall](2026-11-15-hall-observer.md) |
| THERM-01 | g396 两组 TSENS 读数有效；无热策略（M2 恢复） | [TSENS](2026-11-07-tsens-readonly.md) |
| POWER-02 | 设计和只读审计完成；无真实熄屏（M2 恢复） | [方案](../plan/2026-11-07-lockscreen-suspend-design.md) |
| NET-01 / NET-02 | 同网 28 分钟有界窗口通过；换网、自然 T1、MSI 未验 | [11-06](2026-11-06-overnight.md) |
| POWER-01 | 电池健康页仍转圈（缺 thermalmonitord / powerd 状态） | [11-06](2026-11-06-daytime-input-power.md) |
| UX-01 | 主屏静止首帧约 80 ms | [11-06](2026-11-06-overnight.md) |
| BOOT-01 / GPU-01 | 罕见 CS4K 分配失败与画布映射失败，未复现 | [11-06](2026-11-06-boot-release-followup.md) |
| BT-01 / CAM-01 / MODEM-01 / RELEASE-01 / REF-01 / BASE-01 / PERSIST-01 | 未开始或只有调研；PERSIST-01 并入 M3 | 归档台账 |

## 待用户验收 / 需要用户

| 事项 | 时长 | 前置 |
| --- | --- | --- |
| M1：试听 loud-1（Android 电平、同相 1 kHz、1 s） | 约 5 分钟 | 已打包，`ZIZHAN_USER_PRESENT=yes bash work/m1-firstsound/run-loud-1.sh` |
| M2：真实折叠一次：合上应内屏熄灭、外屏出大号时钟，打开恢复 | 约 5 分钟 | 模拟合盖已闭环（fold-2）；已打包，`bash work/m1-firstsound/run-fold-3.sh` |
| M3：写 boot_a 并切到 a 槽（内存启动测试 11-19 已通过：选 Android 后 49 s 自动回到 Android，不再卡菜单） | 约 5 分钟，需要手在机器旁 | 若 magic 不保留会在菜单与重启间循环，需按音量键进 fastboot 后 `fastboot set_active b` |
| 外屏触控：在场摸外屏，验证触点坐标（事件协议 11-19 已闭环） | 约 5 分钟 | 需先把外屏触点接进触摸输入 |
| 蜂窝阶段 2：插 SIM 后是否让基带注册网络 | 决策 + 插卡 | 阶段 1b 基带已能启动到 ready（modem-6），[可行性](../research/2026-11-19-cellular-feasibility.md) |
| IO-01 音量键实体按键 | 并入 M1 末验收 | M1 服务就绪 |

## 2026-11-19 夜间无人值守小结（全部静音）

- **声音（M1）仍无声**，但已逐段排除：音频处理器数据（静音回环 loop-d1/n1/m2 逐帧正确）、功放 228 个配置寄存器（与 Android 播放时一致）、引脚、TDM 时钟（端点自己出 BCLK）。修正了 DACPCM1 源与 96 kHz GLOBAL_FS。剩余嫌疑在 TDM 输出端到数据线之间；下一步需在场读 LPASS TDM 寄存器，或补齐 Android 设备子图里的 0x07001011 / 0x07001098。
- **熄屏/亮屏 S3 + 触控暂停 S4**：无人值守 10 轮 x3 全部 PASS（[证据](2026-11-19-power-s3-device.md)），待人工看屏。
- **M3**：切换程序改为直接 PSCI 重启 + 看门狗兜底（uefi-zizhan `claude/2026-11-19-m3-handoff-reset`），镜像自检 48/48，待在场 RAM 测试。
- 温控 + 熄屏 8 分钟长测通过；[相机](../research/2026-11-19-camera-feasibility.md)、[蓝牙](../research/2026-11-19-bluetooth-feasibility.md)可行性文档；Wi-Fi 上网正常（天气只差定位授权）。

## 2026-11-19 白天无人值守小结（全部静音，未写分区，未推送）

- **声音**：数据一路送到功放已证实（引脚采样：时钟、同步、GPIO124 数据都在跳；功放把收到的数据回传到 GPIO122 也在跳）。"功放之前断了"的判断撤回。修了上午引入的 mode 40 `else` 串接 bug（2437122）。`loud-1` 已打包未运行。录音通路按 ACDB 改成 2 声道后仍拿不到数据。
- **天气**：小组件定位询问已点"允许"，日期与时间"自动设置"已打开，但时区仍为洛杉矶；真正卡住的是 FairPlay（[结论](../research/2026-11-19-weather-fairplay.md)）。
- **折叠/外屏**：合盖跟随模式（c4827a6）；外屏 L18 secondary（DSI1）资料与只读探针。
- **温控**：GPU 封顶与逐级释放实机成立（therm-g1/g2）；日用阈值 CPU 90 / GPU 85 °C 的 10 分钟长测通过（soak-4）。
- **蓝牙**：SE20 串口握手、HCI Reset、rampatch + NVM 下载成功（BT 5.3、Qualcomm）；bluetoothd 走串口 + 设备树节点，节点试验后它读到地址、随后因缺串口崩溃（[蓝牙](../research/2026-11-19-bluetooth-feasibility.md)）。未开射频。
- **相机**：PM8008、CAM_CC MCLK3、CCI 读芯片 ID 的逻辑（宿主机测试）和默认关闭的内核探针（未运行）（[相机](../research/2026-11-19-camera-feasibility.md)）。
- **稳定性**：daily-5/6、soak-3/4 共 80 次熄屏/亮屏全部成功，0 崩溃，Wi-Fi 正常。

## 2026-11-19 下午（用户要求尽量自己闭环；全部静音，未写分区）

- **折叠链路自测**：`zizhan_hall_simulate` 模拟合盖 → 按键熄屏 → 内屏 TE（GPIO86）0 次跳变；开盖 → 亮屏 → TE 44 次/100 ms。面板侧确认真的停帧/出帧。只差真实霍尔一次折叠。
- **外屏显示**：启动程序已把外屏（DSI1/INTF_2）点亮并让 VIG2 从 0xb8fc9900 取帧；写入彩条后 INTF_2 的输出签名（MISR）从恒定的 0x0c3d2ca8 变为恒定的 0xbe2dceaf——**外屏显示我们写的画面已闭环确认**（[外屏](../research/2026-11-19-outer-panel.md)）。
- **外屏触控**：WL2866D 给触控上电（1.8/3.0 V）后，FTS 在复位释放 7 ms 拉低中断——芯片活着；SPI 读芯片 ID 的轮次已备，等电量回升。
- **相机**：四颗传感器全部在 iPadOS 下应答芯片 ID：主摄 IMX766 0x0766、前置 IMX596 0x0596、超广角 OV13B10 0x560d42、长焦 IMX663 0x0663（[相机](../research/2026-11-19-camera-feasibility.md)）。
- **M3**：内存启动测试通过（选 Android 后 49 s 回到 Android，不再卡菜单）；写 boot_a 仍留到你手在机器旁时。

## 2026-11-19 晚间收尾（全部静音，未写分区，未推送）

- **外屏触控**：SPI 读到芯片 ID 0x3640，是 ST FTM5（Android 模块 `fts_touch_spi_sec.ko`）；读事件的协议还没打通（中断一直不来，读回全 0），下一步对照 FTM5 驱动。
- **折叠 fold-2**：模拟合盖时内屏熄灭（TE 0）、外屏画出时钟（MISR 1dcbd31f）；开盖时内屏恢复（TE 44）、外屏清黑（MISR ad495edf）。真实折叠用的包 `fold-3` 已备好。
- **最终稳定性 daily-9**（最新内核 build-play45，日用配置 + 外屏跟随合盖）：熄屏/亮屏 20/20 PASS；温控 mode 2 最高 CPU 77.3 °C / GPU 58.4 °C，未触发封顶；按计划的截止时间重置后回到 Android。为了不超过 1024 字节的启动参数上限，这一轮去掉了 `pong_touch_synth`（S4 之前已 10 轮 ×3 通过）。
- 手机留在 Android 待机充电。需要你在场的事见上方「待用户验收」：真实折叠 `fold-3`、试听 `loud-1`、写 boot_a，以及天气数据源、日用配置是否默认两项决定。

## 2026-11-19 晚（用户 19:30 的五项要求；全部静音，未写分区）

- **推送**：两个仓库已推送，CI 四项全部通过（UniXNU-zizhan `0e88e3e`）。
- **日用配置已改为默认**（`zizhan_daily`，`=0` 关闭）：daily-11 不带任何日用参数，熄屏/亮屏 20/20 PASS，温控与合盖跟随自动运行；命令行 1023 → 864 字节。
- **天气**：其他项目都没有让 fairplayd 在非苹果硬件上工作；静态确认天气的认证要 FairPlay（Mescal）签名。按用户决定暂不接数据源，保持挂起。
- **外屏触控**：FTM5 的 SPI 事件协议打通（读命令应为 0x87），控制器就绪与扫描状态事件都读到，中断线随之释放。
- **蓝牙**：`/dev/cu.bluetooth` tty（XNU 补丁 0023）与 UART 桥已编译，默认关闭，等用户同意开射频。
- **首帧 UX-01**：慢在 CPU 侧渲染准备（约 39 ms）；内屏触控 8 ms 轮询另加约 4 ms；本轮只分析不改。
- **蜂窝**：QcomBaseband（SC7180 + iOS 17）已做到 LTE 数据，本机可复用 ADSP 的启动方式；阶段 1 不发射可无人值守。
- 详情：[晚间记录](2026-11-19-evening-daily-default.md)。

## 2026-11-19 深夜（用户 20:30：蓝牙可开射频、首帧找优化、蜂窝先做无 SIM 部分；全部静音，未写分区，本批未推送）

- **蓝牙**：bluetoothd 崩溃的根因查清并绕过（MobileGestalt 缺 `product-id`，补丁 0025 只对蓝牙进程给 J171 的值），bluetoothd 现在常驻；BlueTool 还需要 `AppleBluetoothModule` 服务，是下一步。
- **首帧**：基线 83.6/82.2 ms 可复现；`fb_swapwait=0`、一直满频两个候选都否决；慢在 CPU 侧渲染准备，下一步细分 SpringBoard→backboardd。
- **蜂窝**：固件和 EFS 只读可达（modem-1）；**基带在 iPadOS 下第一次启动到 ready+handover**（modem-6，元数据须在 4 GiB 以下）；随后显示路径 panic，待补 `rproc_ddr` 带宽票。未发 QMI、未上线、无发射。
- 详情：[晚间记录 · 深夜](2026-11-19-evening-daily-default.md)。

## 2026-11-20 凌晨（无人值守计划执行；全部静音，未写分区）

- **蜂窝**：基带在 iPadOS 下**完整初始化**。GLINK + QRTR + 最小 pd-mapper + 内存影子 rmtfs 后，公布 45 个 QMI 服务，60 s 无看门狗，可正常停止。只读 DMS 读出固件 `MPSS.DE.2.0.c1-00108-WAIPIO_GEN_PACK…`，工作模式 OFFLINE（无射频）。阶段 2 需插 SIM 并经你同意。
- **温控**：逐级压频（每次 4 档，超阈值 10 °C 才一步到底），设备上持续负载下停在 10 档、65–67 °C，不再一下压死。
- **蓝牙**：已加 `/dev/btreset|btwake|btpoweroff`，nub 已注册；BlueTool 仍不打开设备，按规则挂起（下一步：拿 BlueTool 的 os_log、反推 bluetoothd↔BlueTool 的 mach 命令）。
- **首帧**：本轮没做细分测量（需给 CoreAnimation 加测量点），留待下一轮。
- **稳定性**：daily-13（play67，日用默认）熄屏/亮屏 20/20，无 panic。
- 详情：[晚间记录 · 凌晨](2026-11-19-evening-daily-default.md)。

