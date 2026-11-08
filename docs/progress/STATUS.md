# 当前进度与接续入口

更新日期：2026-11-08（UTC+8）。这是持续维护的当前任务台账；历史实验仍保留在日期文档。管理规则见 [AGENTS.md](../../AGENTS.md#progress-management)，本机路径、安装状态及进程交接见仓库外 `$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`。

## 11-08 主线重排（本条优先于下方全部 11-07 段）

- **11-09 00:12 本批终态（优先）**：用户重插后v24r1已真正直接boot/新鲜pinned日志/Android返回PASS；定位页导航accepted1且Preferences启动，weatherd处正常dispatch等待。截图因绝对时钟已579s>shot150在首帧过早拍黑，全局定位状态SKIP；WiFi DMA arena失败，不能验天气数据源。v25双时点补偿包已备，但USB切换NO_FASTBOOT未发送，SKIP；job/锁清理、无心跳，不继续重复切换。实际OS仍v23隔离候选，OS370回退完整。下一步真实fastboot恢复后直接boot v25取正确时点页面，[记录](2026-11-08-weather-authorization.md)。

- **11-09 00:09 诊断接续（优先）**：用户重插fastboot后v24r1已直接boot/新鲜pinned日志/Android返回PASS，原生定位页导航accepted1且Preferences真实启动，但截图在首帧即触发（硬件绝对计数器已579s>shot150），早于页面，定位状态SKIP；本轮WiFi DMA arena失败、17IOSurface/1renderer jetsam保留，不能验天气网络。v25仅改双时点截图（间隔90s）补偿这一时序，其他运行时/已装候选/权限不变，现单次监督前置RUNNING。详见[记录](2026-11-08-weather-authorization.md)。

- **23:53 本批结果（优先）**：v23r5真实设备对照已解除locationd旧MIG1203/队列阻塞（main正常RunLoop），weatherd首次Mach按需启动，用户+中心截图确认定位设置提示，用户误点取消；天气仍北京`--`，GPS/授权成功未验证。v24b只读定位设置/天气服务采样在USB发送前NO_FASTBOOT，结果SKIP，任务已清理、无心跳。实际OS仍隔离v23候选、canonical仍OS370，回退完整，不能声称默认推广。下一步恢复连接后核对原生全局定位设置及weatherd剩余依赖，详见[记录](2026-11-08-weather-authorization.md)。

- **23:49 APP-01有效推进（优先）**：v23r5首次跑通fairplayd-off候选；locationd旧MIG1203/串行队列阻塞消失，主线程1000/1000回正常RunLoop，weatherd首次按需spawn，定位相关提示由用户+中心截图确认（用户误点取消）。天气仍北京`--`，GPS/授权成功/数据源未验证；截图缺列、1次renderer jetsam保留。下一只读诊断为原生定位设置状态+weatherd等待栈，不自动改权限。候选保持隔离、OS370回退完整，详见[记录](2026-11-08-weather-authorization.md)。

- **23:41 实际对照RUNNING（优先）**：用户确认此前为关机充电画面，原厂on-charger配置验证05c6:f000含义；手机按开后adb正常。v23r5改为Android延迟重启、手机端口断电跨过bootloader初始化后重连，23:41:00目标核对/镜像发送/临时boot通过。这是已安装fairplayd-off候选首次真正启动，OS/UI/Data/boot镜像身份保持；正等待新定位栈/天气画面/自动返回，不把单次连接成功当稳定修复。详见[记录](2026-11-08-weather-authorization.md)。

- **23:19 终态（优先）**：v23r4被动等待900秒正常到期，未发镜像；USB恢复结果FAIL、设备干预验收SKIP。job/锁已清理、Paseo心跳已删除，无自动重试。当前USB检测到`05c6:f000`，adb/fastboot均空，缺实际调试接口；v23候选仍已安装未启动，原OS370回退保留但无法在无Android/root时执行。先恢复真正adb/fastboot接口再对照或回退，[详情](2026-11-08-weather-authorization.md)。

- **23:03 当前接续（优先）**：用户开机后adb/boot_completed恢复，v23r3整窗live候选hash与前置PASS；原标准Android→bootloader仍NO_FASTBOOT、未发镜像，主机重启未修复切换。现v23r4只被动等待fastboot下重插原USB-A路径，稳定目标出现后直接临时boot同候选，不再返回Android；15min等待/30min总期限，launchd+有界Paseo心跳接续审查。候选OS已安装未启动、OS370回退保留。详情见[记录](2026-11-08-weather-authorization.md)。

- **22:57 主机重启后复核（优先）**：用户确认重启电脑，主机启动时间22:48:05，与v23r2中断吻合。USB连接仍在，但手机只提供`05c6:f000` Mass Storage，adb/fastboot均空；尚不能继续设备对照或回退。等待手机恢复真实调试/fastboot接口，未再写手机，v23已安装未启动与OS370回退状态不变。

- **22:54 终态复查（优先）**：v23r2于22:47:46被中断，未发镜像，非正常超时；job/进程/锁均已清理，无设备操作，Paseo心跳已删除。当前USB已连接但身份为`05c6:f000`且仅Mass Storage接口，adb/fastboot均不可用；不能沿用“无物理连接”描述当前状态。v23候选仍已安装未启动，OS370回退完整但缺Android/root通道。下一步恢复并核对真正adb/fastboot接口，再继续对照或回退；[详情](2026-11-08-weather-authorization.md)。

- **22:34 无人值守恢复 RUNNING（优先）**：用户重启后v23r1试验改为Android延迟7秒重启、主机先退出adb，仍NO_FASTBOOT、未发镜像。端口仅供电无连接，一次限定手机端口的hub PORT_RESET后仍未枚举。v23r2现由单次launchd被动等待稳定/目标核对通过的fastboot，15分钟等待、30分钟总期限；恢复后直接临时boot同候选、自动收日志，不再重复Android切换或VBUS。Paseo有界心跳负责本线程接续审查；超时停止，不把等待记通过。实际OS仍已安装v23隔离候选，OS370回退完整保留。[记录](2026-11-08-weather-authorization.md)。

- **22:18 最新状态（优先）**：v22r2已抓到locationd主线程等队列、工作线程1000/1000次卡在与g212同址的MIG1203同步依赖，天气仍`--`；不能据此确诊全系统权限弹窗或GPS。隔离OS370候选仅删fairplayd.A2 job，OS-only安装及整窗读回PASS，但v23再次NO_FASTBOOT、未发镜像，因果对照SKIP。**当前手机已安装该候选、未启动；canonical文件仍OS370，不能声称已回退。** adb/fastboot不可见，无活跃设备操作。恢复连接后先核对实际状态，完成对照或OS-only回退；[详情](2026-11-08-weather-authorization.md)。

- **22:11 APP-01 新设备证据**：v22r2完整采集/Android返回PASS，locationd主线程等队列，工作线程1000/1000次停在与g212逐地址相同的同步Mach栈。天气仍`--`，不是GPS或授权弹窗已确诊。下一步隔离OS370 clone仅移除反复退出的fairplayd job，保持内核/UI/Data与诊断参数，验证真实依赖阻塞；详见[诊断设计](2026-11-08-weather-authorization.md)。

- **22:05 APP-01 诊断 RUNNING**：用户直接重启后恢复完整 Android；v22r1再次NO_FASTBOOT、未发送。v22r2改主机切换时序（reboot bootloader后立即释放adb服务），同v22不可变候选成功临时boot。OS/UI/Data未改，autoreset 300/240与30分钟单次监督；正在等新日志，尚无天气/定位或权限新结论。

- **21:51 APP-01 接续更正（优先于下方旧天气归因）**：`pp-weather-v18` 原图已有“上海”与 `--`，撤回“没有城市/完全无文字”；“不请求数据、不是网络问题”也超出了已捕获日志的证明范围。定位/权限/天气服务存在，GPS、权限弹窗和天气接口尚未验证。v22 同运行时输入补齐日志与 locationd 栈，打包及 live OS preflight PASS，但 `NO_FASTBOOT`，未发送镜像；常规与延长 VBUS 恢复失败。设备主实验停止，无活跃操作，adb/fastboot 均不可见。下一步重新插拔已验证后部 USB-A，回完整 Android 后以新运行名执行该候选，见[天气授权诊断](2026-11-08-weather-authorization.md)。

11-07 全天 POWER-02 产出了诊断仪器和只读审计，但**没有任何用户可感知的功能交付**，首次面板 off/on 一次未执行。11-08 起按“最大结果、最小 MVP”重排：

- **主线 = APP-01 日用内容**。首个交付是 `panel-pool 128 MiB`（2 buffers / 5 canvas slots）下的天气可见内容。验收四条：新鲜 klog 出现 128 MiB pool 与 5/5 slots；无 `canvas slot NONE FREE`；有效 `PONG SHOT` 且非黑；Wi-Fi 64 MiB arena 与 Android 返回。第二轮同内核换参数继续，随后 Safari，验收 HTTPS 页面、地址栏输入与滚动。
- 11-08 收工状态（21:40）：分支 `power02-panelpool-128`，16 个提交未推送；设备可用（`pp-wake-v17` 主屏 lit 25.2%），OS 已回到 OS370（窗口 `a0187c09…`、UI 库 `c3897b10…`）。调试基建已落地：`pong_shot_step`、`pong_shot_rect`（全分辨率裁剪）、`round-report.py`。天气仍未修好：App 不请求数据、`weatherd` 零行，最可能是无定位/无城市。**已撤回**此前"OS 刷新导致回归"的判断——黑屏来自从 fastboot 起步（跳过 klog arm）或缺少上滑手势。新发现：`MiniIOSurface` 画布分配失败（81 MB 已用时即失败，1137 次）与 CS4K 同属"内存充足但内核映射分配失败"，疑同一根因。接续细节见仓库外 `work/CURRENT_HANDOFF.md`。
- 11-08 进展：v3/v5 的启动失败根因为 ADT 缺 8 个 `cluster-type`（`PONG_ADT_CLUSTERS` 只在 `ci_boot_ufs.sh` 导出，直接调用打包脚本会丢；`--skip-image-verification` 又跳过 `audit_adt`）。修正后 v6 一轮完成：128 MiB pool / 5 slots / 完整截图 / Wi-Fi arena / 1292 帧 / Android 返回全部通过。天气页未出现的原因为合成触摸周期相位（截图前 0.33 秒刚上滑回主屏），不是渲染故障；`SBRendererService` 的 16 MB 限制来自恢复的 `jetsamproperties.J171.plist` inactive 硬上限。见[本轮记录](2026-11-08-panelpool-128.md)。
- **POWER-02 降级为受控支线**。重新进场的条件是两条具体证据同时存在：启动日志出现 `ZIZHAN_DSI_DMA: buffer ready`，且 128 MiB 布局下显示域仍为 `IdentityObserved`。在此之前不再产出新诊断模型、不再开只读审计轮、不执行面板 off、锁屏或真睡眠。
- **唯一设备主实验**同时只能有一轮；每轮结束当场写记录，未记录候选视为不存在（11-08 00:31–00:44 的 v3/v5 即因此在本机停留 16 小时无人接续）。
- 工作树积压（15 修改 + 24 未跟踪）按“运行时代码 / 文档”两批提交，不再以“保留并发改动”为由长期不提交。

## 当前开发方向与验收安排

11-06 用户要求：面向日用重新规划，人工验收先积累。阶段设计见[日用路线](../plan/2026-11-06-daily-use-roadmap.md)。下方 11-07 各条保留当时授权与记录，不再构成接续顺序。

- 先补内屏 Wi-Fi 日用基础：温控、启动/数据可靠性、锁屏恢复/待机、网络与常用应用；随后音频及折叠，最终蜂窝与相机。完整手机资格仍需通话、短信等独立闭环。
- 11-07 用户再次授权按优先级继续无人值守。THERM-01 已完成默认关闭、有界一次的 XNU TSENS 观察及 g396 两组32路有效寄存器读数；下一步为有界多时点/新鲜度语义及真实热策略调查，仍无温控。POWER-02 已有分阶段恢复契约，BASE-01 完整主机候选保留。热负载、真睡眠、出声或折叠仍按各自前置和集中验收安排推进。UX-01 保留调查进度，优先级为 P2。
- 11-07 最新要求转入POWER-02完整调研设计：覆盖原生锁屏/自动息屏、各设备runtime PM、CPU/SoC深驻留及可靠唤醒；[完整方案](../plan/2026-11-07-lockscreen-suspend-design.md)已形成。主线建议为原生策略＋设备低功耗＋分层CPU idle，SYSTEM_SUSPEND作为需查询验证的备选；本轮只有源码、DT和Android只读调查，无电源态写入或睡眠试验。后续先做原生显示请求/bring-up隔离、事务owner模型和固件/wake/TCS只读清单；THERM-01继续作为独立前置。
- 用户随后授权按方案无人值守实施，人工验证留最后；当前[首批实现](2026-11-07-power-first-implementation.md)推进bring-up隔离、display power观察、事务模型和固件/板级只读清单。UI隔离候选编译/签名及6项实际CF主机回放已通过，未安装；其余项逐项闭环，不把候选、模型或只读查询升级为硬件休眠资格。
- 11-07 用户再次要求以完成 POWER-02 全部内容为目标无人值守推进，尽可能自闭环，人工验证积累。已按[本批记录](2026-11-07-power-unattended.md)从真实显示关闭/恢复事务继续，随后逐设备 PM、CPU/共享域与唤醒；既有基线与首次硬件试验前置不变。
- 人工与长时场景验收统一留在下方“待集中验收”表，尚无执行日期。主机检查和构建随开发完成；候选按源/输入/产物身份隔离保存，设备验证前不推广默认基线。
- POWER-01 聚焦电池与原生管理状态；原任务中的真正睡眠/唤醒拆为 POWER-02，保留既有调查和未通过结论。新增任务均为规划项，不能视为已有实现。
- 11-07 用户新增待办：后续先做一轮系统应用实际情况检查，再根据检查结果决定应用适配顺序，归入 APP-01。逐应用区分启动、内容显示、核心操作与联网能力；系统 HTTPS 通过不能替代 Safari 网页验收。此次仅登记待办，执行时间待安排。
- 11-07 用户新增待办 PERSIST-01：后续探索脱离电脑的持久启动方案，以及重启/异常退出后的数据保存可靠性。当前系统镜像已落盘，但仍用临时 `fastboot boot`，不能把镜像存在等同于独立开机能力。此次只登记，保留现有开发主线和不刷写启动分区的约束。
- 11-07 APP-01/PERSIST-01 首轮无人值守检查已完成[记录](2026-11-07-app-persistence.md)：天气 `com.apple.weather` 前台会话有多次 resumed 证据，但本轮无有效截图，且出现一次 canvas slot/IOSurface 内存不足；天气内容、核心操作和联网仍未通过，Safari/设置/时钟/照片本轮未完成。Android A/B 启动输入、userdata 内 pinned root、OS/SystemOS/Data 窗口和回退边界已只读核对；持久化探索方案形成，独立开机和 Data 跨重启可靠性未验证。
- 11-08 00:31–00:44 已存在两个未上机候选 `out/app01-panelpool-128-device-v3` / `-v5`（pool 96→128 MiB；loader 已重建且不含撤回的 `pong-dsi-command` 字符串）。v5 身份：image `94d04d5f…a4d7`、loader `1f51ed2b…b8e2`、kernel `66553850…08e3`、ADT `c11cebc3…772f`；`stage/EFI/UniXNU/pong-boot-manifest.json` 仍为 APP-01 天气参数（`pong_shot=100`、`tsa=1118`）。未启动、未记录；启动前须复核 loader 确为隔离构建（本文档记录 `dc607a1d…dbb` 为 11-07 的干净 loader，v5 使用的是 11-08 重新构建的 EFI，二者不可混用）、Wi-Fi arena 预算与回退。见[本轮记录](2026-11-08-panelpool-128.md)。
- 11-07 用户要求对照 UniXNU 设备目录寻找类似设备，先形成一轮待验证方向。已完成[首轮参考调查](../research/2026-11-07-device-reference-directions.md)，登记 REF-01；R1–R10 分别接温控、显示恢复、共享供电/IRQ、CPU idle、持久启动/Data、启动分配、折叠、音频、应用/GPU及蓝牙/蜂窝。先借 SM-T733 的热策略失效语义、Pong 的显示资源生命周期、Sheng 的共享 owner 做主机差异审计；POWER-02 主实验与现有验收前置保持。方案尚未移植/设备验证，目录及继承记录不能作为本机新资格。

## 已验证基线

- 最近完成硬件验收及四项 CI 的发布提交：`de0d977d654f054da22896de41596f4ba5610a89`。这是功能基线，不要求后续纯文档提交与该 SHA 相同。
- 默认 `configs/zizhan_ipados18_gpu_defconfig` 同时开启 GPU、Wi-Fi 和内屏亮度；旧 Wi-Fi 配置名为相对符号链接。默认测量探针、合成触摸、自动 UI 动作关闭。精简 CI 显式选择自己的配置。
- 原生 Settings 选网加入、保存凭据自动加入、开关后第二次 DHCP/HTTPS、断线路由/DNS 会话清理以及 2.4↔5 GHz 不同 BSSID 重连通过。当前接收仍为 polling，短时通过不等于长稳完成。
- L18 内屏手动亮度可用：原生滑条 25%/65%/50% 对应实际 DCS 512/1331/1024，HID 拖动有匹配硬件读回并恢复。正常 60/120 Hz 启动、完整截图和 HTTPS 通过；没有光度校准或睡眠恢复结论。
- 面板槽真实缓存映射生命周期已加固。g342 历史映射失败仍是实际故障，未稳定复现；多轮回归通过不能保证永不再现。
- display-link 实际 UIKit 类名修复有效，g298 合成 moving 95.3 fps，基线约 57 fps；不是全局 120 fps。主屏静止触摸首帧仍约 80 ms。

## 已纠正结论与撤销实验

- g369/g371 的旧 fence “差一笔”不能证明 GPU 长停。g374：等待快照 763/762，实际提交/完成 763/763；g375：等待快照 664/663，实际 665/665。已修正诊断，原 ring 协议未改变。
- g365 提前 CA flush 候选使首帧中位数从 g363 的 79.76 ms 退步至 301.68 ms，已完全撤销。没有接受新的首帧优化。
- IB-list 边界协议候选仅做过主机研究，未上机、未发布。不得把它写成已生效的修复。
- 旧 g292–g295 的 Settings/跨频段失败、旧默认 Wi-Fi 关闭、旧准备态 g298/g342 不是当前基线。

## 当前工作队列

状态：`TODO` 待开始，`IN_PROGRESS` 进行中，`BLOCKED` 有明确外部前置条件，`HOST_VERIFIED` 仅主机通过，`DEVICE_VERIFIED` 对应设备条件通过，`DONE` 本项验收完成，`REJECTED` 候选撤销。11 月 6 日获授权开始四项无人值守工作，设计及边界见[本轮记录](2026-11-06-overnight.md)；实际设备启动单独登记。

| ID | 优先级 / 状态 | 范围与验收条件 | 下一步 / 证据 |
| --- | --- | --- | --- |
| APP-01 | P1 / IN_PROGRESS | 主线。逐应用记录启动、内容显示、核心操作、联网、输入及前后台表现；至少覆盖 Safari、设置、时钟、天气、照片，产出状态表并据此定序。 | v23r5解除旧locationd阻塞并显示定位提示，用户取消，天气仍`--`。v24r1直接boot/新鲜pinned日志/Android返回PASS，原生定位页导航accepted1与Preferences spawn，但截图绝对时钟触发过早，开关状态SKIP；weatherd正常dispatch等待、WiFi arena失败，网络未验。v25双时点补偿包已备、NO_FASTBOOT未发送，无owner/心跳。候选保持隔离、OS370回退完整；下一步真正fastboot下直接接续v25，见[诊断](2026-11-08-weather-authorization.md)。 |
| POWER-02 | P3 / BLOCKED | 完整原生锁屏/自动与手动熄屏、按后台任务进入真实低功耗并可靠恢复；静默、联网、后台任务与功耗分场景验收。支线 MVP 仍为原生显示请求→内屏真实 off→wake→可见首帧。 | 11-08 起降级为受控支线，不再产出新诊断模型或只读审计轮。解阻条件（须同时满足）：启动日志出现 `ZIZHAN_DSI_DMA: buffer ready`；128 MiB 布局下显示域仍 `IdentityObserved`。在此之前不执行面板 off/锁屏/睡眠。既有结果保留：v5 只读快照 PASS（SMR3=0x84022800、S2CR3=3、CB3 SCTLR=0xe0、FSR=0x400、11 reads 0 write）；FIFO-v2 正常 8 包 461µs、CP1836/1836；memory-v2 因 ADT `pong-dsi-command` 字段早期 FAIL 且该字段已撤回；长包 DMA 需 IOVA 而非物理地址。见[本批记录](2026-11-07-power-unattended.md)。 |
| THERM-01 | P1 / IN_PROGRESS | 本机温度传感器、真实热策略与 CPU 限频/恢复；输入来源、单位、失效处理和负载响应可核对。 | g395温度SKIP保留；按v2 VALID纠正后，g396两组2.8/32路有效 last-temperature 读取PASS（35.5–46.9°C），34项host及完整/精简内核通过。原supervisor因Android未完成记FAIL；46.224秒后有界复核boot_completed=1，返回单独PASS，未改旧记录。下一步有界多时点/更新与过期语义、映射交叉核对及vendor热策略输入；无温控或限频资格。见[本轮记录](2026-11-07-tsens-readonly.md)。 |
| BASE-01 | P2 / HOST_VERIFIED | 以现有 Darwin24/UFS 链路得到可追踪、可重建和可回退的本地日用候选；源码、OS/UI/helper、服务策略和 manifest 对齐。 | `os370-full-candidate-v3` 已按显式 OS370 服务存在性策略完整 prepare/apply/finish；source/input/prepared image/policy hash 固定，APFS readback、Data roundtrip、24项服务/runtime审计通过。完整镜像只在仓库外；未安装/启动，OS370 仍是设备基线。验收暂存；设备批次执行前重新preflight并登记变量/回退。默认策略审计仍指出 fairplayd 漂移。见[本批记录](2026-11-06-daily-use-development.md)。 |
| USB-01 | P1 / DEVICE_VERIFIED | 后部 USB-A 有界稳定枚举、目标检查、临时启动、autoreset、ramoops 取回及 Android 返回。 | g390 单轮通过，包含两次等待期记录端口 VBUS 恢复；只证明此条件的一次完整往返，不代表长期失败率或 USB-C。见[g390与下一项设计](2026-11-06-daytime-input-power.md)。 |
| UX-01 | P2 / IN_PROGRESS | 降低主屏静止首帧延迟；保留相同输入和可见内容控制，报告有效样本数、median/p90，确认改善且无内容/稳定性退步。历史控制 17 次，79.76/87.87 ms；11-06 当前 OS 控制 16 次，73.608/86.020 ms，不当作优化收益。 | g379 已取得三个真正重叠 GPU trace 的静止样本，主要延迟在提交前；下一步用有界 CA commit/编码观察细分前端，候选仍需至少 15 样本、median 改善 ≥10%、p90 不退步及独立画面验收。保留调查、排在日用基础之后；见[本轮记录](2026-11-06-overnight.md)。 |
| GPU-01 | P1 / DEVICE_VERIFIED | 罕见冷启动画布失败的持续回归与复现；每轮完整画面、映射结果、真实 CP 完成及退出状态可核对。多轮成功仅登记覆盖，故障根因另行验收。 | g380/g381 两轮有界正常启动覆盖完成，CP1843/1843、1777/1777，各四张完整主屏采样、HTTPS及返回；02:02 原始日志/PNG逐像素和macOS独立解码推翻此前残图误判，驱动诊断候选已撤回。仅本轮覆盖通过，g342 根因未解决；见[本轮记录](2026-11-06-overnight.md)。 |
| BOOT-01 | P1 / IN_PROGRESS | 诊断真实共享缓存签名存储分配失败；保留错误处理，在相同资源输入下取得可解释的分配与完整启动证据。 | 失败路径 kernel_map size/free/largest 诊断已实现，精简CI和完整默认主机内核构建通过。g393完整profile启动/返回通过，但 fresh klog 未复现CS4K失败，也没有 map summary；根因仍未知，不重复同条件。见[后续结果](2026-11-06-boot-release-followup.md)。 |
| PERSIST-01 | P2 / HOST_VERIFIED | 探索 iPadOS 持久化方案，分别覆盖脱离电脑的冷启动/重启进入、Android 共存与故障恢复，以及 Data 在正常重启和异常退出后的完整性。探索阶段验收为形成基于本机启动链与存储约束的方案比较、前置条件、风险、回退及分阶段验证计划；实际能力须另有设备证据。 | 已完成 Android A/B 启动输入、userdata/pinned root/klog extent、OS/SystemOS/Data layout 和当前临时 `fastboot boot` 边界的只读核对，形成方案比较与分阶段验证。当前不刷写启动分区；下一步先做主机 boot image 设计审计，再用临时启动做 Data sentinel 正常/异常返回测试。独立开机、Android 共存和 Data 跨重启可靠性未验证。见[本批记录](2026-11-07-app-persistence.md)。 |
| NET-01 | P2 / IN_PROGRESS | 不同 SSID/子网切换、自然 T1、长时运行分别验收；旧 session/路由/DNS 清理，新 DHCP 租约和 HTTPS 都有证据。 | g387同网有界窗口（启用限频原生HTTPS witness）28次成功，跨度27.89分钟、末端≤64.3秒、0见证失败/链路丢失、三张晚期图通过；不是无探针功耗或长期资格。helper-off对照未上机：g388/g389均传输前NO_FASTBOOT；已恢复OS370。自然T1、不同SSID/子网及helper-off行为仍待验。见[本轮记录](2026-11-06-overnight.md)。 |
| POWER-01 | P2 / IN_PROGRESS | 电池/充电真实状态、原生管理状态及 ADSP 开销；健康页修复独立判定，睡眠/唤醒已拆为 POWER-02。 | g391 ADSP 阶段1–13、电池发布、Settings 健康页诊断完成；`getBatteryHealthServiceState=-1`、`getManagementState=0`、spinner仍在，健康状态未修复。先追真实服务输入；人工页面/充电器核对留集中验收。见[本轮设计](2026-11-06-daytime-input-power.md)。 |
| IO-01 | P2 / IN_PROGRESS | 本轮选定音量实体键，验收真实 PMIC→HID/通知→原生音量动作、短按/长按/连按与释放。蓝牙、其他传感器另行资格。 | g391 已确认内核按键识别、HID 服务和 `SBVolumeControl` 路由 ready；没有物理按键事件，短按/长按/连按/锁屏留集中验收。见[本轮结果](2026-11-06-daytime-input-power.md)。 |
| AUDIO-01 | P2 / TODO | 本机 CS35L41 音频链；板级输入、启动、播放/停止/静音及恢复分段验收，麦克风输入另建链。 | 复核 live DT；先准备隔离只读身份探针，再核对固件/调音及 ADSP 路由，不能复制 Pong TFA9873 常量。见[音频审查](2026-11-03-audio-board-audit.md)。 |
| BT-01 | P3 / TODO | 本机蓝牙传输/固件与原生栈，发现/配对、HID、音频分别建链及验收。 | 先做只读资源和接口盘点，再选择有界探针；音频与待机恢复分别列依赖。当前无本轮蓝牙资格。 |
| CAM-01 | P3 / TODO | 本机相机；传感器/ISP/固件、单摄预览、拍照与 Photos 保存，随后录像和其他镜头。 | 先盘点板级输入、缓冲及原生接口，选择最小闭环；新规划项，无本轮实现或设备结果。 |
| DISP-01 | P2 / TODO | 自动环境光、光度校准、睡眠恢复和运行中刷新率切换分别验收。 | 先明确电源/传感器前置条件；当前仅承认手动亮度和两种刷新率正常启动。 |
| FOLD-01 | P2 / BLOCKED | 外屏独立 60 Hz 测试图、真实帧推进及熄屏恢复；随后外触控、手动切屏、Hall 自动切屏和失败恢复。 | 11-06 主机核对确认 DSI1、SE19 外触控及 Hall GPIO62；真机前置为人工固定姿态、采集交接和观察出光/触控，集中验收尚未安排；继续独立适配准备。见[调查](2026-11-06-fold-power-audit.md)。 |
| GPU-02 | P3 / TODO | IOSurfaceSharedEvent listener、动态壁纸及 ASTC/多任务/Photos 等更广应用覆盖。 | 单场景截图与真实完成验收；保留动态壁纸当前限制及 fairplayd 安装/全量重建差异。见 [porting.toml](../../porting.toml)。 |
| MODEM-01 | P3 / TODO | 蜂窝数据与完整手机能力；固件/SIM/注册/数据、通话、短信和待机恢复分别验收，当前未实现。 | 先做本机资源/驱动链调查及独立计划；通话依赖音频及电源恢复，不从 Wi-Fi 或注册成功推断。 |
| NET-02 | P3 / TODO | 真实 GIC MSI 接收、企业认证、SAE、6 GHz 各自独立资格。 | IRQ驱动收包/真实wake/WoW子能力先作为POWER-02的P1联网深睡前置推进；认证/SAE/6GHz仍按本项后续扩展。先审查硬件/固件及环境，不能从WPA2或polling成功推断IRQ与睡眠支持。 |
| REF-01 | P2 / TODO | 验证首轮设备参考 R1–R10 的可迁移性并筛选最小候选；每个方向记录固定上游版本、本机已有/新增/不适用部分、假设、前置、单变量验收及回退。实现与设备结果归对应子系统，不从外部记录提升本机资格。 | [首轮方案与来源](../research/2026-11-07-device-reference-directions.md)已形成，迁移验证未开始。先 R1→THERM-01 的读数新鲜度/失效回放，R2→POWER-02 的显示源退役/首帧，R3→POWER-02/NET-02 的共享 owner/IRQ审计；R4 CPU idle、R5 Data/独立启动、R6 分配归因、R7 折叠、R8 音频、R9 应用/GPU、R10 蓝牙/蜂窝按各自主任务与前置进入批次。已有 Pong 派生代码先查差异，不重复移植；没有成熟双屏/SM8475深睡样板的缺口保留，当前 POWER-02 主实验不变。 |
| RELEASE-01 | P3 / BLOCKED | 接入上游统一 package 模板与 action，`--check` 和完整本地构建通过。 | 已加入上游原样 `scripts/package.sh` 和 22H374→GPU defconfig 映射。当前工作区 UniXNU 缺少 `release` 命令；含该命令的审查 checkout 不含 22H374 profile，且通用 build/pack 步骤不调用 Darwin24/UFS 专用 kernel/prelink/boot 入口。外部前置为上游支持 Darwin24 自定义 recipe，随后继续接入 UEFI/local inputs、workflow 并完成真实构建；现有链可复现性由 BASE-01 独立推进。见[后续准备](2026-11-06-boot-release-followup.md)。 |

既有 `IN_PROGRESS` 表示保留中的开发/调查，不表示手机正在运行试验，也不代表新主线已开工。只保留一项设备主实验；下一次设备试验前记录实验设计和验收阈值，尚未启动的轮次不写 `RUNNING`。

## 待集中验收

本表是验收债务与前置条件，不代表每项都有可直接上机的修复包。人工操作和长时场景先积累，不再绑定 11-06 晚间；设备、候选身份和批次在执行前重新核对。

| 任务 | 当前就绪程度 | 待验内容 / 前置 |
| --- | --- | --- |
| IO-01 | 已有识别及路由 ready 证据，未有实体事件 | 加减短按、长按释放、交替连按、锁屏；与原生音量动作对应，声音联动待 AUDIO-01。 |
| POWER-01 | 读数及健康页诊断可观察，健康页尚未修复 | 页面与充电器拔插核对；管理/服务状态修复后另验，不把现状核对记为修复通过。 |
| NET-01 | 有限单网有探针窗口通过，其他场景未通过 | helper-off、不同 SSID/子网、自然 T1、分级长稳；先核对真实租约和测试网络条件。 |
| BOOT-01 / GPU-01 | 有诊断或有限启动覆盖，无根因修复候选 | 随有新输入的开发轮次保留故障现场及完整内容/完成证据，不重复不变轮次等故障。 |
| FOLD-01 / POWER-02 / AUDIO-01 | 尚有实现或前置缺口，不能直接联合验收 | 分别准备显示交接、恢复/独立返回、板级输入/音频链；首次出光/折叠、真睡眠和实际声音需人在场。 |
| THERM-01 | 默认关闭的一次 XNU 寄存器观察已通过 g396，热控制未实现 | 待多时点读数更新/过期、映射交叉核对与真实热策略；Android 4项其他温区 ERROR 保留。仅一次VALID读数，不代表校准、新鲜度、持续监控或热控制验收。 |
| BASE-01 | 完整本地主机候选已验证，未安装/启动 | 设备功能验收暂存；按集中批次冻结候选/hash/OS370 回退和步骤。原默认策略与显式候选差异继续可见。 |
| APP-01 | 天气已显示城市名与温度占位符，内容/定位/授权/天气联网尚未完成资格 | v22 未发送镜像，因 USB 不可见暂停；恢复后先采定位依赖栈与权限事件，再验真实天气内容。Safari 仍须实际 HTTPS 页面、地址输入与滚动；锁屏恢复等缺少前置的场景单列 SKIP。 |
| UX-01 | 既有调查阶段，未新增可验候选 | 产出后登记身份、主机验证和批次依赖，再做同候选联合日用验收。 |

## 已完成项目索引

| ID | 状态与验收范围 | 证据 |
| --- | --- | --- |
| NET-BASE | DONE：原生设置加入、自动加入、跨频段不同 BSSID、原生开关第二次连接及短时连接保持。 | [设置与跨频段](2026-11-04-wifi-settings-and-roaming.md)、[生命周期](2026-11-05-wifi-lifecycle.md)。 |
| CFG-BASE | DONE：本地默认构建和打包统一 GPU/Wi-Fi；当前默认也含亮度。 | [统一配置](2026-11-05-default-gpu-wifi.md)、[最终显示准备态](2026-11-05-display-and-backlight.md#最终准备态与验证范围)。 |
| DISP-BL | DONE：实际内屏 DCS 读写/恢复、原生滑条和 HID 拖动读回、正常 60/120 Hz 回归。 | [显示记录](2026-11-05-display-and-backlight.md)、[脱敏证据](../evidence/2026-11-05-display-and-backlight.txt)。 |
| GPU-MAP | DONE：保留真实面板映射、受限子描述符租用与失败槽排除的加固；长期资格归 GPU-01。 | 同上；源码 `88e5106dca99c03b9be1af02ceae5b041480a83d`。 |
| GPU-DIAG | DONE：等待快照与真实 CP 完成分开报告，有界真实 outstanding 观测及 60/120 Hz 对照。 | 同上；源码 `9d7afd134e56d14dcdd2d0862f3a18f49427b49a`。 |
| UX-FLUSH | REJECTED：提前 CA flush 候选性能退步，代码已移除。 | g363/g365 同场景控制；后续不得默认重新启用。 |
| DOC-01 | DONE：建立进度管理规则、唯一当前台账和本机交接入口，纠正旧导航中的过时状态。 | [AGENTS.md](../../AGENTS.md)、本文及仓库外本机 handoff；相对文件链接、任务 ID 唯一性、公开路径和既有四项 CI 引用核对通过，`git diff --check` 通过。本项未改变硬件资格。 |
| DOC-02 | DONE：按已有真机记录整理面向用户的功能清单，只描述可用情况与使用限制。 | [功能适配清单](../FEATURES.zh.md)，README 和进度索引已加入入口；38 项功能区分已可用、部分可用、待确认及暂不支持，保留高刷、Wi-Fi 长稳、电池健康、锁屏待机及应用覆盖边界。相对链接、状态用词、任务 ID 和 `git diff --check` 通过；仅文档整理，无新设备验证或硬件资格变更。 |
| CI-01 | DONE：CI 触发策略调整，本机双配置要求保留；记录检查自动、快速回归按相关路径运行，macOS 内核与磁盘诊断分别手动运行。 | [CI 效率调整](2026-11-07-ci-efficiency.md)；含同期新增面板 / ADSP 过滤，40 个路径场景、63 个输入覆盖、actionlint 与增量记录检查通过。`80c8deb` 四项自动检查全成功，总等待 24 秒；文档 `cc10ff9` 仅记录检查、9 秒成功；面板过滤提交 `bb3bab5` 日用回归 14 秒成功，Porting record 首次失败后已按要求补证据，复核提交 `4c21486` 记录检查 8 秒成功。均未触发 macOS，无内核或设备改动。 |

## 发布与 CI 基线

当前触发策略见 [CI-01](2026-11-07-ci-efficiency.md)：记录检查保持自动，三组快速回归按相关路径触发，macOS 内核 / 磁盘诊断只手动运行。下列旧发布记录保留当时实际触发范围；后续只跟踪本次 SHA 实际触发的工作流，未触发项不记 PASS。内核 / 驱动变更仍在本机验证精简 CI 和完整设备配置。

以下均是 **`de0d977d654f054da22896de41596f4ba5610a89`** 的已完成结果，记录于 2026-11-05 19:41（UTC+8）。后续提交要核对自己的运行，不继承该绿色状态。

| 工作流 | 运行 ID | 结论 |
| --- | --- | --- |
| Display host regressions | 42017140568 | SUCCESS |
| Porting record | 42017140516 | SUCCESS |
| Wi-Fi host regressions | 42017140486 | SUCCESS |
| zizhan macOS build | 42017140373 | SUCCESS，disk/kernel |

本地完整默认和精简 CI 内核均构建通过，显示/Wi-Fi 主机回归通过；记录结构及相对本轮改动前发布提交 `3b3c52fb5ba11b0a185703d7832fd567c94e770e` 的 area 检查通过。云端精简内核不代表完整设备镜像的硬件能力。

后续文档提交 `6d074ef09e5777cecfa23612e626ce2c3878153e` 的四项也已 SUCCESS：Porting record `42053769068`、Wi-Fi host `42053768932`、Display host `42053768861`、macOS build `42053768684`；最后一项于 2026-11-05 23:43:58（UTC+8）完成。

11-07 用户要求整理并发布 11-06/07 既有内容，见[本次发布记录](2026-11-07-publication.md)。主提交 `45c7037b5d42c203b2a624f83437bf49f29792a0` 已推送；Daily-use host 的私有DTB依赖已由 `314744f33cb10303edc78e9cbb66b4aa4409c949` 修正，该提交Daily-use、Display、Wi-Fi云端SUCCESS，但Porting record因旧network area认领全部测试而FAIL，正在收口ownership。电源测试已改人工最小FDT，隔离回放及本机67项日用host通过；修正后的发布SHA仍须单独跟踪五项CI。运行时代码与g397冻结清单一致，已有完整默认/精简内核构建通过，不重复相同构建。g397已覆盖有界Android完成等待的正常返回，没有新的睡眠cycle资格。

最终发布验证更正：`477417e2d8daf9efbb74e78dc98f40a55f8fbe96` 的 Porting record、Daily-use host、Display、Wi-Fi 与 macOS build 五项云端 CI 全部 SUCCESS，运行ID见[本次发布记录](2026-11-07-publication.md)。电源清单回归已改用人工最小FDT，本机日用host共67项通过；运行时代码与g397冻结清单一致，无新的睡眠cycle资格。

## 每轮更新与交接清单

1. 启动：记录任务 ID、baseline/dirty state、假设、控制、唯一变量、验收条件和回退；检查既有进程和真实安装状态。
2. 验证：记录命令、完整源 SHA、输入身份、实际结果和证据位置；主机、设备、CI 分列。缺失证据不填成功或零耗时。
3. 收口：更新此台账、对应日期记录及受影响的 porting areas；记录拒绝的实验和未覆盖条件。
4. 交接：本机 handoff 写明准备/已安装/已启动三种状态、回退、活跃进程、提交/推送/CI、下一条具体动作；长运行必须有 deadline 和日志入口。
5. 发布：审查公开 diff，正常提交；授权推送后跟踪相同 SHA 的所有触发工作流。纯文档变更核对链接、状态一致性和 diff，无需重跑设备或本地内核。
