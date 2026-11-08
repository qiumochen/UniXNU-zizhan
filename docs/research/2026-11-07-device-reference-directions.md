# 从设备目录筛选的第一轮待验证方向

调查日期：2026-11-07（UTC+8）。入口为 [UniXNU 设备目录](https://unixnu.com/devices/)，并读取设备仓库的固定版本 README、porting 记录和相关源码。zizhan 对照入口为 `477417e2d8daf9efbb74e78dc98f40a55f8fbe96` 加入口已有工作区修改；当前能力、执行顺序及动态待办只在 [STATUS.md](../progress/STATUS.md) 维护。本轮形成方案，未执行候选移植或设备试验。

最接近的参考仍是同 SM8475、A730、22H374 的 **Pong**。本轮新增价值较明确的是 **SM-T733 的热策略失效处理**、**Sheng 的 WLAN/蓝牙共享供电所有权**及**umi / SM-T733 的 Data 保存边界**。目前查到的折叠项目没有可直接采用的双屏切换闭环；查阅范围内也未找到能直接替代本机 POWER-02 设计的、完整限定条件的 SM8475 整机深睡实现。

## 调查范围与证据等级

设备目录快照含 265 条记录，含同机不同维护者，不能理解为 265 种已运行设备。先筛选同 SoC、折叠结构及功耗/存储相关记录，再核对 12 个设备/板级参考仓库（含同机 LineageOS）及 4 个组件仓库。只读材料和下载清单留在仓库外；公开来源的完整版本列在文末。

- 目录是发现入口。其 zizhan revision 仍为 `3b3c52fb5ba11b0a185703d7832fd567c94e770e`，sheng 为 `460179565a018c741b93e0b1efa07c55d4de497a`；本次读取 sheng HEAD 已为 `689400177f93219a38bd7cf5ae2e9f5782adc594`。目录摘要不能覆盖设备仓库的较新证据。
- `implemented`、源码有回调、host 回放和外部作者的设备报告分别记录。外部真机结果在本轮没有复跑，更不是 zizhan 的设备资格。
- Pong 的 `services*` 历史已有大量内容随派生关系进入本仓库。执行前必须先做差异和调用可达性审计，分成“已有”“确有新增”“板级不适用”“缺设备证据”，不能把相同代码重新移植一次。
- 以下 R1–R10 是静态假设编号，动态状态统一归入台账 REF-01 及对应子系统；本文不另开第二份任务队列。

## 参考对象筛选

| 对象 | 本轮核对到的事实 | 适合参考 / 不足 |
| --- | --- | --- |
| Nothing Phone (2) / Pong，SM8475 / A730 / 22H374 | services125/132 有锁屏后 scanout 资源失败和成对退役设计；services162 用户仍报告解锁后无声。 | 首选 ABI、显示资源和 ADSP 生命周期对照；不是完整睡眠或音频恢复成功样板。[P1–P3] |
| Redmi K60 / mondrian，SM8475 / 20G81 | 11-07 README 报告首帧和用户态运行，但 pid 1 SIGSEGV 后复位仍阻塞；quiet-wakes 补丁直接跳过资源违规发送。 | 可参考内省与启动身份检查；不采用绕过资源错误的办法，也不把 Darwin22 参数当 Darwin24 参数。[M1] |
| realme GT2 大师探索版 / rmx3551，SM8475 / 22H374 | 已有板级派生与存储工作；porting 里仍保留 Pong/Nothing 输入及 services 历史。 | 同平台候选，但需先确认结论归属与实际 RMX3551 输入；不能把继承记录当另一台设备的独立复现。[M2] |
| OnePlus Ace 2 / udon、Pad 6 Pro / liuqin，SM8475 | 目录分别指向用户态/UI 诊断、板级/UEFI 准备；udon 没有给出可直接采用的完整恢复闭环。 | 保留低优先级同 SoC 跟踪，不因同芯片提升参考资格。[M3、目录] |
| Pad 6S Pro / sheng，SM8550 / 24A446 | 有 CPU/GPU 调频、WCN 共享 owner、蓝牙及音频记录；power 记录明确区分外设控制和系统 suspend；蓝牙 sleep/wake 未测。 | 借 owner、错误传播及测量结构；硬件寄存器、固件、音频拓扑和 27 原生 ABI 均需隔离。[S1–S3、B1] |
| Galaxy Tab S7 FE / SM-T733，SM7325 | 有 TSENS 热策略源码、固定 Data 布局和更新保留记录；power 记录仍把 suspend/resume 列为独立缺口。 | 热策略状态机和数据场景参考价值高；没有本机热阈值或深睡依据。[T1–T2] |
| Xiaomi Mi 10 / umi，SM8250 / 21H461 | README 报告测试布局中 Data 跨重启保存；安装替换 Android userdata，休眠未合格。 | 借数据保存验收和布局身份检查；不能照搬其破坏 Android 共存的部署布局。[U1] |
| Surface Duo 2 / zeta，SM8350 | README 有 GPU 映射/物理分配压力及长帧调查，仍有黑色背景等限制。 | 可借诊断分层；本轮未找到双屏交接验收，不能因为外形相似就认定折叠适配成熟。[F1] |
| Galaxy Z Fold3 / q2q；MIX Flip / ruyi | q2q 明确是 source-only 编译 harness，无存储/GPU/电源驱动；ruyi README 仍是继承 Sheng 描述，未给出本机双屏证据。 | 暂作跟踪对象，不作为内外屏切换实现来源。[F2–F3] |
| Pixel 4 / ios-flame | 显示电源补丁将像素逐步变黑，注释明确面板仍供电，VSync timer 继续调度。 | 可参考请求观察，不能把该做法计作面板掉电或真实低功耗。[F4] |

## R1：温度读数新鲜度与失效安全

**归属：THERM-01，优先衔接。** 本机 g396 只读到有效 last-temperature，还没有更新/过期和热策略资格。SM-T733 的 `T733Thermal.cpp` 按完整传感器组计算状态，有过期时间、热/冷滞回，缺少有效读数时保持限制，GPU consumer 还检查 worker 样本年龄。[T1]

待验证假设：这些状态和错误语义可抽成 zizhan 的策略输入层，先运行 shadow 观察，再接现有 EPSS/GMU owner，而不直接采用它的地址、sensor mask、200 ms 周期、温度 trip 或频率档。源码中 CPU cap 读取与 GPU 的 consumer 年龄复核并不相同，也需要审计 worker 停止后 CPU 是否会保留旧的无限制值。

下一动作是对照已有 TSENS core，列出 source sample time、consumer read time、VALID、组覆盖、worker 延迟和过期原因；回放无首样本、部分丢失、读数恢复、worker 停止及过期后的冷样本。**通过条件：失效时不会错误解除限制，只有本机映射与完整新鲜冷样本满足策略后才能解除。** 限频写入另需 vendor 策略和本机档位核对、双配置构建及负载/恢复设备证据；本轮不能拿 T733 数值设 zizhan 阈值。

## R2：真正面板电源事务与显示资源恢复

**归属：POWER-02 / DISP-01 / GPU-01。** Pong services125/132 的现场是按键已到、服务继续、显示导入资源失败；它提供 source generation、用户/内核成对退役及未知 DMA 隔离的参考。[P1–P2] 这与本机显示恢复设计相关，但不是当前 g342 罕见映射失败的已证实同因。

待验证假设：将已有源生命周期审计接到本轮正在实现的显示事务，可以区分“显示请求已到”“面板命令完成”“恢复后首帧有效”。先核对现有 ResetSources/prepare/complete/retained mapping 是否已覆盖，再补真实 off/on 边界；本机 L18 的完整 vendor 序列和长包限制以[本批实现记录](../progress/2026-11-07-power-unattended.md)为准。flame 的画黑方案不作为掉电候选。[F4]

下一动作是补充跨 off/on 的 generation、owner、在途帧、命令完成和首帧证据设计。**通过条件：未完成 DMA 不释放、失效映射不复用、命令失败不报告 off 成功，恢复可见内容与扫描推进分别取证。** 主机覆盖超时/重复请求/恢复失败后，首次面板休眠仍待已有人工前置。

## R3：共享供电 owner 与 WLAN 事件化

**归属：POWER-02 / NET-02 / BT-01。** Sheng 的 WcnPower 将 Wi-Fi、蓝牙对共享 supplies 的引用分开，最后一个 owner 才释放；host 检查覆盖启动次序、重复投票和失败转换。QcaBluetooth 还提供 UART DMA 的有界排空、quiesce/resume 和异步 IRQ 工作结构，但其系统 sleep/wake 未测。[S1、S3、B1]

待验证假设：先把“业务活动引用”和“物理 rail 投票”分开，能防止未来关 Wi-Fi 误断蓝牙/共享域；WLAN 从 5 ms polling 转向真实 IRQ 是联网低功耗的独立前置。PongWifi 的 MSI receiver/aggregator 结构可作同代参考，但其 GPIO、SPI 和供电值仍必须以 zizhan live DT 为准。[W1]

下一动作是制作 zizhan consumer→rail/clock/IRQ→owner 矩阵，复核实际 GIC 到达、屏蔽/清中断/EOI、worker 唤醒和队列排空；先 shadow 记录请求，不降共享票。**通过条件：失败投票不丢 owner，仍忙的资源不能关闭；停止周期 pump 后实际 RX/HTTPS 仍推进。** 首次 IRQ 收包、固件节能、WoW、物理 wake 和联网深睡分别验收，不能用延长轮询周期替代。

## R4：CPU idle、降频与深驻留分层

**归属：POWER-02，THERM-01 为依赖。** Sheng 的 CPU idle patch 提供 busy/idle 统计给 governor；`ShengPlatformPower.cpp` 明确不声明 sleep 等未实现能力。PongPower README 同样写明 CPU 调频/系统睡眠回调未接入。[S2、P3]

待验证假设：可借 governor 的观测结构，把本机固定高 L3、触摸 boost 和后台周期唤醒分开统计；这些材料不能提供一个可以直接执行的 CPU_SUSPEND state。现有本机设计已包含 timer-stop、目标 CPU 契约、PDC/RTC 和 TCS 歧义，不重做 g397 相同查询。

下一动作是审计每个定时唤醒的真实需求及可合并期限，冻结“停止可选生产轮询、保持 WFI”这一浅层对照设计，再评估单核上下文与共享域。**通过条件：给出相同负载/观测开销下的 wake 次数、驻留增量、实际功耗或电流、延迟与温度，且保留返回资格。** 减少日志、降低频率或统计 idle 增长都不能单独计作深睡。

## R5：持久启动与 Data 保存分开探索

**归属：PERSIST-01 / BASE-01 / APP-01。** umi 报告测试布局的 Data 跨重启保存，SM-T733 记录固定 Data 与普通 FAT 更新相隔离；Pong services132 也明确 guest 改写 APFS 后不能把旧主机元数据差量覆盖回去。[U1、T2、P2]

待验证假设：先固定本机已存在的 pinned root / Data 身份、块范围和更新所有权，可以独立推进保存可靠性，不必先实现脱离电脑启动。直接用 umi 的 userdata 重置或 T733 的固定偏移不适用本机。

下一动作是只读梳理 boot/slot/恢复入口、正常 sync/unmount、OS-only 更新和 Data 写回路径，再设计设置/应用文件在正常重启、进程崩溃及异常复位后的分级核验。**通过条件：每类场景明确允许丢失的未提交数据、已提交数据的校验、APFS 错误与恢复入口；更新保留 Data 必须与布局/hash 同时核对。** 正常重启通过不推出掉电一致性；异常退出只在隔离候选和恢复条件齐备后验证。脱离电脑启动先形成方案比较，现有不 flash 约束不变。

## R6：启动内存分配失败与压力归因

**归属：BOOT-01 / GPU-01。** zeta 的记录将 GPU VA 上限、物理分配拒绝、队列背压及内容错误分开；mondrian 的当前阻塞是 pid 1 SIGSEGV，不能当本机 CS4K 分配失败的同因。[F1、M1]

待验证假设：参考这种分层证据，补齐失败时刻的 map size/free/largest、请求大小/对齐及保留资源身份，比泛化增加 GPU 池或改变验证策略更有诊断价值。本机已有失败路径诊断和 g393 未复现记录，先审查覆盖缺口，不重复原输入等待故障。

下一动作是比较 CS4K 和显示池的实际分配路径、失败时间及资源释放顺序，形成可用正常启动自然覆盖的取证清单。**通过条件：故障出现时可以区分 KVA、连续物理页、VA/IOVA 或队列限制，且诊断不改变正常控制流。** 不移植 K60 的资源违规早退，不取消签名或真实 panic，也不把某次无故障启动当根因修复。

## R7：折叠先用同机 Android 约束显示与触控交接

**归属：FOLD-01 / POWER-02。** 本轮的 Duo 2、Fold3、MIX Flip 材料均未给出可直接采用的 iPadOS 自动双屏资格。更可靠的板级来源仍是同机 LineageOS：display layout 分别选内/外默认屏，input port 匹配 fts-pri/sec，FoldStatusNotifier 将折叠传感器状态通知触控。[F1–F3、Z1]

待验证假设：固定逻辑画布、外屏独立 60 Hz，再手动交接显示和触控，可避免在同一轮混入动态尺寸、Hall 与电源策略。现有 GPIO62/DSI1/外触控调查继续有效。

下一动作是对照 Android 状态、Hall 边沿、面板和触控 owner，写出手动切屏的排空→关旧屏→启新屏→首帧/TE→触控坐标交接及逆序回退。**通过条件：外屏真实出光、帧推进、坐标和失败返回单独通过，之后再加 Hall 去抖和自动切换。** 需要人工固定姿态、确认面板及触控；本轮只补参考，不启动该试验。

## R8：音频借协议与时钟恢复，不借功放参数

**归属：AUDIO-01 / POWER-01 / POWER-02。** Pong services161/162 提供 StartIO 代次、样本 epoch、stream-active、停止功放与释放时钟次序的调查；但非零 PCM、DSP WRITE_DONE、功放状态仍未解决作者反馈的解锁后无声。Sheng 有 AudioReach/原生音频及主机时钟处理，其路由和保护限制也与本机不同。[P2、S1]

待验证假设：将“原生客户端→已提交 PCM→DSP 周期完成→本机功放→实际声音”拆为五段，可减少在没有模拟输出时反复修改 HAL。本机是 CS35L41，不能复制 Pong TFA9873、Sheng CS35L43 配置或未调音的直通增益。

下一动作仍是 live DT 身份/总线只读核对，再盘点本机固件、调音、保护及 ADSP graph，补 StartIO/StopIO 并发与恢复回放设计。**通过条件：设备只在真实 ready 后发布；每段都有自身证据，播放、停止、静音与恢复实际声音分别验收。** 功放寄存器显示启用和波形非零都不能代替听感确认。

## R9：先查应用，再按真实首帧阶段选择 GPU 优化

**归属：APP-01 / UX-01 / GPU-02。** Pong services161/162 用真实 CP、提交同步及缓存维护区分 CPU 侧开销与 GPU 执行；其普通运行窗口不是本机性能对照。[P2] 本机 g379 已定位静止首帧主要在提交前，display-link 修正已接受，提前 flush 已拒绝；这些不重新列为未移植优化。

待验证假设：先补当前应用矩阵，再对出现故障的应用检查纹理布局、pipeline/BO 缓存、CA commit/编码及真实 CP，可缩小移植范围。Pong 活动导入协议只在确认本机差异、协商和所有权完整后评估；不能为了少一次同步取消 acquire/fence。

下一动作先按 APP-01 固定镜像与内容检查 Safari、设置、时钟、天气和照片；独立设计有界 CA commit/编码观察，同一轮记录触摸→提交→GPU→显示。**通过条件：内容正确、有效样本至少 15、median 改善至少 10%、p90 不退步及独立画面验收；未测能力保留 SKIP。** Safari 实际网页加载与网络 helper HTTPS 仍分开。

## R10：蓝牙和蜂窝的新公共组件先做兼容性清单

**归属：BT-01 / MODEM-01，后续扩展。** QcaBluetooth 在 Sheng / WCN7850 / 24A446 有配对、重启重连、A2DP 及与 Wi-Fi 共存记录；HFP/SCO、sleep/wake 未测。QcomBaseband 在 Coachz / SC7180 / 21H461 的 IPA v4.2 路径有 LTE IPv4 网页和 PDP 重连报告，IPv6、APN 编辑、长时运行等仍待验证。[B1–B2]

待验证假设：蓝牙 GENI/H4/IBS 与原生桥、QMI/QRTR 与 AppleBaseband 服务可以拆成公共协议层和本机 adapter；无需先编写一套同类桥。但 WCN7850→本机控制器/固件、27→18 蓝牙 ABI、SC7180→SM8475 IPA/GSI/启动/存储都不是可直接兼容的已知事实。

下一动作是分别列芯片与传输、固件/地址来源、原生 ABI、DMA/IRQ、共享电源及错误回收的兼容性表；蜂窝只读盘点不接触 NV/EFS 写入。**通过条件：每个接口明确可复用、需 adapter 或缺输入，先形成最小探针设计再实施。** 蓝牙播放不推出通话麦克风；SIM/注册、数据、通话、短信和待机恢复继续分别验收。

## 建议如何进入后续批次

保持 POWER-02 当前实现为主实验。先把 R1 的失效/新鲜度规则、R2 的显示生命周期对照、R3 的共享 owner 审计作为独立主机输入，避免争用 build/install 状态。R4 接现有 CPU/wake/TCS 设计；R5/R6 在只读调查层并行准备。R7/R8 随板级前置积累人工验收，R9 按既定 APP-01 先检查顺序推进，R10 留后续。

每条方向进入实现前，至少固定一个上游版本/文件、本机差异、一个预期可证伪的假设、单变量候选、验收与停止条件、manifest/hash 和回退。已经存在的实现只登记覆盖缺口；没有可复用闭环时明确保留“未找到”，不据目录排名推断成熟度。任何硬件资格变化再更新 porting.toml，本轮不提升其能力状态。

## 固定来源

以下链接指向本轮实际读取的源码版本；完整 HEAD 不是对应硬件结果的统一验收版本，具体试验仍看文内各轮记录。

| 标记 | 来源与完整版本 |
| --- | --- |
| 目录 | [UniXNU 设备列表](https://unixnu.com/devices/)；[devices.toml](https://github.com/UniXNU/UniXNU/blob/32446c8f15722e583f9353d4f22bcfcb0f9ad695/devices.toml) 为本轮目录快照，SHA-256 `c9027a64d2e4672a856b2745473591aff6a0c3e2f30282e7b759135b39267ba8`，源码固定于 `32446c8f15722e583f9353d4f22bcfcb0f9ad695`。 |
| P1 | Pong `e0ee6df109b4b5dc706cac660d4f8210dc911dac`：[services125](https://github.com/Chengdong-Yu/UniXNU-pong/blob/e0ee6df109b4b5dc706cac660d4f8210dc911dac/migration/ipados18/services125-wake-scanout-usb.md)、[services132](https://github.com/Chengdong-Yu/UniXNU-pong/blob/e0ee6df109b4b5dc706cac660d4f8210dc911dac/migration/ipados18/services132-wake-usb-ownership.md)。 |
| P2 | 同一 Pong 版本：[services161](https://github.com/Chengdong-Yu/UniXNU-pong/blob/e0ee6df109b4b5dc706cac660d4f8210dc911dac/migration/ipados18/services161-active-import-audio-resume.md)、[services162](https://github.com/Chengdong-Yu/UniXNU-pong/blob/e0ee6df109b4b5dc706cac660d4f8210dc911dac/migration/ipados18/services162-sleep-audio-active-acquire.md)。 |
| P3 | PongPower `c37179ed12ba7f981b2b088aea482b969085eb84`：[README](https://github.com/Chengdong-Yu/pong-power/blob/c37179ed12ba7f981b2b088aea482b969085eb84/README.md)。Pong 的 [sleepserver](https://github.com/Chengdong-Yu/UniXNU-pong/blob/e0ee6df109b4b5dc706cac660d4f8210dc911dac/userland/sleepserver.m) 是 Sleep.framework 设置 provider，不能用于硬件 suspend。 |
| W1 | PongWifi `d2e33ca6e0f6a3b3e7881a45ab36c6920e7afb07`：[README](https://github.com/Chengdong-Yu/pong-wifi/blob/d2e33ca6e0f6a3b3e7881a45ab36c6920e7afb07/README.md)。 |
| S1 | Sheng `689400177f93219a38bd7cf5ae2e9f5782adc594`：[porting.toml](https://github.com/UniXNU/UniXNU-sheng/blob/689400177f93219a38bd7cf5ae2e9f5782adc594/porting.toml)。 |
| S2 | 同一 Sheng 版本：[CPU idle patch](https://github.com/UniXNU/UniXNU-sheng/blob/689400177f93219a38bd7cf5ae2e9f5782adc594/patches/xnu/zzye-sheng-cpu-idle.patch)、[ShengPlatformPower.cpp](https://github.com/UniXNU/UniXNU-sheng/blob/689400177f93219a38bd7cf5ae2e9f5782adc594/package/ShengPlatformPower.cpp)。 |
| S3 | 同一 Sheng 版本：[WcnPower.h](https://github.com/UniXNU/UniXNU-sheng/blob/689400177f93219a38bd7cf5ae2e9f5782adc594/package/WcnPower.h)、[owner host checks](https://github.com/UniXNU/UniXNU-sheng/blob/689400177f93219a38bd7cf5ae2e9f5782adc594/scripts/check-wcn-power.py)。 |
| T1 | SM-T733 `4379151e85f4a3bc5fd6011b4e79fe7d8be5a3ab`：[T733Thermal.cpp](https://github.com/RigoLigoRLC/unixnu-sm-t733/blob/4379151e85f4a3bc5fd6011b4e79fe7d8be5a3ab/package/T733Thermal.cpp)。 |
| T2 | 同一 SM-T733 版本：[porting.toml](https://github.com/RigoLigoRLC/unixnu-sm-t733/blob/4379151e85f4a3bc5fd6011b4e79fe7d8be5a3ab/porting.toml)。 |
| U1 | umi `3eb72578597d2ca16471b172d45eddc096a34a04`：[README](https://github.com/reAAAq/UniXNU-umi/blob/3eb72578597d2ca16471b172d45eddc096a34a04/README.md)。 |
| M1 | mondrian `70a1e4ca361a1116bc0ec1563bac74771c79f81c`：[README](https://github.com/Emilelu/UniXNU-mondrian/blob/70a1e4ca361a1116bc0ec1563bac74771c79f81c/README.md)、[porting.toml](https://github.com/Emilelu/UniXNU-mondrian/blob/70a1e4ca361a1116bc0ec1563bac74771c79f81c/porting.toml)。 |
| M2 | rmx3551 `0146dc3dd5961235b7eb0d92bdbcf2b4e2e8c899`：[porting.toml](https://github.com/Axiang-dev/UniXNU-rmx3551/blob/0146dc3dd5961235b7eb0d92bdbcf2b4e2e8c899/porting.toml)。 |
| M3 | udon `ad550a97f612836181dd36c6c08b1d7e90fb7b14`：[README](https://github.com/takasugl/UniXNU-udon/blob/ad550a97f612836181dd36c6c08b1d7e90fb7b14/README.md)。liuqin 仅作目录初筛，未深入其源码。 |
| F1 | zeta `eb489285a1d9153872f00c93ebb519a269b249b5`：[README](https://github.com/none24smu/UniXNU-zeta/blob/eb489285a1d9153872f00c93ebb519a269b249b5/README.md)。 |
| F2 | q2q `d67aea801a727df283d7a20f4ca034c216596deb`：[README](https://github.com/agcarbajo/UniXNU-q2q/blob/d67aea801a727df283d7a20f4ca034c216596deb/README.md)。 |
| F3 | ruyi `5394473ed77d634269e9023d4f97a9429242ef5a`：[README](https://github.com/ArnoDorian123/UniXNU-ruyi/blob/5394473ed77d634269e9023d4f97a9429242ef5a/README.md)。 |
| F4 | ios-flame `745c4341af86089efb0df851fa8a27fb8face624`：[display-power patch](https://github.com/NWylynko/ios-flame/blob/745c4341af86089efb0df851fa8a27fb8face624/xnu/patches/0079-sweet-display-power.patch)。 |
| Z1 | 同机 LineageOS `8474ba82bc10844e4ddce0925333d081fb5a471c`：[display layout](https://github.com/LineageOS/android_device_xiaomi_zizhan/blob/8474ba82bc10844e4ddce0925333d081fb5a471c/display/display_layout_configuration.xml)、[input ports](https://github.com/LineageOS/android_device_xiaomi_zizhan/blob/8474ba82bc10844e4ddce0925333d081fb5a471c/display/input-port-associations.xml)、[FoldStatusNotifier](https://github.com/LineageOS/android_device_xiaomi_zizhan/blob/8474ba82bc10844e4ddce0925333d081fb5a471c/sensors/FoldStatusNotifier.cpp)。 |
| B1 | QcaBluetooth `22ff97f93416452335bb61badb14c762da653487`：[README](https://github.com/UniXNU/QcaBluetooth/blob/22ff97f93416452335bb61badb14c762da653487/README.md)。 |
| B2 | QcomBaseband `dd0199cf3300764ed52b23f4a47761f91fab21a2`：[README](https://github.com/UniXNU/QcomBaseband/blob/dd0199cf3300764ed52b23f4a47761f91fab21a2/README.md)。 |
