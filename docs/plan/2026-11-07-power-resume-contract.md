# POWER-02：分阶段恢复契约

11-07 用户追加完整锁屏、熄屏及后台硬件休眠要求后，详细实现路线、当前缺口、尝试方向和验收已扩展到[完整电源设计](2026-11-07-lockscreen-suspend-design.md)。本文保留初版契约；后续按完整设计和唯一台账接续，不作为第二份任务队列。

基于设备源码 `6d074ef09e5777cecfa23612e626ce2c3878153e` 加当前 WIP 的主机审查。本文完成进入真睡眠前的恢复设计，不实现 suspend/resume，不触发设备电源状态变化。动态状态只在 [STATUS.md](../progress/STATUS.md) 维护；既有板级调查见[外屏与电源](../progress/2026-11-06-fold-power-audit.md)。

## 分开验收的状态

| 阶段 | 可验证的行为 | 进入下一阶段前必须取得的证据 |
| --- | --- | --- |
| L0 锁屏 | 真实锁屏 UI、解锁、前后台恢复，CPU/存储/网络仍运行 | 相同内容可继续交互，Data 写入可回读，网络仍可访问；不称系统睡眠 |
| L1 内屏熄屏 | 确认 DSI/背光实际状态，唤醒后显示与触控恢复 | 正常首帧、内容、亮度读回及多次返回；黑图或 brightness=0 不能证明控制器已休眠 |
| L2 单设备暂停 | 独立排空和恢复 GPU、DSI、Wi-Fi、ADSP、UFS；每次只选一个设备 | 明确的在途工作清单、提交门、deadline、失败逆向恢复，以及暂停后实际硬件状态 |
| L3 CPU/平台睡眠 | 进入已查询支持且有固件依据的具体状态，恢复 CPU/GIC/timer 与全部设备 | 独立返回机制在目标态中有效、保留内存及恢复入口契约已确认；首次真睡眠有人在场 |

L0/L1 可以独立开发与验收；不应等待 L3 全部前提齐备才改进锁屏体验。当前未有新的 L0/L1 设备结果。不能把 `sleepserver.m` 的 Sleep.framework 作息服务或触控唤醒事件当成 L3 入口。

## 统一事务和失败回退

系统层需要一个显式事务：`Awake → Preparing → Quiesced → Suspended → Resuming → Awake`。进入准备阶段后，依赖设备先停止新提交，再排空在途工作；只有所有实际 owner 确认完成才允许进入平台态。任一 deadline、有效性或固件前置失败，就取消进入并逆序恢复已经成功暂停的设备。不能把未完成 DMA 标记为完成来推进状态。

现有设备层尚无该事务的实现。下表是逐设备验收契约，不能直接作为已有 API 调用顺序。电源依赖图和具体硬件动作仍需从本机固件/DT/vendor 输入补齐。

| owner | 暂停前必须持有的真实证据 | 恢复必须重建/核对的状态 | 失败行为 |
| --- | --- | --- | --- |
| CPU / GIC / timer | 在线核集合、实际 PSCI conduit/feature、boot CPU 恢复入口、睡眠中计时来源及 IRQ 唤醒路由 | 每核上下文、GIC distributor/redistributor/CPU interface、timer compare 与单调时间；辅助核回到调度器 | feature/唤醒未知时停在 Awake；不能调用当前 `haltCPU` 作为可恢复睡眠 |
| GPU / IOSurface | 阻止新 ring 提交，实际 CP 提交=完成；scanout/client BO 生命周期与 fence 可核对 | IOMMU/SID、ring/固件与有效映射，恢复提交后的真实完成和可见内容 | 未退役则取消睡眠；不回收仍被硬件使用的 BO |
| DSI / 背光 / 触控 | 取得 DSI owner lease、排空命令/DMA、固定当前内屏状态；触控唤醒路由有依据 | 面板/时钟/PHY 的必要顺序、DSC/timing、首帧和亮度读回、HID 新事件 | 忙碌则退出准备；不复用失败 DMA、不把未出光记为恢复 |
| Wi-Fi | 拒绝新请求并排空 dispatcher/FIFO；固件 WoW/IRQ/电源前提有证据 | MHI/PCIe/SMMU 和原生接口状态；按真实情况保留或重建关联、租约/路由/DNS | 原 session 失效就正确清理；不伪造 link-up；HTTPS 与新/旧 DHCP 资格分开 |
| ADSP / 电池 | 电池/音频通道真实启动状态、pending mailbox、固件低功耗协议 | ADSP 通道、真实电池更新和错误/过期状态；音频另验 | 无低功耗契约则不暂停此链；不重复发布假电池状态 |
| UFS / APFS | APFS/缓冲同步由真实存储栈完成；冻结新 I/O，所有 doorbell/descriptor/DMA 归属可核对 | host/PHY/link/clock、IOMMU 与 pinned backing 不变；先只读核对再允许 Data 写入 | 任一在途请求或 sync 失败就拒绝睡眠，保留日志及真实错误 |

依赖顺序的原则：应用/文件系统和所有 producer 先停提交；GPU/网络/ADSP 等 DMA owner 排空；显示命令与存储完成后，平台最后进入。退出时先恢复 timer/GIC 和基础 transport，再恢复存储及设备，最后放开 producer。具体 UFS/ADSP/GPU 的互相依赖尚未完成核对，不预设固定硬件时序。

## 当前代码证明了什么

- `drivers/ArmCpu/ArmCpu.cpp` 的 `quiesceCPU` 关闭 timer/GIC 接口，辅助核走 `psci_smp_cpu_off`；`haltCPU` 随后无限 WFI。`startCPU` 使用已存在的 CPU_ON；没有可恢复平台睡眠状态的契约。不能把辅助核开关当成 SYSTEM_SUSPEND。
- 本轮检查 `ArmCpu`、`NativeUfs`、`PongWifi`、`PongAdreno`、`ArmDTPlatform` 未发现这些自定义 owner 完整配对的 IOKit `registerPowerDriver/setPowerState` 链。这只界定当前适配缺口，不声称系统所有上游 kext 都没有电源实现。
- `PongDsiLease.h` 已有实际 DSI owner 串行化，但没有整机睡眠恢复保证。`RootQueue.h` 的 outstanding 检查针对现有根传输队列，不等同于 APFS 全栈 sync、冻结和 resume。
- `PongBootReset.h` 的普通启动自动返回依赖 pinger/guard 线程及当前 watchdog 路径。目标睡眠态下线程/计时或 watchdog 是否仍有效尚未验证；L3 无人值守前提未满足。

## 接续实施与停止条件

1. 先做默认关闭的固件能力只读查询设计：复用真实 PSCI conduit，查询 version/feature 并核对返回码；不调用 CPU_SUSPEND/SYSTEM_SUSPEND，不凭“支持函数”推导具体状态 ID、内存保留或唤醒能力。
2. 为 L0/L1 制定独立候选与真实内容/输入控制，确认屏幕状态和时间仍推进。原 baseline、人工开关和安全回退保持可用。
3. 逐 owner 实现准备/恢复接口及单设备对照；错误注入只在主机模型中检查逆序回退，不能用主机模型替代硬件资格。
4. 只有具体平台态、固件恢复入口、独立返回和人工条件齐备，才登记首次 L3 实验。缺任何一项即停止在相应阶段；不通过延长 autoreset 线程期限来替代独立唤醒。

主机检索记录留本机 `logs/thermal-1107/power-contract-source-review.txt`。本项只完成设计，不提升电源硬件资格。
