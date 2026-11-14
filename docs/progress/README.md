# Development notes

[11-14 发布检查点、工作量与迁移必要性](2026-11-14-native-checkpoint-and-decision.md) · [Current ledger](STATUS.md) · [11-14 首次原生启动与通俗交接](2026-11-14-native-first-boot.md)

Dated notes from the zizhan bring-up, in Chinese, kept as written during the work: each run (k*, u*, t*, g*) with its boot arguments, result and root cause.

Paths in the notes refer to the author's workspace:

- `src.noindex/` or `$ZIZHAN_SRC` is the Mac work directory; its `UniXNU-zizhan/` is this repository.
- `src.noindex/*.sh`, `*.py` and the workspace `tools/mac/` scripts are in `tools/zizhan/mac/` here; `tools/phone/` is `tools/zizhan/phone/`.
- `device/` was a mirror of this repository's `drivers/`, `patches/` and `scripts/`.
- Logs (`logs/*.txt`) and photos other than those in `docs/evidence/` are not published.

## 最新交接

- [11-14 白天原版Pong对照](2026-11-14-upstream-control-day.md)：用户授权全天手机操作、目标当日原生桌面；同stock原版工具显示大量共享缺口，full复现定位到真实Pong私有输入门槛，继续共享绑定批量重现。

- [11-14 OSSymbol证据修正](2026-11-14-native-symbol-pool-correction.md)：撤回未闭合SMR依赖的findSymbol/hash/equality绑定，当前独立缺717、NoCopy与全局槽位未资格。

- [11-14 OSSymbol 字符串池](2026-11-14-native-symbol-pool.md)：`_strnlen`独立通过后缺口717；`findSymbol` SMR/allocator依赖仍未资格。

- [11-14 原生日志格式器](2026-11-14-native-log-format.md)：修复LLONG_MIN真实UBSan trap，新完整native内核/836向量/149类表/placement/loader与双配置内核主机通过；NoCopy依赖未闭合的13项旧证明撤下，新输入独立缺718项，无collection/boot。

- [11-14 同版KDK与独立函数体检索](2026-11-14-native-kdk-and-proof-search.md)：09:00后仅主机接续；Apple签名同版参考提供154项待审名称线索，资格层清空/改错提议对照通过，vnode_mount完整契约加入后当前独立缺705项。无collection或手机操作。

- [11-14 无人值守晨间记录](2026-11-14-native-morning.md)：整夜交付、错误配对撤回、710项独立资格缺口、未启动状态及保留回退；09:00实际设备观察和监督撤销已完成，目标仍未完成。

- [11-14 原生显示handoff](2026-11-14-native-display-handoff.md)：CPU回落前恢复维护scanout的真实panel owner；未知/未确认GPU结果禁止CPU写入，实际控制块回放与原生链接通过，未做硬件试验。

- [11-14 原生导入契约](2026-11-14-native-import-contracts.md)：三个时钟KPI实际指令计算回放、字符串工厂到全局槽位核对及负控制；真实UUID候选闭包继续推进，无设备启动。

- [11-14 原生PAC与collection布局](2026-11-14-native-pac-layout.md)：实际Android EL0 PAC控制、预期错误认证进程fault与CPU affinity限制；外部PWF2解决实际collection空间拒绝，reader测试/本机OS读回通过，导入仍未闭合。

- [11-14 原生板级最小完整链接](2026-11-14-native-board-link.md)：维护中的ath11k/网络、ADSP/双电池/背光原生编译与PAC继承核对，真实全板级内核链接；旧闭包351缺口，新kernel地址重新派生，无设备启动。

- [11-14 原生应用种子与目录](2026-11-14-native-app-seed.md)：修复实际bootstrap拒绝原生应用种子的路径，完整15,701项资源解析/复制回放；229项目录保留原生可移除迁移，无设备资格。

- [11-14 本机原生OS、GPU/FST2与导入](2026-11-14-native-os-gpu.md)：独立OS/Data种子staging与读回、维护GPU及真实触控源原生内核链接；无boot包/手机写入，按新内核继续闭包识别。

- [11-14 原生根卷链接、loader与闭包](2026-11-14-native-root-loader.md)：真实UFS/AES root链接、保留本机所有权的native loader、bootstrap/UI主机结果；默认stock-kext闭包613项未解析，继续整夜工程，无新设备资格。

- [11-14 原生迁移整夜目标](../plan/2026-11-14-native-overnight.md)：用户授权无人值守至09:00，完整原生候选与桌面/基础功能为方向，中间构建结果不收工；同一对话心跳监督，保留设备安全与回退边界。

- [2026-11-14 原生迁移第一批主机结果](2026-11-14-native-core-and-inputs.md)：匹配22G100系统输入、隔离arm64e core链接、真实zizhan UFS编译与190/181/168方法PAC祖先匹配；无原生设备启动候选，继续本机电源/完整闭包适配。

- [2026-11-14 原生Pong路线前置验证](2026-11-14-native-alignment-gates.md)：新鲜Android/安装/备份前置、CPU PAC报告、arm64e编译及loader格式回放；完整源码/22G100输入仍缺，未启动原生候选。

- [2026-11-13 接手与上游增量调查](../research/2026-11-13-upstream-resume-survey.md)：Pong原生22G100、Sheng/AdrenoMetal、同SM8475 diting Linux恢复与CS35L41参考；只研究，未操作手机，设备主线与资格保持。

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
