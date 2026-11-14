# 接手调查：上游变化与可迁移方向

调查日期：2026-11-13（UTC+8）。本轮接续 [11-07 首轮参考调查](2026-11-07-device-reference-directions.md)，以远端仓库当前默认分支、设备实际引用的 submodule revision、固定版本源码及作者设备记录为依据。只调查和整理文档，没有构建、改动运行时代码、安装、启动或写手机，没有提升任何硬件资格。当前任务仍由 [STATUS.md](../progress/STATUS.md) 管理。

## 结论与接手状态

值得借鉴的新增材料集中在四处：**Pong 原生 22G100 的定位阻塞和 X65 蜂窝实现；Sheng 的空闲负载、内存与媒体适配；UniXNU/AdrenoMetal 的缓存与提交边界；同 SM8475 的 diting Linux 恢复、CS35L41 和自动唤醒经验。** 它们不能组成一个可以整体升级的同版本基线。

- 本机入口为 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`，`main` 跟踪 `origin/main`，入口工作树干净。11-09 发布和相应 CI 已收口，不重新开展发布工作。
- 11-10 本机交接已纠正独立 UEFI 的旧判断：常用工作副本已对齐用户 fork 的 `c498ff26d438f818a2910e099dcf64c9ea036336`；“27 项尚未公开”不是当前待办。
- 设备最后记录的安装态仍为隔离 fairplay-off v23，canonical 为 OS370；v25 从未启动。APP-01 天气/定位仍未验收，POWER-02 的 DMA ready 和 128 MiB 下 IdentityObserved 前置不变。
- 本轮进程检查未见设备 cycle、installer、waiter 或内核构建；仅有 adb server。未查询手机接口或屏幕，当前设备运行态保持未知，不能沿用 11-09 的充电模式观察作为实时状态。
- 用户本轮要求先充分调查，因此原下一设备动作暂不执行。研究后的设备接续仍是重新核对真实通道、候选和温度后取得定位页面；本报告不授权直接运行旧 v25 脚本。

## 范围、版本和证据边界

读取本机交接、当前台账、既有研究和相关源码；初筛 UniXNU 最新设备目录，再检查同 SoC、折叠机、主要组件与其他 GPU 路线。目录当前含 **368 条记录**，11-07 为 265；含维护者副本、继承记录和 invalid 项，不代表新增 103 种已运行设备。目录的 revision 和摘要仍会滞后，例如 Pong 的旧 iOS16 摘要与其当前 22G100 profile 并存，应回到仓库核对。

本轮核对了 32 个可访问远端仓库的 HEAD 或主仓库 gitlink（清单见末尾），重点读了 Pong、Sheng、AdrenoMetal、T733、mondrian、PHK110、K90、diting Linux 及同版 diting 的材料。初筛通常读取最近 15–35 条提交；重点组件另读实际 gitlink 的历史。没有穷尽每个仓库的所有分支、PR、issue 或历史。负面结论均限于本次范围。

结构查询按 codebase-memory 工作流检查了索引和 coverage，本项目未索引，故采用具体文件读取和 `rg` 回退。GitHub API/源码读取为主要来源，网页搜索用于发现 Linux 线索。原始 API 快照、源码下载、成功及失败请求、内容 hash 清单保留于仓库外的本机研究工件目录；公开文档只给可复查的固定来源。

| 对象 | 本次固定版本 | 相对前轮的关键变化与边界 |
| --- | --- | --- |
| UniXNU | `7d85ede80092050f4d87f72ac7229bb605ce07fb` | 11-11～13 集中推进 macOS 内存、GPU/Turnip、APFS container；与本机旧固定构建链不同。[U] |
| Pong | `0ee4243a101eac10a77cbc8b7b8ef77ae8ec93d7` | 11-10 起主线为原生 arm64e、PAC、iOS18.6.2/22G100，输入 iPhone17,2；同为 Darwin24，但不再是本机 iPad7,11/22H374 的直接更新源。[P] |
| Sheng | `1108068537d5396edb2c63b2f8c6f5b97b0a4bbc` | SM8550/A740，新增 macOS27.3/26C5035r 运行、GPU idle 修正、内存压力结果及 H.264/HEVC VPU；记录混有不同 OS 与仅主机结论，需逐项读 evidence。[S] |
| AdrenoMetal | `73d582fd4d5772b1f5fa83581a25077a16f2b6cd` | 11-11/12 更新 development runtime、pipeline 编译位置、tile dispatch 和内存/编码诊断。不能只看 metal2vulkan 的默认分支。[G] |
| T733 | `e8d584c22c299074ffbf6d1fcb298ff14c2a1b64` | 11-08 新增固定重建与 crypto 依赖记录；前轮热策略仍有参考价值，不是本周新增热控制成果。[T] |
| RMX3551 | `de4d4e1557eef3e16fe1f70cfec641979131f440` | 11-08～11 有 S3910、TrustCache handoff、USB debug worker 启动时机修正；旧 porting 中仍含 Pong 输入与 services 记录，不能算另一块板的独立复现。[R] |
| mondrian | `f119a8026776f5dd5d6bf536bb060fc9374c36f3` | 已较前轮 pid1 故障前进到新的双启动基线；20G81，触摸/GPU仍有缺口。多个历史状态段互相修正，优先看 MAIN-BASELINE 和新记录。[M] |
| PHK110 / Ace2 | `a7d45752aca1c46d43e3daf83b96ebad707ea447` | 11-12 发布 GPU 首帧/USB/持久化记录；README 明确尚未导出完整设备源码增量，clone 不能重建同样镜像。可借诊断方法，不能视为现成驱动源。[H] |
| K90 / annibale | `c3526b4e7e24c462fafcb5695ade5b2190ff7a17` | A830、23G90，新增热/流水线及媒体记录；有独立启动、正常重启保存报告。其 Gallium/A830 路线与本机 m2v/A730 差异大。[A] |
| diting Linux | `c68ff47db94792fb5fd608e98d1ae489c2e592b1` | Linux7.2.10＋未提交上游的设备补丁；10-21 材料为本轮新发现，**不是 11-07 后新增提交**。同 SM8475、QCA6490、CS35L41，硬件参考价值高。[L] |

### 必须按实际依赖找源码

Pong 主仓库实际引用 `pong-power@c117a6700968c944528570326c8d709e29bd373f`、`pong-wifi@20e1e5b4d9ad4e994917f6e8e0b890b7812b06a0`，两者都是 11-11 的累计 22G100 硬件更新，位于 `port/ios18e-native-302`，默认 `main` 仍在 11-02。只查各组件 main 会错误地认为没有更新。相反，某些组件 README 仍保留早期“不工作”段落，也不能用它覆盖较新的具体设备记录。

本机已有 Pong 派生的资源 owner、源退役、显示租约、网络修复和两块备用内存策略。调查结果应按**已存在／确有新增／OS或板级不适用／只有外部证据**拆开，不把同一份祖先代码重新移植。

## 1. 天气与定位：有很接近的同因记录，没有现成天气修复

Pong [services188](https://github.com/Chengdong-Yu/UniXNU-pong/blob/0ee4243a101eac10a77cbc8b7b8ef77ae8ec93d7/migration/ios18e/services188-location-migration-board-gate.md) 的真实 stackshot：CoreLocationMigrator 等 locationd，locationd 等 CLHarvestControllerSilo，最终等待 `com.apple.fairplayd.versioned` 的 intransit port；原生 fairplayd.H2 不停干净退出、被 launchd 节流。作者没有合格的 FairPlay/GNSS 后端，未合成成功回复。

这与本机 v22/v23 的 A2 job 对照一致，**增加了“服务端口占用导致定位链等待”的可信度**。但上游处理的是首次 DataMigration 开机屏障：暂缓该 migrator，后来在明确授权下重置 Data。它没有证明定位开关、授权、GPS 或天气源成功；本机已经进入桌面并显示过定位提示，没有依据照做迁移暂缓，更没有清空 Data 的需求。

对 APP-01 的决策：不回头重复旧 MIG1203 调查，不引入上游 Data 重置或 migrator 改动。接续仍先取得真实原生定位页，区分全局开关、应用授权、网络、weatherd/WeatherKit 请求与 GNSS。v24 的 Wi-Fi arena 失败独立保留，不能由 weatherd 空闲栈推定网络或天气服务正常。

## 2. 电源：新增的是“减少空闲负载”及恢复失败经验

Sheng `GpuPowerProbe.cpp` 的新 governor 以 busy 时间和 ring 未清空判断活动：微小背景计数不再让 GPU 永远无法判 idle；持续约 80 ms 休息后才认定真正 idle，避免轻帧间隙反复触发 wake boost。`porting.toml` 记录触控在无手指三秒后回 doze、GPU 空闲行为已上机确认。**这是外设/空闲功耗经验，不是系统 suspend 资格。** 本机触控补丁仍有每 8 ms 醒来检查 GPIO、约每十轮兜底取 FIFO 的路径；控制器为 FST2，不能复制 Sheng Novatek 的 doze 命令或 A740 governor 数值。

K90 [11-09 热与流水线记录](https://github.com/iola1999/hackios-redmi-k90-annibale/blob/c3526b4e7e24c462fafcb5695ade5b2190ff7a17/docs/progress/2026-11-09-thermal-and-pipeline-optimization.md) 还发现锁屏小动画引发整屏重绘、错误归一化的 busy 让 GPU 保持高频、开机亮度同步被当作用户拖动。其修正后作者记录桌面静置14分钟 CPU约38–39°C、GPU40.5°C。这是多个改动共同形成的 A830 结果，不是本机可复现的降温数字；可借鉴的检查是“锁屏后谁仍在产生帧、busy单位是否正确、自动亮度同步和实体滑块是否混淆”。本机已验手动亮度与自己的display-link修正，不重新移植其整套策略。

diting Linux 的价值更具体：[BRINGUP_STATUS](https://github.com/royka1/diting-ubuntu/blob/c68ff47db94792fb5fd608e98d1ae489c2e592b1/docs/BRINGUP_STATUS.md) 记录 s2idle/deep 进入和返回，四轮无失败，USB/display/GPU 返回，APSS collapse 有报告；**CX/AOSD/DDR 统计未显示驻留且计数器布局未核实，功耗测试不确定**。[KNOWN_ISSUES](https://github.com/royka1/diting-ubuntu/blob/c68ff47db94792fb5fd608e98d1ae489c2e592b1/ubuntu-diting/docs/KNOWN_ISSUES.md) 还保留：

- ADSP GLINK 充电通知、modem QMI indication 会造成早醒；“进入 deep”不能推出整机持续低功耗。
- 自动恢复时系统把屏幕点亮，又不再次入睡，导致灭屏后仍长时间发热；其修正区分按键/来电/通知唤醒与 RTC 等后台恢复。
- FastRPC 的 listener 被 freezer 打断后丢请求，跨几轮 suspend 后音频才失败；不能只验第一次画面恢复。
- 新增 SLPI SMP2P `sleepstate` bit12 通知仍未解决 handover 消息风暴；该位不是已证明有效的万能修复。
- A730 runtime resume 仍有 HFI timeout、挂起及恢复后 GMU 不再初始化，延长 autosuspend 只作为实验，不宣称修好。

对 POWER-02 的决策：未来方案应显式区分“后台恢复但保持屏灭”和“交互唤醒”，恢复契约包含 GLINK/QMI/FastRPC outstanding request 与 owner epoch。先看本机真实 wake 源、固件和已有事务设计，不复制 Linux 的定时重新入睡服务，不照抄 PSCI state、IRQ 或 SMP2P 位。**现有显示 DMA/SMMU 解阻条件仍是第一道门，本轮不新增同输入的诊断轮。** 本次仍未找到可以直接采用、具备完整条件与本机适配的 XNU/SM8475 整机深睡样板；Linux 的经验补充了这一缺口的调查输入。

## 3. 温控：最新原生热压力接口与安全控制要分开

Sheng 新 `ThermalLevel.h/.cpp` 接入 thermalmonitord 读的压力等级，提供 CPU/GPU trip/hysteresis 与 sample-count stale 检查。然而源码明确：无读数输入计 nominal 并清掉已跨阈值；连续三轮样本计数不动后不采用读数。这适合研究**原生等级发布接口**，不能直接作为本机的失效安全限频控制。

T733 `T733Thermal.cpp` 的完整样本、新鲜度和失效限制语义仍更适合 THERM-01：缺传感器不解除已有热限制，长时间没有完整样本请求最低档；GPU consumer 自己复查 worker 样本年龄。CPU consumer 的 worker 停止语义仍需单独审查，不能把该实现整体称为无缺口。

本机 `ZizhanTsensCore.h` 正确保留 VALID/无效状态，`ZizhanTsensProbe.h` 仍是有限一次观察，g396 的 32 路有效值不能推出持续监控。本轮建议仍为**有界多时点＋源采样时刻/consumer 时刻＋过期失效回放**；之后才是本机 sensor→domain 与 vendor trip 核对、shadow policy、真实限频。Sheng/T733 的地址、sensor mask、温度阈值、周期和 OPP 均不直接采用。

## 4. GPU与分配：先审两个小差异，暂不整套升级

UniXNU 11-12 的两个具体补丁值得列为后续小审计：

1. [`turnip-darwin-sweep-retired-on-cache-miss.patch`](https://github.com/UniXNU/UniXNU/blob/7d85ede80092050f4d87f72ac7229bb605ce07fb/patches/mesa/turnip-darwin-sweep-retired-on-cache-miss.patch)：BO cache hit 不再先进行可能阻塞的全量退役回收；fence 观察的机会式回收用 trylock，真正 cache miss 再回收。保留失败传播和 fence，适合检查本机是否有同样锁竞争，不能当作可省略退役的许可。
2. [`turnip-shaders-shared-across-pipeline-caches.patch`](https://github.com/UniXNU/UniXNU/blob/7d85ede80092050f4d87f72ac7229bb605ce07fb/patches/mesa/turnip-shaders-shared-across-pipeline-caches.patch)：同一 device 的不同 pipeline cache 复用 shader 对象，保留对象引用，并不冒充应用自身 cache hit。可用于重复建管线的特定应用；先量重复编译是否存在。

这两条仍依赖对应 Mesa 数据结构和 Darwin KMD。本机 `scripts/build-pong-turnip.py` 配置为 `shader-cache=disabled`，不能从有缓存术语推定同类缓存已生效；磁盘 shader cache、Vulkan pipeline cache、BO cache 是三个层次。`build-pong-metal-bridge.py` 已应用 `0021-zizhan-keep-two-empty-blocks.patch`，不再把“留两块备用”列作新移植。

提交拆分补丁以 command-buffer 开头作为优先切点，属于协议相关改动；本机旧 IB-list 边界候选没有上机，不能因出现类似上游方案就复活。最新 AdrenoMetal 还更换 m2v development runtime、tile dispatch 和编译线程路径，移植成本远大于上述两条差异。

Sheng [run139](https://github.com/UniXNU/UniXNU-sheng/blob/1108068537d5396edb2c63b2f8c6f5b97b0a4bbc/reports/macos27-memory-run139.md) 对关闭 lowering worker 的对照，重 pass commit→schedule median 17.0→17.4 ms，p90 31.5→29.1 ms，窗口和 pass 数不同；作者明确没有证明目标中位数改善，不改策略。其 5 GiB 压力确实发生 compressor、swap-out/in 并回到正常压力，但不是逐字节完整性或吞吐上限验收。不能用这些结果推定本机 iPadOS jetsam 16 MiB renderer 或显示池故障已经解决。

UniXNU/Sheng 的低 DRAM bank adoption 针对 macOS 内核 window 与 firmware-owned 排除范围，本机低地址 panel/Wi-Fi/DSI DMA 约束不同。Pong 新 `PongLowDma.h` 按 consumer 命名分配、验证不重叠，并要求 loader 真实停止旧 USB DMA；可以借所有权契约，不能借保留地址或把时钟开启视为 DMA ownership。

## 5. 音频、蜂窝、视频、持久启动与折叠

| 方向 | 新材料及证据等级 | 对本机的决定 |
| --- | --- | --- |
| CS35L41 音频 | diting Linux 同 SM8475、双 CS35L41，记录实际听见双扬声器；后续加载 Cirrus 保护固件及每台工厂校准，讨论双 slot、反相 bitclock 和保护开启后单边静音。 | **比 TFA9873/CS35L43 更贴近的新增参考。** 后续 AUDIO-01 先对 zizhan live DT、TDM/MI2S 拓扑、固件/保护/校准归属；不借另一台的校准、gain 或 slot 常量。未保护路径不是本机放音候选。 |
| X65 蜂窝 | Pong `pong-baseband@817a2854bec6dad7a70ad6456dac1ad3274ca345` 为 SM8475/IPA5.1/GSI3.0；services287 记录 Wi-Fi 排除的原生 cellular HTTP 200；README 的 VoLTE 措辞比组件“通话立即结束、麦克风/听筒未完”更乐观。 | **MODEM-01 的首选硬件参考应增加 Pong。** 相比旧 Coachz/SC7180/IPA4.2 更接近；复用协议/owner/真实 completion 设计，22G100/iPhone 的 AppleBaseband ABI 与本机 iPad profile、固件、NV和资源仍待独立核对。数据、IMS注册、真实通话/SMS分验。 |
| 硬件解码 | Sheng 11-11～12 有 VPU/VideoToolbox，H.264 60 pictures 设备记录，HEVC/10bit 新源码；颜色未对参考、无 encoder。diting Linux 有 SM8450 Gen2 Iris 平台数据与 firmware 差异，明确 encoder 曾重置整机。K90 当前离线270帧不等于连续播放。 | 建立后续媒体 ABI/SoC 差异清单即可；不把 SM8550 Iris3/VPU30 固件发给本机，不直接开放 encoder。优先级低于天气、热和恢复。 |
| 独立启动/Data | K90 报告开机菜单选 Android/iOS、脱离电脑及正常重启保存；PHK110 报告 `/var` 保存但构建源码链不完整；Pong新增 APFS write tracking。 | 先研究菜单入口/回退及 Data 更新所有权。K90 有分区/刷写步骤，本机仍只临时 boot；不因外机成功直接安装，不将正常保存扩大到掉电一致性。Pong原生写跟踪也不是本机 root image 可直接套用的更新器。 |
| 折叠 | 新目录有 MIX Fold3/babylon 条目，但该 repo 当前 README 仍写 Sheng，目录 device.name 校验 invalid。Duo2/q2q/ruyi HEAD 与前轮相同。 | 本次未取得新增的合格自动双屏参考。保持同机 LineageOS 显示/输入/Hall 的板级事实和手动交接方案，不能因目录出现 Fold3 就推断外屏/折叠已有闭环。 |
| 蓝牙/共享电源 | QcaBluetooth 11-10 增加 macOS adapter及 UART 停钟后排队控制字节处理；README仍明确 HFP/SCO、sleep/wake 未测。 | 借排队/IBS/时钟 owner 设计，保持 WCN7850与本机芯片/固件、27与18 ABI隔离，不登记蓝牙睡眠资格。 |

## 后续决策顺序

这份研究完成的是 REF-02 的“接手增量调查与决策”，不完成 REF-01 的移植验证。暂不另建一条设备主线。

| 次序 | 对应任务 | 最小下一交付 | 必要判据与停止条件 |
| --- | --- | --- | --- |
| 1 | APP-01 | 在新鲜接口/输入前置成立时取得原生定位页及开关实际状态；网络单独判定。 | 没有真实通道、候选身份或温度/返回证据就停；不自动改权限、不重跑旧脚本、不伪造位置。当前研究阶段不执行。 |
| 2 | THERM-01 / REF-01 R1 | 以现有 TSENS 为基础，准备有界多时点和新鲜度/失效语义；固定本机 sensor/domain 依据。 | 缺失/过期不被解释为冷，不因 worker 停止解除限制；先 shadow，不能以外机阈值写本机频率。 |
| 3 | POWER-02 / REF-01 R2–R4 | 在显示前置解阻后，将后台恢复/交互唤醒、DSP/QMI请求恢复纳入已有事务设计；先验证最小 off/on。 | 前置未同时满足不进 panel off；失败 DMA 不释放，首帧/显示推进/实际功耗分验。 |
| 4 | UX-01 / GPU-02 | 只审 BO cache-hit 回收与同 device shader-cache 复用是否存在真实本机差异。 | 先有具体慢阶段与重复工作证据；匹配场景、内容正确、真实 completion、median/p90；不整体升级m2v，不重启已拒绝flush方案。 |
| 后续 | AUDIO-01 / MODEM-01 / PERSIST-01 | 分别吸收 diting CS35L41、Pong X65、K90 启动菜单的可复用契约，形成板级/ABI输入表。 | 原有顺序和人工前置保持；不能用外部作者的设备报告提升本机资格。 |

## 固定来源索引

固定 SHA 是本次读取版本，不意味着所有文内实验都发生在该 SHA。具体试验 revision、dirty state 和条件以其原记录为准。

- [U] [UniXNU](https://github.com/UniXNU/UniXNU/tree/7d85ede80092050f4d87f72ac7229bb605ce07fb)，含本轮 [devices.toml](https://github.com/UniXNU/UniXNU/blob/7d85ede80092050f4d87f72ac7229bb605ce07fb/devices.toml) 与 Mesa patches。
- [P] [Pong](https://github.com/Chengdong-Yu/UniXNU-pong/tree/0ee4243a101eac10a77cbc8b7b8ef77ae8ec93d7)，含 services188、287、294、299、302及真实 submodule gitlinks。
- [P-power] [PongPower](https://github.com/Chengdong-Yu/pong-power/tree/c117a6700968c944528570326c8d709e29bd373f)；[P-WiFi](https://github.com/Chengdong-Yu/pong-wifi/tree/20e1e5b4d9ad4e994917f6e8e0b890b7812b06a0)；[P-X65](https://github.com/Chengdong-Yu/pong-baseband/tree/817a2854bec6dad7a70ad6456dac1ad3274ca345)。
- [S] [Sheng](https://github.com/UniXNU/UniXNU-sheng/tree/1108068537d5396edb2c63b2f8c6f5b97b0a4bbc)，重点 `package/ThermalLevel.h`、`GpuPowerProbe.cpp`、`VideoProbe.cpp`、`porting.toml` 与 run139 报告。
- [G] [AdrenoMetal](https://github.com/UniXNU/AdrenoMetal/tree/73d582fd4d5772b1f5fa83581a25077a16f2b6cd)，重点 README、`SharedShaderCache.h`、`NativeBridgeMetalFrontend.mm`、bridge patches 0040/0041。
- [T] [T733](https://github.com/RigoLigoRLC/unixnu-sm-t733/tree/e8d584c22c299074ffbf6d1fcb298ff14c2a1b64)，重点 `package/T733Thermal.cpp`。
- [R] [RMX3551](https://github.com/Axiang-dev/UniXNU-rmx3551/tree/de4d4e1557eef3e16fe1f70cfec641979131f440)，其 `porting.toml` 继承内容不算另一机独立设备证据。
- [M] [mondrian](https://github.com/Emilelu/UniXNU-mondrian/tree/f119a8026776f5dd5d6bf536bb060fc9374c36f3)，重点 `docs/engineering/MAIN-BASELINE.toml`、`STATUS.md`。
- [H] [PHK110](https://github.com/Seanding1998/hakios-xnu-kernel/tree/a7d45752aca1c46d43e3daf83b96ebad707ea447)，重点 README 的工作范围、八步表与源码重建限制。
- [A] [K90](https://github.com/iola1999/hackios-redmi-k90-annibale/tree/c3526b4e7e24c462fafcb5695ade5b2190ff7a17)，README及 11-08/09 性能/热记录。
- [L] [diting Linux](https://github.com/royka1/diting-ubuntu/tree/c68ff47db94792fb5fd608e98d1ae489c2e592b1)，重点 `docs/BRINGUP_STATUS.md`、`ubuntu-diting/docs/KNOWN_ISSUES.md` 与 patches 0085/0086/0093/0095/0096。
- [BT] [QcaBluetooth](https://github.com/UniXNU/QcaBluetooth/tree/986d6319d1d2819b27ed2c9786776f10bf63cc76)。

补充初筛固定版本：同版 UniXNU-diting `62ff5bae93fb040f7d80a42f9187c3968d643249`（launchd启动但用户态仍有阻塞）、thor `e9c7f81a38dbca2a1ceba89ad38895d74d8ba5a9`、unicorn `a99b5c1d2128894519948eeee9faa49a7bc58021`、babylon 所指 repo `b87a4cb89377914fdb0e11482a3e9221491db817`；udon、zeta、q2q、ruyi、ios-flame、umi、LineageOS zizhan、QcomBaseband 与前轮引用 SHA 相同；uefi-pong `997ae23d4ef578a4c0529fa52dc51aa0eb7789aa`、uefi-zizhan `c498ff26d438f818a2910e099dcf64c9ea036336`；dada `f1ae906fcc43ba8f763663257724ba938fb820ab`；metal2vulkan 默认 master `219f01365dc4c8eb346816c405b5b17403793764` 只用于默认分支初筛，实际新增 runtime 以 AdrenoMetal 的 development 依赖为准；ShengNovatekTouch gitlink `3b44b1b7603eaadae942ea44ddc51bdcc48ad060`；OOSEX 上游 `oosexgang/hakios-xnu-kernel@63fc2b4633bac3af8e57dadab9f5a8d4cb940438` 仅检查近期提交，含有效旧 Data archive 回退与父节点地址 cell 解码，尚未作本机可迁移性审计。这些初筛不构成逐子系统审计。
