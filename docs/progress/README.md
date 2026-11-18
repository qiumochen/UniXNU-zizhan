# Development notes

[当前台账](STATUS.md) · [今晚分支收口](2026-11-17-codex-prep-summary.md) · [M2 方案](../plan/2026-11-17-m2-thermal-and-screen-off.md) · [M3 评估](../research/2026-11-17-m3-bootmenu-port.md) · [电池健康输入](../research/2026-11-17-battery-health-inputs.md) · [接手梳理与重新规划](../plan/2026-11-17-takeover-replan.md) · [双系统引导调研](../research/2026-11-17-dual-boot-loaders.md) · [11-17 前台账原文](2026-11-17-status-archive.md) · [11-14 回到主线与系统应用目标](2026-11-14-system-app-resume.md) · [本批无人值守交付与历史](2026-11-14-app-unattended.md) · [主线应用修复调研](../research/2026-11-14-main-app-repair-directions.md) · [Weather/Safari晚间接续调查](../research/2026-11-14-weather-safari-followup.md)

一小时人工验证： [传感器、GNSS、音频与折叠检查点](2026-11-14-sensors-gps-audio-fold.md)

Dated notes from the zizhan bring-up, in Chinese, kept as written during the work: each run (k*, u*, t*, g*) with its boot arguments, result and root cause.

Paths in the notes refer to the author's workspace:

- `src.noindex/` or `$ZIZHAN_SRC` is the Mac work directory; its `UniXNU-zizhan/` is this repository.
- `src.noindex/*.sh`, `*.py` and the workspace `tools/mac/` scripts are in `tools/zizhan/mac/` here; `tools/phone/` is `tools/zizhan/phone/`.
- `device/` was a mirror of this repository's `drivers/`, `patches/` and `scripts/`.
- Logs (`logs/*.txt`) and photos other than those in `docs/evidence/` are not published.

## 最新交接

- [2026-11-17 UI-STATUS-03 电池右上工作UI](2026-11-17-ui-status-03.md)：无诊断917B UI-only包、wholeOS读回、Home+电池设置页135×120全宽采样；当前安装statusbar-work-v1，weather回退保留。
- [2026-11-17 音频单图实机 A/B](2026-11-17-audio-graph-device.md)：MFC/SAL/TDM + WR_SHMEM→MFC 双图生命周期、真实MAP、4×480零数据V2DONE；AMP/BST/TX保持关闭。
- [2026-11-17 音频有限运行时入口实机](2026-11-17-audio-runtime-device.md)：mode38 控制页生产者水位、Ready门控、欠载拒填、4×480 ring批次V2DONE、stop排空与清理；未接HAL/未出声。
- [20小时无人值守计划](../plan/2026-11-17-20h-unattended.md)：音频单图、至多五轮音频静音资格、默认关闭运行时、UI电池收口与传感器限时解阻；仅规划，未启动。
- [Zizhan系统音频服务设计](../plan/2026-11-17-zizhan-audio-service.md)：D增量的独立service/HAL bundle、生产者水位ABI、Ready门控与设备验收；未实现/未上机。
- [2026-11-16 音频环取数与格式接线](2026-11-16-audio-ring-wiring.md)：48k/2ch HAL环取数、24-bit打包、2880B/480帧有限批次、TDM子图MFC成员核对与双kernel；未接单图/HAL服务/首声。
- [2026-11-16 夜间UI与音频批次](2026-11-16-night-ui-audio.md)：无诊断UI候选、传感器阻塞判断与11520B/960帧四槽WRITE/DONE核心；设备状态与后续边界。

- [下一轮静音无人值守计划](../plan/2026-11-16-next-unattended.md)：两小时顺序、交付与停止条件；任务状态见STATUS，本次仅规划。

- [2026-11-15 功放实际OTP易失trim](2026-11-15-amp-volatile-trim.md)：独立mode12按每颗本轮OTP8展开98允许位域，masked保留与精确读回，不编程OTP/启输出。
- [2026-11-15 功放RAM传输计划](2026-11-15-amp-ram-transfer.md)：保护固件4字节分块、完整有序读回校验与8项控制，实际输入59752字节；仅host工具，未传输或启DSP。
- [2026-11-15 功放RAM加载候选](2026-11-15-amp-ram-load.md)：默认关闭mode13，同轮真实初始化后固定RAM写/全读回，私有临时启动输入与45秒界限，不启DSP或出声。
- [2026-11-15 算法身份与保护参数](2026-11-15-amp-protection-coefficients.md)：独立mode16先核两颗实际22字算法身份/base，再原机T/B保护参数受限RAM写/完整读回；不启DSP或声音。
- [2026-11-15 HALO无声执行](2026-11-15-amp-silent-run.md)：默认关闭mode17，原机速率/MPU设置、真实心跳推进与核心停止，GLOBAL/AMP/BST输出保持关闭。
- [2026-11-15 原机音频时钟与后端](2026-11-15-audio-clock-route.md)：实际TDM后端/overlay输入已留存，静态48k双声道与96k三通道分组待活动PCM采样核对，原生时钟资格未验。
- [本机TDM端点无输出时钟资格](2026-11-15-audio-tdm-clock.md)：实际Forte标定关系、TDM模块/参数、图六步生命周期与PLL观测。
- [统一时钟租约与功放无声执行器](2026-11-15-audio-silent-owner.md)：默认关闭同线程组合、原机PLL与真实心跳/清理门槛，首声前停止。
- [2026-11-15 音频PRM控制链](2026-11-15-audio-prm.md)：默认关闭的APM readiness与digital core单次请求/释放，不设频率、不接pin/codec/graph，实际completion分别判定。

- [2026-11-15 功放原机B2补丁](2026-11-15-amp-b2-patch.md)：独立mode11固定补丁/逐项读回/关闭ACK与禁止状态，不启动DSP或声音。

- [2026-11-15 SLPI真实cmd-db采集](2026-11-15-slpi-cmd-db.md)：实际保留区定位，默认关闭只读双copy/稳定解析/完整私有导出；继承资源票和SLPI启动分别待验。

- [2026-11-15 已知屏蔽状态候选](2026-11-15-amp-owned-readiness.md)：独立mode10 exact readiness与实际快照入口，保留raw80/完整guard；本批仅主机，心跳到期不派设备轮。

- [2026-11-15 功放GENI配置证据](2026-11-15-amp-irq-config.md)：mode9实机确认master80被mask、enabled-pending0和来源/活动/数据均0；诊断通过但OTP仍跳过，下一严格masked-state分类候选。

- [2026-11-15 功放RX-LAST来源交接](2026-11-15-amp-source-handoff.md)：mode8实机slave RX-LAST已清/DMA0，但master80持续，交接失败/OTP跳过；排除只缺slave确认，下一核bit7语义/实际配置。

- [2026-11-15 功放RX初始化](2026-11-15-amp-rx-setup.md)：mode7实机RX15/16读回通过；master80持续且slave RX-LAST08000000未确认，handoff失败/OTP跳过，下一source-first交接。

- [2026-11-15 功放RX有界交接](2026-11-15-amp-rx-handoff.md)：mode6实机一次确认清bit26但RXIRQ80持续，按deadline失败/OTP跳过；真实watermark0/RFR e留证，下一核RX初始化语义。

- [2026-11-15 功放快照Busy入口](2026-11-15-amp-bus-entry.md)：实机确认idle/SE0/RXFIFO0，仅RX watermark/RX IRQ4000080置位；完整OTP仍未取得，下一步有限清理交接。

- [2026-11-15 功放清除使能位](2026-11-15-amp-safe-off.md)：两颗实机4次清零写/精确读回通过；随后OTP前置Busy未采到数据，返回与失败证据分开保留。

- [2026-11-15 功放OTP实机快照候选](2026-11-15-amp-otp-snapshot.md)：mode4主机与新双kernel通过；实机发现AMP/boost/HALO位未清，按门槛在OTP前中止并正常返回，完整快照未通过。

- [2026-11-15 功放OTP离线计划](2026-11-15-amp-otp-plan.md)：原机五种表/100字段/688位解析和易失trim位流控制通过；未采逐颗OTP或执行钥匙/trim/声音。

- [2026-11-15 功放保护内存计划](2026-11-15-amp-memory-plan.md)：静态HALO算法解码、T/B系数地址/跨别名边界和原机B2差异，只有host计划，无硬件执行。

- [2026-11-15 原机功放保护输入](2026-11-15-amp-original-inputs.md)：11文件私有来源/hash、HALO/WMDR/场景离线framing及9项控制；尚无加载写计划或声音资格。

- [2026-11-15 CS35L41数字启动与身份](2026-11-15-amp-boot-identity.md)：本机模块复位/供电绑定依据、iPadOS两颗35a40/B2与OTP通过，最终重新reset；声音仍待现场。

- [2026-11-15 GENI时序/FIFO](2026-11-15-geni-timing.md)：实际XO/DFS选择、polling准备及实机配置回读通过；reset未变/未发I2C，功放身份与声音未验。

- [2026-11-15 SE15时钟/引脚设置](2026-11-15-se15-clock-pin.md)：默认关闭有界实现，实机clock vote/GENI可读/pin回读通过；reset未变、未发I2C或识别功放。

- [2026-11-15 SE15平台前置](2026-11-15-se15-platform.md)：cape时钟匹配、默认关闭只读入口、实机发现关门/GPIO模式与预期SKIP；未访问SE或功放。

- [2026-11-15 CS35L41只读身份](2026-11-15-cs35l41-identity.md)：本机live DT/IRQ差异、32位大端身份核心、实际GENI失败回放及双kernel；尚未接SE15平台初始化或上机。

- [2026-11-15 RPMh整组发送](2026-11-15-rpmh-batch.md)：共享TCS4有限payload、真实完成/超时保留策略和两个owner入口；host/双kernel通过，无设备调用。

- [2026-11-15 SLPI启动请求核心](2026-11-15-slpi-power-lease.md)：真实cmd-db格式解析、ARC/BCM编码、完整VCD分组和保留baseline的失败边界；尚未绑定硬件owner或启动SLPI。

- [2026-11-15 SLPI共用资源](2026-11-15-slpi-shared-resources.md)：单actor有限QMP事务、互斥下SMEM分配和精确固件入口；新双kernel/host通过，SLPI时钟/带宽前置仍须落实。

- [2026-11-15 共用IPCC接收器](2026-11-15-ipcc-routing.md)：默认关闭的ADSP/SLPI通知路由、首事件窗口修正、实际owner/ADSP回放及双配置构建；设备中断资格另验。

- [2026-11-15 SLPI 离线 preflight](2026-11-15-slpi-preflight.md)：原机备份固件与25MiB carveout核对、实际MDT解析器放置和8项负控制；SLPI/SSC尚未启动。

- [2026-11-15 Hall 只读观察器](2026-11-15-hall-observer.md)：默认关闭 GPIO62 采样、实际 probe host 回放与双配置内核构建，尚未上机；[硬件适配计划](../plan/2026-11-15-ipados-hardware-adaptation.md)规定 GNSS、传感器、音频和外屏的后续范围。

- [2026-11-09 累计发布与验证](2026-11-09-publication.md)：设备仓库累计提交快进main、推送前验证与精确SHA CI状态。

- [2026-11-09 天气与定位诊断交接](2026-11-09-weather-handoff.md)：已安装候选与canonical OS370的区别、定位阻塞解除/提示取消、截图时序与网络限制、未启动v25及安全接续；机器工件留本机。
- [11-08/09 天气逐轮技术记录](2026-11-08-weather-authorization.md)：控制与干预、全部负结果和更正，当前顺序仍以STATUS为准。
- [2026-11-08 主线重排与 panel pool 128 MiB 候选](2026-11-08-panelpool-128.md)：APP-01 转为第一主线、POWER-02 降为受控支线；pool 96→128 MiB 的槽位计算、候选身份及 MVP-1 验收。v5缺ADT属性不可用；后续v6起已实际完成显示池与正常返回，天气接续见11-09交接。
- [面向用户的功能适配清单](../FEATURES.zh.md)：截至 2026-11-07 的用户版功能摘要，按已可用、部分可用、待确认及暂不支持说明，不展开技术细节。
- [当前进度与工作队列](STATUS.md)：唯一持续维护的状态入口，含任务编号、验收条件、下一动作及已验证发布/CI 基线。日期文档中的“当前”按记录当时理解。
- [2026-11-07 类似设备参考与首轮待验证方向](../research/2026-11-07-device-reference-directions.md)：从设备目录核对同 SoC、折叠及功耗/存储参考，形成 R1–R10 的假设、前置与验收；待迁移验证统一归 REF-01 和对应子系统，未改变设备资格。
- [2026-11-07 系统应用现状与持久化方案](2026-11-07-app-persistence.md)：APP-01 首轮天气前台会话检查、未完成的应用内容边界，以及 PERSIST-01 的启动链、root/Data 布局和分阶段方案。
- [2026-11-07 CI 效率调整](2026-11-07-ci-efficiency.md)：云端 macOS 构建与磁盘诊断改手动，快速回归按源码及公共依赖路径触发，本机双配置验证保留。
- [2026-11-07 整理与发布](2026-11-07-publication.md)：11-06/07 代码、计划与证据的发布范围，本机验证和本次提交的云端 CI 跟踪。
- [2026-11-07 完整锁屏与硬件休眠设计](../plan/2026-11-07-lockscreen-suspend-design.md)：原生显示请求与实际缺口、runtime PM和CPU/SoC主线、备选SYSTEM_SUSPEND、逐设备恢复、wake/投票/时间契约及功耗验收；本次为调研设计。
- [2026-11-07 电源首批无人值守实现](2026-11-07-power-first-implementation.md)：bring-up隔离、电源事务模型、PSCI/registry只读审计、g397查询轮次与真实未完成边界。
- [2026-11-07 电源完整无人值守推进](2026-11-07-power-unattended.md)：ADSP事件等待、实际显示队列owner接线、DSI FIFO执行器及长包DMA地址域前置；主机、普通设备启动和真正休眠资格分列。
- [2026-11-07 TSENS 只读观察](2026-11-07-tsens-readonly.md)：默认关闭 XNU 探针、真实地址变换、完整/精简构建、patch 格式回归及 g395 有界设备设计和结果。
- [2026-11-07 电源恢复契约](../plan/2026-11-07-power-resume-contract.md)：锁屏、熄屏、单设备暂停与平台睡眠分阶段，列明真实 owner 排空/恢复及独立返回前置；设计不等于实现。
- [2026-11-06 面向日用的后续路线](../plan/2026-11-06-daily-use-roadmap.md)：先内屏 Wi-Fi 日用基础，再声音与折叠，最终手机能力；人工验收后置、候选隔离积累，动态状态仍只在台账维护。
- [2026-11-06 日用第一批开发](2026-11-06-daily-use-development.md)：THERM-01 只读采集和本机 TSENS/EPSS 输入；BASE-01 服务策略审计、显式选择和隔离目录候选，含失败控制与资格边界。
- [2026-11-06 白天输入与电池准备](2026-11-06-daytime-input-power.md)：用户选择的 USB、音量键、电池原生状态开发顺序及晚间人工验收设计。
- [2026-11-06 BOOT 与发布后续](2026-11-06-boot-release-followup.md)：BOOT-01 分配范围诊断和 RELEASE-01 发布入口接入结果及阻塞边界。
- [2026-11-06 无人值守设计与结果](2026-11-06-overnight.md)：首帧同批分段与 trace 覆盖、当前干净 OS 控制、串行显示和网络回归；未将收集成功当作硬件资格。
- [2026-11-06 外屏与电源调查](2026-11-06-fold-power-audit.md)：本机 DSI1、外触控和 Hall 输入；真睡眠的恢复与独立返回前置条件。
- [2026-11-05 显示、首帧与内屏亮度](2026-11-05-display-and-backlight.md)：画布映射生命周期加固；首帧分段与被撤销的退步候选；L18 真实读写/恢复、原生滑条和 HID 拖动通过。
- [2026-11-05 GPU＋Wi-Fi 默认配置](2026-11-05-default-gpu-wifi.md)：默认构建和打包同时开启两者，旧 Wi-Fi 配置名只作兼容链接；精简 CI 仍明确选择。
- [2026-11-05 Wi-Fi 连接生命周期](2026-11-05-wifi-lifecycle.md)：修复原生 PrimaryTimeout、dispatcher 内同步通知和 timer 重入遍历；g337 自动入网及短时保持、g341 原生关开后的第二次 DHCP/HTTPS 与清理通过。
- [2026-11-04 Wi-Fi 设置与跨频段重连](2026-11-04-wifi-settings-and-roaming.md)：提前更新管理帧速率后，2.4↔5 GHz 两个方向都通过三次 HTTPS 和断线清理；缩短原生 InterfaceNamer 回退等待后，g309 从真实 Settings 加入完成 WPA2、DHCP、HTTPS。

- [2026-11-04 display-link 设备对照](2026-11-04-displaylink-device.md)：修正实际 UIKit 动画类名，g298 moving 95.3 fps，基线 56.7–58.4；动画尾段间隔改善，首帧约 80 ms 仍待定位。

- [2026-11-04 原生 Wi-Fi 修正与跨频段故障](2026-11-04-wifi-native-repair.md)：原生接口绑定与 manager 端口修补已有设备证据；跨频段 RDDM、Settings 入网未通过，随后转入 GPU 对照。

- [硬件适配实验计划](../plan/2026-11-04-next-hardware-work.md)：保留已完成项与场景验收设计；执行优先级以当前台账为准。
- [2026-11-03 display-link 主机核对](2026-11-03-displaylink-host.md)（历史主机构建状态；后续设备帧率已有 g298 记录）。
- [2026-11-03 音频板级审查](2026-11-03-audio-board-audit.md)（公开 zizhan DT 与 Pong 音频常量对照；未上机）。
- [2026-11-04 Wi-Fi 与 USB](2026-11-04-wifi-usb.md)：默认 Wi-Fi 路径的 DHCP、续租、断开清理和重连 HTTPS 通过；后部 USB-A 自动部署已实测。
- [2026-11-03 Wi-Fi 首轮准备与扫描](2026-11-03-wifi-bringup.md)。
- [2026-11-03 Photos 零尺寸断言](2026-11-03-photos-context.md)（最终 g264 已上机）。
- [2026-11-02 GPU：ASTC 布局与 fairplayd 回填](2026-11-02-gpu-astc-and-fairplayd.md)（含六项 GPU 工作状态、未验证项和下一步）。
- [2026-11-02 设置首次启动](2026-11-02-settings-launch.md)。
- [2026-11-01 电池](2026-11-01-battery.md)。

旧 `src.noindex` 路径用于理解历史记录；新 Mac 工作区按根目录 [CLAUDE.md](../../CLAUDE.md) 建立，直接在 clone 内编辑、提交。

- [2026-11-16 内屏状态栏与传感器电源交接前置](2026-11-16-statusbar-sensor-receipts.md)
- [2026-11-16 SLPI专属传输项接续](2026-11-16-slpi-transport-resume.md)：继承票阻塞与独立remote3 SMEM/SMP2P/GLINK元数据前置；功能与设备结果以当前台账为准。
