# POWER-02：完整无人值守推进记录

2026-11-07 用户要求以完成 POWER-02 全部内容为目标，自行实现和验证，需要人工的场景积累到最后。入口源码为 `477417e2d8daf9efbb74e78dc98f40a55f8fbe96`，保留入口已有 README、功能清单、进度导航及 STATUS 文档改动。当前台账仍为 [STATUS.md](STATUS.md)，本记录保存本批设计和结果。

## 执行顺序与边界

1. 核对原生显示请求 ABI、本机 L18 命令与 DSI 所有权；实现有界显示事务，单独验证排空、命令完成、超时隔离、恢复及首帧条件。
2. 接入原生显示策略与 presenter/VSync 生命周期，准备原生锁屏及自动息屏候选，保留真实认证和数据保护语义。
3. 依次处理 ADSP、GPU、触控/实体键、WLAN 和 UFS 的活动引用、排空、低功耗及恢复；不以尚未实现的回调宣称具备 PM 能力。
4. 核对 CPU 目标对象退出契约、固件状态格式、wake、RTC/PDC、RPMh/TCS 所有权与计时，完成可验证的上下文和准入链。
5. 完成可无人值守的主机故障回放、完整默认/精简内核构建，以及具备可靠返回的设备轮次；按场景积累人工验收步骤。

首次面板休眠、真睡眠、物理唤醒及未知跨目标态的返回能力仍需人在场；这不阻止独立源码实现和主机验证。只有临时 fastboot boot，不刷启动分区，不移动 pinned root，不关闭真实 panic。没有跨目标态独立返回证据时不执行系统深睡。

## 入口核对

进程表未发现设备 cycle、installer、waiter 或内核构建；本次未据此推断手机实时状态。已安装基线仍按最后验证的 OS370，bring-up UI 和 BASE-01 候选未安装。g397 只证明只读查询及普通启动返回，不能作为深睡返回资格。

本批主机发现：L18 本机 DT 的 off 序列有 7 个包，包含 display-off、sleep-in 以及后续厂商命令；不能用通用 `0x28/0x10` 两包替代完整序列。on 序列包含大于现有 timing DSI helper 60-byte 上限的包，不能直接复用其发送器或忽略失败包。g397 唯一原生请求是一个 scalar `1`，不足以单独确定全部 power state 和完成 ABI。

后续结果逐项追加。尚未执行的设备场景为 TODO/SKIP，主机通过与设备通过分列；本记录创建不改变硬件资格。

## 本批结果：ADSP 事件等待与 CPU/面板主机候选

ADSP 事件等待候选已通过主机 ASan/UBSan 回放，并以 `zizhan_adsp_events=1` 在临时设备启动中验证。fresh klog 为 1,337,747 字节，deadline 返回、无 panic，Android `boot_completed=1` / product `zizhan`。日志显示 IPCC/GLINK 事件唤醒，待发送队列进入有界 1000 ms 重试，ADSP 达到 stage 13；不把这次结果扩大为 ADSP 休眠恢复或整机低功耗资格。

CPU 目标退出契约的主机默认/精简内核均构建通过。`ArmCpu::haltCPU` 不再从调用 CPU 直接关闭目标 CPU 的 timer/GIC 或调用 CPU_OFF；由 Darwin24 `processor_sleep` 调度目标处理器自己的退出 continuation，`quiesceCPU` 只接受本地 CPU 上下文。这个修正没有开启系统睡眠。

L18 DT 程序解析和完整 off/on 预检已通过。off 每个 timing 7 包、总等待 130 ms；on 每个 timing 55 包、总等待 120 ms；一个 90-byte on payload 超过 64-byte FIFO store，需要 DMA-capable command buffer。面板事务在长包 DMA executor、DPU/DSI/PHY/clock 所有权及恢复首帧未完成前拒绝进入 off；因此本批没有真实面板掉电。

**已撤回的早期DMA候选**：先前把物理连续buffer写入DSI DMA offset的实现仅编译通过，未在设备上触发长包。15时后厂商源码复核确认offset输入是DSI所属SMMU的IOVA；物理地址不等于该IOVA，故撤回可执行长包DMA实现。保留程序解析/内存包布局编码和负控；在本机显示stream/context及映射owner未确认前，不发送长包、不以该候选支持panel off。后续构建结果须对应修正后的源身份。

显示 owner 模型已补齐主机回放：有排队帧或在途帧时拒绝 off；off/on 事务使用 generation，旧回执拒绝；硬件完成不确定时进入失败隔离；恢复只有在确认可见首帧后才重新满足显示 owner。最初模型未接入presenter；后续接线见下节。DPU/PHY/clock executor未完成，不能记设备显示休眠通过。

脱敏证据见 [POWER-02 本批证据](../evidence/2026-11-07-power-unattended.txt)。下一步先验证DSI共享显示地址域、实际FIFO执行与软件owner接线，再实现有地址域依据的长包DMA和硬件排空/恢复；之后再准备人工可控的首次panel off/resume。整机 suspend、真实 wake 和功耗测量继续保留人工/独立返回前置。

## 接续：实际显示队列接线与传输复核

入口源码为 `087416a4eb083032b2a4fbb43617f76a11d41c36` 加前述runtime WIP；前一goal轮次实际产出了源码/主机及ADSP设备证据，属于progress。本轮实时核对Android/product/boot_completed及无占用设备进程后继续主机实现。

- 新增有序补丁0021，把同一owner模型放入真实MiniIOMobileFramebuffer对象。producer入队、presenter取帧、show与surface释放后的软件结束，都在既有fSwapLock下更新；power request可观察同一组计数。默认关闭，显式`zizhan_display_owner=1`只启用接线/计数；不改变power request的硬件返回、VSync或显示电源态。
- 软件结束与硬件完成分开：show返回/清理不能证明真实CP/DPU latch或可见内容。runtime只调用software_done，不凭它设置first_frame或发布hardware completion；模型仍为后续真实排空和恢复的基础。
- 主机回放从真实ordered patch chain生成MiniIOMobileFramebuffer，提取实际新增producer/presenter结束代码，10,000帧并发执行，计数与队列一致且first_frame保持未证实。不是用另写的一份模型替代真实接线。
- 新增生产候选ZizhanDsiCommand FIFO执行器，保留DT packet type，完整检查程序后才发送；真实DMA_DONE和DMA非busy同时满足才成功。busy/error/timeout/冻结计时器均有有界回放；仍忙时不恢复FIFO/寄存器或释放lease。`zizhan_panel_transport=1`只选择它执行已有120Hz timing程序，作为首次新发送器设备对照，不执行panel off。
- 厂商`dsi_ctrl_buffer_init`先通过显示aspace取得IOVA；本机这条DSI mapper尚缺。内存包编码遵从`mipi_dsi_create_packet`＋`dsi_ctrl_copy_and_pad_cmd`：硬件计算ECC，LAST/payload flags及0xff DWORD补齐，不能发明软件ECC或把FIFO偶数填充长度当作DMA命令长度。

本轮有一次未接线完成时的build因早期大补丁改动无法应用而失败，已撤回对0001/0013的修改并改用独立0021；该失败日志保留，不写通过。只有修正后双配置构建和后续独立设备记录可作为新候选证据。

### owner-v2 / FIFO-v2 有界设备设计

源为上述完整HEAD加dirty候选，443个输入文件已冻结并哈希；清单SHA256为`5e195c419ffb22b7da102a6af9729bd1d7d6117688242c904e52a53679baf0f8`。修正后的默认/精简内核均KERNEL_DONE。OS/UI/helper保持OS370，两个候选不安装OS。每轮先复核温度、备份、真实extent/manifest、live OS身份和USB-A；单次supervisor、设备锁、15秒心跳及18分钟上限留本机交接。

1. `power02-owner-control-v2`：假设真实producer/presenter接线能保持软件计数和正常画面；只开`zizhan_display_owner=1`，旧timing transport为控制。touch/audio/adsp关闭，保留正常GPU/Wi-Fi/背光；80秒起两张截图、间隔20秒；autoreset300/SB180，panic与电源键返回保留。验收为owner软件计数无FAIL、正常完整可见主屏、新鲜deadline日志及Android完成返回。只认软件排空，不认硬件off/首帧恢复。
2. 首轮收口PASS后，`power02-dsi-fifo-v2`保持同一内核/OS/UI/helper、owner及截图条件，只增加`zizhan_panel_transport=1`（另记录loader epoch变化）。假设保留DT type的新FIFO executor能完整执行既有120Hz timing程序。验收额外要求8包实际完成、DPU/TE推进、两张完整画面、无新panic/DSI错误/retained busy及正常返回。任何前置或首轮失败先审查，不自动跳入下一轮、不重跑不变失败。

两轮都不发panel off/on、不切PHY/clock、不调用suspend/RTC/wakealarm。若出错，停止后续轮次并按已观察transport完成有界恢复；回退为原OS370＋已验证正常内核，不推广默认开关。等待/打包状态不算设备PASS，结果随后单独登记。

### owner控制轮结果与新增allocator对照

`power02-owner-control-v2`已正常返回Android：fresh deadline日志1,434,704bytes、无panic、owner计数4条PASS、真实CP1734/1734。两张图各16,200原始采样点及全部PNG放大副本匹配，非黑点均3489；主屏静态组件像素一致，放大PNG仅27像素变化。查看通道第二张局部呈现与文件不符，沿用既有逐像素复核边界，不登记残图故障。

本轮WLAN启动出现`WiFi wired DMA arena allocation failed at all budgets`，此前ADSP轮64MiB分配曾成功。owner的软件计数PASS与WLAN FAIL分列；本轮不记正常联网或联合功能PASS。失败发生在WLAN早期arena申请、owner首帧计数之前，但不足以排除新候选相关性。

因此FIFO-v2轮暂缓，先新增`power02-owner-off-v2`：同冻结内核、同OS/UI/helper/截图/期限，唯一业务开关改为`zizhan_display_owner=0`（另记epoch）；用于allocator负控及正常画面/返回覆盖。若仍失败，停止同输入重复并调查分配资源；若成功，仅排除“当前冻结内核必然无法分配”的解释，不能据单次对照认定owner造成/修复Wi-Fi。完成审查后才决定FIFO候选。

`power02-owner-off-v2`控制已收口：fresh deadline日志1,889,364bytes、Android完成返回、CP2192/2192、两张完整PNG；64MiB arena申请成功，WLAN固件/station/scan里程碑存在。owner日志按预期关闭。这不是端到端HTTPS/长稳资格，也不足以单轮归因上轮分配失败。

FIFO-v2对照调整为直接使用这轮已通过的owner=0控制（不再以owner=1混合网络失败轮作唯一控制）：同一冻结内核、OS/UI/helper、截图与期限，只增加`zizhan_panel_transport=1`，epoch差异单列。既有120Hz八包发送、真实帧推进、完整内容及返回作为验收，Wi-Fi arena结果另列。首次owner-off supervisor因preflight过期在设备动作前拒绝，随后fresh检查重启监督才执行本轮；原前置失败保留。

### FIFO-v2结果与后续主机收口

`power02-dsi-fifo-v2`通过限定场景：新FIFO executor完成既有120Hz程序8包，`result=0 retain=0`、耗时461µs，DPU INTF帧推进约120.1/s（TE字段仍0，不据它宣称TE中断有效）；CP1836/1836。fresh deadline日志1,666,175bytes、无panic、两张完整PNG、Android完成返回通过。两张原始采样各3487非黑点，蓝色widget、dock及白色widget等静态位置与控制相同；工具局部呈现不覆盖文件证据。WLAN64MiB arena、firmware/station/scan里程碑成功，未将其扩成HTTPS或长稳PASS。

这次设备输入仍为443-file冻结manifest和stagekernel `40d35b3cb44d73c929b9c5b1e013f6d469bbd91441fd60ec05202d4c10645e57`；不包含之后的host修正。后续审查将超时retain置为持续quarantine：即使busy在调用返回后清除，也不能未经显式恢复直接释放DSI lease；新增对应negative replay，属于主机修正，未在设备触发该错误分支。默认transport仍旧路径，所有新开关默认关闭。

离线清单v2补充本机共享非安全显示aspace、SID/mask、IOVA池和handoff声明；新测试拒绝缺失/重复provider及非法参数，未把DT变成当前group/context/MMU状态。当前host为14组79项PASS（本机CF回放包含在内）；最终双配置结果单列本机日志，不从v2设备结果推断v3错误分支硬件通过。

本批尚未完成原生锁屏/自动息屏安装验证、panel off/on与DPU/PHY/clock恢复、长包DMA映射、GPU/UFS/WLAN/触控/实体键的完整runtime PM、CPU/SoC深驻留、RTC/PDC/物理/网络唤醒及功耗验收。POWER-02保持IN_PROGRESS。可继续独立实现的下一项为固定显示group/context最小只读审计及地址域准入，之后才是完整显示电源事务；首次面板休眠与真正睡眠的人工/独立返回前置继续保留。

最终主机收口：14组79项日用回归、既有显示host回归、补丁应用回放、相对链接、公开私有路径检查、14-area结构及包含未跟踪文件的29-path归属通过。v3源清单SHA256 `9aeeb60bc23cc10c938907996d3635f327bece259a7dbf65f5ba9ceba28b7c7e`（443files），未预链接的完整默认内核SHA256 `b0bf4b04cc018c75c0869d83bdf5ee47a73070d6c893324fc906fb41431e05b0`；v3默认/精简均KERNEL_DONE，未设备启动，不与v2的stagekernel混用。未提交/推送或启动新云端CI。手机再次只读核对Android/device/product/boot_completed通过；本批监督label和设备锁均已清理。

## v4 固定显示地址域只读轮

入口HEAD仍`087416a4eb083032b2a4fbb43617f76a11d41c36`加WIP。新增Board40-byte ADT属性严格编码本机live DT基址/尺寸/SID/mask，以及历史已知group3/bank3窗口。观察器在DSI初始化后、原有时序切换前有界执行，只读IDR0/1、固定SMR3/S2CR3/CBA2R3及CB3 SCTLR/FSR；gate失败不读取其他context，不扫描后续group，不读页表、不写任何SMMU/DMA/电源状态。结果IdentityObserved仅当前快照，不授予长期身份IOVA lease。

候选假设是当前固定显示域仍符合既有路由/寄存器布局，且普通启动/完整画面/返回不退步；控制为已完成FIFO-v2及host-only v3基础，新增audit/ADT为当前变量，v3与v4源码差异和epoch单列，不能声称与v2仅一个二进制差异。source446-file清单SHA256 `32aab5ea132229c757ed778036ed4362e637ee689cd9b8d3824e1346e31bba00`，stagekernel `a10903da2d7da6eadb0b5763b3932a9dbe64cfc8b4742f989a50a924e62e8e34`，image `ac55940fa60743e8a2d039245067c0c0fa3deee8bc475906f24687f25c5ae122`。默认/精简构建、15组81项host通过。OS/UI/helper仍OS370，不安装；args1014bytes，audit1、FIFO1、owner0、shot80/n2/ms20000，autoreset300/SB180、panic与键复位保留，18分钟监督deadline。

验收为11次固定读取、路由/CB gate与前后快照一致、FSR0、普通FIFO时序8包、新鲜deadline日志、完整截图及Android完成返回；translation开启或资源不符按SKIP，不强改地址域。任何真实panic/读异常保留FAIL并停止后续相同探针。回退不改OS，下一轮关闭audit即可回原普通路径；不根据本轮自动启动panel sleep。

本轮启动编排有主机错误：最初身份脚本错误地要求新v4内核与旧v2哈希相等而失败，但同一shell的后续preflight仍继续；修正身份与监督文件后，已在执行的preflight完成并bootstrap了一次本轮监督。后续重复preflight被现有cycle检测拒绝，没有第二次boot。现已按PID/label/实际image/hash核对唯一活跃轮次，保留原主机错误，不重启已运行设备。后续编排使用明确成功链，避免把脚本失败误认为未启动。

### v5 fixed display-domain read result

v5 收口为 `PASS_BOUNDED_READONLY`：fresh日志1,594,546bytes、11次固定读取、0写入，`id0=0x4c017e71`、`id1=0x6000004b`、`SMR3=0x84022800`、`S2CR3=0x3`、`CBA2R3=1`、`CB3 SCTLR=0xe0`、`FSR=0x400`。按本机现有ArmSmmu4K fatal mask `0xc00001fe`，FSR0x400为非致命诊断位；快照前后稳定，状态`IdentityObserved`。FIFO 8包、CP1803/1803、64MiB WLAN arena/firmware/station/scan、两张截图和Android返回均通过。结果见`logs/power02-unattended-1107/domain-v5-result.json`和`domain-v5-shot-integrity.json`。

这确认显示group3/context3当前快照为identity/bypass形态，但不等于长期DMA lease：没有读取页表、没有建立IOVA、没有写SMMU、没有发送长包、没有panel off或suspend。下一步可设计有明确物理/地址一致性依据的显示命令映射；仍需保留失败回退和硬件完成确认。

## MVP推进：低地址命令缓冲与首次加载回归

本次按结果优先原则把 POWER-02 近期MVP收敛为原生显示请求→内屏实际关闭→wake→可见首帧；CPU深睡、WLAN wake及其他device runtime PM不作为这段切片的前置。完整POWER-02仍要求后续待机/硬件驻留/功耗资格。

### 已交付的主机实现

- DarwinBoot24识别可选`/chosen/pong-dsi-command` 16-byte lease，单独保留一个小于4GiB的专用页并回填物理地址/长度；不会挪用96MiB panel pool，也没有固定物理地址。`make-pong-adt-ipados18.py`发布零值占位，由loader填入真实地址。
- DSI memory command path只接受长包、有效的page lease、32-bit对齐和identity准入。每个长包发送前重新读取固定display group/context并要求`IdentityObserved`，不会写SMMU；写回命令buffer后flush D-cache，执行DMA后要求DONE且不busy，再恢复原寄存器。超时仍busy时保留DSI lease并隔离后续命令。
- 主机回放覆盖90-byte命令编码、地址/容量/identity错误、D-cache flush地址、DONE+idle、持续busy隔离及DMA寄存器恢复；panel packet parser的8项回放和domain observer回归也通过。
- DarwinBoot24以本机EDK2和LLVM18重新构建通过。包含该改动的最新精简`pong_ipados18_ci_defconfig`和默认`zizhan_ipados18_gpu_defconfig`内核构建分别通过；对应kernel只存在本机候选目录。此处不把构建结果当硬件DMA能力。

### memory-v2设备结果

为验证loader交接而不执行显示关闭，使用完整默认profile、OS370、后部USB-A和临时`fastboot boot`。命令行`zizhan_panel_transport=1`只选择已存在的120Hz timing程序；owner=0；没有panel off/on、SMMU写入或OS分区写入。候选kernel SHA-256为`031f41cb15164ee9c1cc618d602b1f2209309688142566f58fb2b8474760a5c9`，镜像SHA-256为`c438f36cdee6cf30af1a2bebda83c814ad3692dc15b0773dd3cad32c5cbca150`。输入image使用了本机已有`out/zizhan-uefi.img`作为boot template；loader单独重建，身份见仓库外运行记录。

设备确实收到临时镜像并在约59秒后回到fastboot；ramoops dump随后把手机正常带回Android，`boot_completed=1`。但是新`XNULOG.TXT`只有154字节，console描述为`start=8 size=0x2bde`，没有XNU启动文本、没有截图，也没有`ZIZHAN_DSI_DMA` buffer ready或`ZIZHAN_DSI_COMMAND`记录。因此此轮功能结论为**FAIL：发生在可见XNU启动/有效日志之前的早期失败，根因未知**。监督器的cycle退出码和Android返回只能证明恢复路径工作，不能把这轮记作内核或loader功能通过。原始日志及supervisor JSON保留在本机`logs/power02-dsi-memory-v2*`；不覆盖或同输入重跑。

对FASTBOOT返回时保留的旋转UEFI日志做了只读筛查：固件日志有约11.5秒的`Start EBS`，但五个日志槽里没有可归属本轮的`DARWINBOOT24`/`BOOTFAIL`行；日志槽可能含相邻启动的历史内容，不能据此确定loader已运行或未运行。暂时只能把故障范围收窄到EBS之后、有效XNU console之前，具体交接点未知。

下一动作是对这次精确镜像/loader/ADT及空ramoops做早期boot断点归因：核对包内EFI入口和默认项、实际加载/调用路径、ADT长度与CRC/树审计、loader的BOOTFAIL写入时机和ramoops初始化前后边界，再设计一个单次有界普通启动，必须取得新鲜loader或XNU阶段标记。只针对定位出的具体原因改代码或输入；不重跑同一失败镜像，不加泛化探针。普通显示启动恢复后，单独确认loader页回填和XNU mapping ready；然后接通真实Off/On请求、事务owner、panel序列、DPU/PHY/clock安全状态和首帧。面板第一次真实off/wake仍需要人在场和独立返回。POWER-02 MVP没有完成。

### 对照收口：新增 ADT 字段与构建缓存污染

后续普通启动对照把失败范围进一步收窄：

- 旧 g397 kernel + 旧 g397 ADT + 新 loader：正常进入 XNU、UFS、PSCI 查询和 Android 返回；说明新 loader 在旧 ADT 上可工作。
- 新 kernel + 旧 g397 ADT + 新 loader：正常进入 XNU并返回；新内核本身不是早期复位的充分原因。
- 旧 kernel + v5 ADT + 旧 loader：正常进入 XNU并返回；v5 的 `zizhan-dsi-domain-v1` 字段单独不是问题。
- 含新增 `pong-dsi-command` 字段的 ADT：旧 kernel/旧 loader 组合也早期失败；该字段已经撤回。后来尝试从 panel pool 尾部复用一页，但在正式设备前置回归前不把它视为已验证实现。
- 共享 DarwinBoot 构建树曾出现“源码已移除、EFI二进制仍含`pong-dsi-command`字符串”的旧对象污染。隔离构建后生成的 loader 不再含该字符串；后续候选必须使用隔离 loader、kernel、ADT 三件套，并记录每个 SHA。
- 当前新增DSI代码的page mapping默认改为关闭；只有显式`zizhan_dsi_dma_memory=1`才尝试映射。它仍未取得设备长包证据。普通启动恢复前不打开该开关。

当前最有价值的下一步是用同一隔离 loader 和已启动过的 kernel/ADT 组合复现普通启动，再单独加入一个改动。不要把一次 `cycle rc=0` 或 Android 返回当作功能通过；必须有新鲜 XNU 日志、对应阶段标记和画面证据。
