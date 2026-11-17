# 台账归档（截至 2026-11-17 16:00）

> 这是 11-17 精简前的 STATUS.md 原文，只作历史查阅；当前状态见 [STATUS.md](STATUS.md)。


更新日期：2026-11-17（UTC+8）。这是持续维护的当前任务台账；历史实验仍保留在日期文档。管理规则见 [AGENTS.md](../../AGENTS.md#progress-management)，本机路径、安装状态及进程交接见仓库外 `$ZIZHAN_SRC/work/CURRENT_HANDOFF.md`。

## 2026-11-17 10:45 接手续跑：mode38 有限运行时入口实机 PASS（限定）

用户把窗口改为 open-ended（无固定结束、小时心跳）。T0 仍为 00:11:55；接手入口核对无 cycle/install/waiter/lock，fresh `app-preflight.py` 已复核实际安装为 `statusbar-work-v1`（OS `5c0912...`、UI `f2d639...`、helper `14653f8d...`），wholeOS/manifest/extent 与备份清单 PASS。

| ID | 状态 | 已交付/验收 | 下一动作 |
| --- | --- | --- | --- |
| AUDIO-SERVICE-01 | P2 / DEVICE_VERIFIED（限定：内核受限生产者；未接HAL/服务/未出声） | mode38 复用已验证单图/映射/PRM，新增 v2 控制页与 65536 帧 int16 ring；12 图 ACK status0、Ready 前0后1、水位门/欠载门各1、4 次 pack 校验、4 个真实 V2DONE/1920 帧、末批 stop 后 drain、Ready 清除、DMAMAP restore/native-idle、PRM/Android 完成 PASS；AMP/BST/TX/数据脚/输出关闭 | 实现独立 `ZizhanAudio` IOKit 服务/用户客户端 + `ZizhanAudio.driver` HAL bundle，默认关闭；先主机接口/负控制与双 kernel，再考虑 HAL 安装后的静音 D 轮；首声仍停叫用户 |
| UI-STATUS-03 | DEVICE_VERIFIED（限定；当前安装 statusbar-work-v1） | 本轮未重装/未改 UI；fresh preflight 复核实际 OS/UI/helper 未变 | 保留；全分辨率/细字/全场景另行验收 |
| SENS-BASELINE-03 | BLOCKED | 本轮未做传感器实验；六项带宽票仍无可信同 boot 生产者 | 无新依据前不写资源/不启 PAS12/remote3 |

mode38 证据与边界见[运行时入口实机记录](2026-11-17-audio-runtime-device.md)与`$ZIZHAN_SRC/work/unattended-20261117-20h/audio-runtime-v1-device-review.json`。设备轮使用的完整 kernel 为 `827f5b...`；设备轮后只改了无关 WIRE 汇总日志门控（数据面不变、未重跑），当前源码完整 kernel 为 `c183d1...`。本轮不称用户态 HAL、系统音频服务、App 播放或首声。

## 2026-11-17 20小时无人值守计划（已制定，未启动）

计划见[20小时无人值守计划](../plan/2026-11-17-20h-unattended.md)。本次只规划：T0待用户确认，未创建心跳、未派设备轮、未安装/构建。窗口按“主机音频单图 → 至多五轮音频静音资格 → 默认关闭运行时 → UI电池收口 → 传感器限时解阻”安排；T+16h后不再新OS写，T+18h后不再启设备轮，T+20h收尾。首声、外屏/触控、自动切屏和GNSS现场仍禁止；六项带宽票无同boot可信生产者时保持BLOCKED。

## 2026-11-17 01:10 20小时窗口：音频单图 A/B 实机通过（限定）

| ID | 状态 | 已交付/验收 | 下一动作 |
| --- | --- | --- | --- |
| AUDIO-STREAM-01 | DEVICE_VERIFIED（限定：图生命周期+零数据+非零48k；未接HAL/未出声） | 默认关闭mode35/36/37实际12图命令status0、真实MAP句柄、MFC/SAL/TDM与WR_SHMEM→MFC同owner；零与固定48k非零数据各4个2880B WRITE/4个V2DONE/1920帧；UNMAP/native-idle/PRM清理及Android完成全部PASS | D HAL环运行时接线或P7 UI独立收口；首声仍停叫用户 |
| UI-STATUS-03 | DEVICE_VERIFIED（限定Home+电池设置页；当前安装statusbar-work-v1） | 无诊断917B UI-only包；wholeOS/manifest/extent读回PASS；135×120全宽采样完整性PASS；Home电池132/135列、左时钟保留；Settings电池健康页实际打开、电池同样在132列；Android完成PASS | 保留工作UI；全分辨率/细字/全场景另行验收；weather回退与rollback-resume保留 |
| AUDIO-SERVICE-01 | P1 / TODO（D设计完成，未实现/未上机） | zizhan独立服务/HAL bundle默认关闭、真实HAL环生产水位、Ready只在图+数据成立后发布；输出/AMP/BST/TX关闭 | 按[服务设计](../plan/2026-11-17-zizhan-audio-service.md)先实现主机接口/负控制，再双kernel和单轮静音设备测试；首声停叫 |

当前实际安装已切换为`statusbar-work-v1`（UI`f2d639...`/OS`5c0912...`），不是原weather UI。详见[UI收口记录](2026-11-17-ui-status-03.md)。

SENS-BASELINE-03在20h窗口内只做25分钟定向复核：现有交接仍只有vendor software aggregate/批写，`get_bw_stub=0`；`waipio.c`六项均为voter0，idx1 ACV/MC0/SH0属于display，不构成SLPI空闲独立voter。未发现新的同boot生产者/DRV/active-wake-sleep所有权输入，保持BLOCKED；本轮未写SLPI资源、未启动PAS12/remote3、未重复cmd-db/SMEM采集。

A/B原始证据、负结果与hash见[音频单图实机记录](2026-11-17-audio-graph-device.md)。A1/A2/A3的media参数/布局/回复匹配失败均保留；A4/B的PASS不倒写旧轮。AMP/BST/TX、数据脚、输出和系统音频服务仍未接通，不能称App播放或首声。



## 2026-11-16 21:13–23:13 静音无人值守（已收尾）

执行规格见[两小时计划](../plan/2026-11-16-next-unattended.md)。窗口已收尾；22:28后未启设备轮，本轮零设备实验，心跳已删除。本节优先于历史交接中的下一步；传感器初始化须先满足资源与远端所有权前置。

| ID | 状态/顺序 | 最小交付与验收 | 下一动作 |
| --- | --- | --- | --- |
| UI-STATUS-03 | P1 / HOST_VERIFIED + PREPARED / 先收口20分钟 | 无诊断UI-only候选，一轮完整画面与默认右上电池验证，明确安装或回退完整读回状态。 | 准备独立trial，保留weather及OS370回退；不混新驱动变量。 |
| SENS-BASELINE-03 | P1 / BLOCKED / 限25分钟 | 六项带宽票可信生产者/同次启动/voter语义的具体解阻或拒绝决定；不重复采表。 | 定向核交接或有原机依据的独立voter；无依据保持BLOCKED，转音频。 |
| AUDIO-STREAM-01 | P1 / HOST_VERIFIED（批次+48k环取数/24位组包；未接服务/图） / 主线约60分钟 | 实际HAL环缓冲取数、确认格式、有限WRITE/DONE、帧数与停止核心；host/新双kernel验收，硬件结果另列。 | 实现单图`WR_SHMEM 48k/2ch/24bps → MFC 0x07001015 → TDM 0x0700100e`的连接/参数计划，默认关闭；再做fresh双kernel/单轮静音资格；首声仍停等现场。 |

T0+75分钟后不启动设备轮，最多两轮有不同目的的静音实验，留原30分钟监督和末15分钟收尾。首次声音/外屏/自动切屏/GNSS现场边界不变；不commit/push。

本轮已备917B无诊断UI候选，未安装；vendor idx1是display且ACV mask不同，独立voter捷径拒绝，传感器仍BLOCKED。当前主线转音频，见[夜间记录](2026-11-16-night-ui-audio.md)。

音频已交付生产有限四槽WRITE/DONE调度核心，sanitizer/完整日用/新双kernel及74源哈希通过；该窗口内的HAL取数/格式转换/PRM运行时尚未接入。窗口关闭后接手补齐48k/2ch HAL环取数、24-bit打包、480帧/2880B批次与欠载门控，75源哈希及新双kernel通过；仍不称App播放。详见下方接手节与[夜间记录](2026-11-16-night-ui-audio.md)。

## 2026-11-16 23:25 接手补齐：AUDIO-STREAM-01 环取数与格式接线

静音窗口关闭后接手，只做主机实现与证据整理；未启动设备、未安装、未出声、未commit/push。源码仍为 dirty main `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交工作树；最后已知安装为原weather UI，本轮未重探设备状态。

**格式接线结论（SOURCE/HOST，不是设备资格）**：实际Android AudioFlinger speaker deep-buffer为48kHz/2ch，HAL与processing format均为`AUDIO_FORMAT_PCM_24_BIT_PACKED`（1920帧/40ms）；同一播放期间PCM32为`S24_LE/96000/3ch/PREPARED`。本机Forte ACDB（sha256 `089eb5e42b180bd86bb4f12ebd250efa89fcd80d9073931d694139fe38351359`）的唯一24-bit TDM候选为子图`0xb0000002`/IID`0x428f`，96k/24/3ch、slot_mask7、4×32位、lane2、12.288MHz；同一子图的SGIT模块表包含MFC`0x07001015`实例`0x48ba`。因此48k双声道到96k三通道的转换属于原机ADSP图内MFC，CPU侧不猜重采样滤波器，也不把三通道当三扬声器。该结论只证明原机子图共成员关系，尚不证明iPadOS图连接/参数已被DSP接受。

本轮新增`ZizhanAudioRing.h`：按CoreAudio sample time从65536帧、48k/2ch、int16交错的既有HAL环取480帧，做精确的int16→24-bit packed存储转换，输出2880字节/10ms；覆盖环绕、正负满幅符号扩展、非法ring/frame与单独欠载门控，缺数据不填零冒充媒体。`ZizhanAudioWrite.h`新增每请求`bytes`，默认仍为11520字节；`ZizhanAudioBatch`新增精确周期`init_ex`，2880B/480帧与11520B/960帧共用同一WRITE/V2DONE、槽位、token、DONE-only帧数与停止/超时保留逻辑，并增加槽位extent与4GiB边界。既有11520B调用与测试保持通过。

**主机验收**：`cs35l41-ring.py`的ASan/UBSan覆盖环取数/24-bit字节/欠载/四槽DONE/480帧累计/错误与超时；`cs35l41-batch.py`原核心回归通过；`cs35l41-tdm-inputs.py`扩展为14项控制并验证TDM子图含MFC，实际Forte ACDB解析报告（仓外`tdm-verified-inputs-v3.json`）给出MFC实例`0x48ba`。完整日用host通过，新精简/完整内核均PASS，75项runtime源哈希一致。精简kernel SHA256 `2405594b0552712f1ed1786e96c3eceb47ae40aac52f047d1e46fb905bf17885`；完整 `f08cb6fd332b58cd4348df4ee8c6d5bdcb1edf345bfa05a06dde21ceb6fdc7df`。

**仍未完成**：没有建立单图`WR_SHMEM → MFC → TDM sink`的模块连接、MFC输入/输出media-format参数与端口；没有HAL服务发布/Ready端点、真实生产水位或PRM运行时调用。当前probe仍使用分离的PCM/TDM图与11520B/96k/3ch写入，不能据此宣称App播放、持续播放或首声。下一步只在主机实现上述单图计划并用公开SPF/AGM结构做负控制，随后才考虑fresh双kernel与单轮默认关闭静音资格；首次扬声器输出继续停叫用户。

## Afternoon final verified checkpoint

SENS-TRANSPORT-01: DEVICE_VERIFIED for stable metadata only. V2 captured two consistent copies with 196 reads: remote3 partition 0x17f000/196608 bytes, GLINK descriptor478 (32 bytes) and TX479 (16384 bytes) present; SMP2P481/430 and RX480 absent. Complete short state and terminal records pass strict review. Fresh1372786-byte deadline log, ADSP13, real battery updates and Android completion pass; no panic or audio startup. V1 truncated receipt remains FAIL. No SLPI startup, baseline, sensor-data or API qualification. No OS installation. Next: host preparation for remote3 initialization with qualified remote-quiescence/PAS lifecycle and isolated QRTR ownership; no repeated inventory. Six unknown bandwidth baselines still block hardware startup.

## 2026-11-16 下午接手与静音无人值守

- 用户重新授权接手并开始无人值守。本轮原定监督至 **18:37 UTC+8**，未在截止后启动新设备实验；两轮静音试验已完成，18:40记录安全收尾和心跳删除；首声、首次外屏/触控/自动切屏、需用户参与的GNSS继续等待现场。保留dirty main，不commit/push。
- **UI-STATUS-02 / P1 / DEVICE_VERIFIED（默认选择与完整采样画面；未推广安装）**：接手发现昨夜台账漏记收尾。现存v3原始工件证明02:04临时启动、196秒返回、1,411,486B新鲜deadline及Android完成；没有`zizhan_statusbar=1`，实际3次nativePad/scale2，STUI1080×20。完整135×120采样帧/PNG逐像素一致性通过，实际查看绿色电池在整屏右上、左时钟保留；低分辨率不证明细字或全部场景。02:10原UI回退/wholeOS读回PASS已存在，下午重新核验原weather OS/manifest/extent/备份/Android通过，**无需重装或重跑**。源码默认策略已实机验证，当前安装仍为原weather UI。详见[补充记录](2026-11-16-statusbar-sensor-receipts.md#默认路径第三轮接手补齐已完成结果)。
- **SENS-BASELINE-02 / P1 / BLOCKED（继承票输入）**：固定厂商BCM/ICC/clock/route输入哈希复核通过；已读聚合/批写函数从Linux软件请求计算票，未交付本机固件继承票；get_bw_stub零不可用。解阻需同次启动/明确voter/DRV及active/wake/sleep的可信handoff，或经原机依据验证的非降票独立voter方案；当前两者均无，不从跨启动Android票/闲置TCS推值，禁止SLPI资源写/PAS12。[限定源码结论及独立下一步](2026-11-16-slpi-transport-resume.md)。
- **SENS-TRANSPORT-01 / P1 / DEVICE_VERIFIED (metadata only)**: V2 stable2copies/196reads, remote3 partition0x17f000/196608B, descriptor478/TX479 present, SMP2P481/430/RX480 absent; complete short receipts/fresh1372786Bdeadline/ADSP13/battery/Android pass. V1 truncated-receipt FAIL retained. No SLPI startup or sensor-data qualification. Next host-only remote3 initialization preparation; actual startup remains blocked by six unknown bandwidth baselines. Original weather OS retained; no more phone rounds this window.

## 2026-11-16 夜间无人值守（约01:05–03:05 UTC+8）

- **UI-STATUS-01 / P1 / DEVICE_VERIFIED（限定顶部图像，不推广整帧）**：用户报告电池未靠整屏右上。真实UIScreen native2160×1916/logical1080×958/scale2，旧Pong provider适配1080×2412/scale3。显式开关/真实L18限定原生UIKit/STUI Pad候选经SDK编译、10项panel控制与独立静音轮：实际STUI由600×50→1080×20。完整图像仍FAIL（部分下方行缺块），但从原始日志无填补还原的顶部140行全部270列完整，实际查看确认绿色电池位于整屏右上约97%处；此局部位置资格通过。首轮预算SKIP/第二轮整帧缺列FAIL均保留；先前“右边缘完全无法验证”解释被完整top原始像素纠正，不能倒改整帧PASS。两次已完整恢复原weather UI/readback。
- **UI-STATUS-02 / 夜间记录停点（已由上方下午接手结果补齐）**：同原生Pad布局改为真实zizhan内屏默认启用、`zizhan_statusbar=0`明确回退，外来屏仍原选择；diag仍默认关闭。当时记录仅HOST_VERIFIED + PREPARED；随后v3实机与回退实际已完成，原始结果见上方，不再把旧停点当待执行轮次。
- **SENS-BASELINE-01 / P1 / DEVICE_VERIFIED（仅完成请求观测）**：两个公共owner同锁completed journal/ACK0与unknown分离/64地址上限与失败拒绝，经生产回放/日用/新双kernel通过。默认关闭cmdDbmode2真实稳定表2copies/65536reads/parser0/9资源，实机查询known3（XO/LCX/LMX）/unknown6（MC0/SH0/SN3/SN0/CE0/ACV），13类平台控制及新双kernel/日用通过；无raw重复导出、投票/PAS/SLPI启动。3条软件完成不等于继承baseline，6未知不能补0；下一按这6项做可信owner交接或独立已知协议增量，不重复cmd-db。见[本批记录](2026-11-16-statusbar-sensor-receipts.md)。
- 用户今晚无法听测；音频首声候选封存，禁止PRM34/attended1、首次外屏/触控/自动切屏和需解锁权限GNSS。Paseo两小时心跳监督；单设备轮仍30分钟/自动返回/新鲜日志/Android见证。到期停止新轮，保留未提交工作树，不commit/push。

## 2026-11-15 iPadOS 硬件适配启动方案

- **当前音频主实验 / AUDIO-DMA-01 / DEVICE_VERIFIED（限定只读）**：mode25真实UEFI64KiB保留区/全局锁下SMMU两遍908读完整稳定，实际75CB/113SMR/pages128，SID1801计划可用slot112/bank73（非迁移/复用）。零映射写/数据。PCM联合收尾/两颗HALO100/99心跳/reset/资源释放及1,425,228B新鲜deadline/Android35.8°C通过。下一只在真实保留区建立可撤销SMMU+ADSP内存句柄资格，无PCM数据/声音；须原始状态快照、严格读回、TLB/撤销/失败保留。见[本批结果](2026-11-15-audio-silent-owner.md#mode25实机真实保留区与smmu只读前置通过)。
- **AUDIO-DMA-02 / DEVICE_VERIFIED（native idle释放；原始CB恢复REJECTED）**：mode26 leasev2实际MAP返回非零句柄、UNMAP回复0，nativeidle禁用/TLB/unused stream恢复/完整读回通过；双HALO100/99、reset/双时钟脚/联合图/PRM5ACK held0全部通过。1,385,319B新鲜deadline/正常返回/Android完成36.6°C。没有PCM数据/声音，prior_context_restored0明确保留；下一mode28真实10ms全零缓冲区消费资格。
- **AUDIO-DMA-03 / REJECTED（本地对照）**：strict映射/16叶audit/ASIDguard通过，零DSP命令/完整新鲜ownership观察后恢复仍panic；39,904B新鲜panic/36秒返回/Android完成31.7°C，无deadline/截图。未作功放执行/PCM数据，负结果保留。
- **AUDIO-DMA-04 / DEVICE_VERIFIED（限定本地安全释放）**：mode27实际16页映射/fullaudit/TLB/namespace通过，原unused stream恢复、上下文disabled1/nativeidle完整回读/TLB1/uncertain0；prior_context_restored0明确保留。双HALO/联合图/PRM5ACK held0通过，199秒正常返回/1,442,241B新鲜deadline/Android完成35.0°C，无panic。无DSP命令/PCM数据/声音；下一同kernel mode26真实句柄全寿命资格：相同source60/943B新包及fresh后部USB/Android完成/无owner通过；尚未DSP lease完整资格。
- **AUDIO-DATA-01 / DEVICE_VERIFIED（10ms全零消费，非声音）**：mode28真实UEFI64KiB/16页unmanaged只读NC，两遍16,384读全零；真实MAP后68B WRITE送11,520B全零（96k/3ch/32位/10ms），收到56B实际V2 DONE并严格绑定地址/句柄/token/status/metadata，sent1/ack1/uncertain0。UNMAP/nativeidle/双HALO/联合图/PRM释放通过，200秒返回/1,432,324B新鲜deadline/Android完成37.2°C，无panic/ADSP失败。数据引脚/AMP/BST/TX仍关闭；下一接GPIO124数据路径与运行中的保护程序，默认无声。
- **AUDIO-DATA-02 / DEVICE_VERIFIED（接数据脚，运行窗口内全零传输）**：mode29v2真实nativeRAM full0/两次ACK及逐芯片6读通过；GPIO124=原机c4/io0，分别在每颗核心sample49/HALO2/HB正常时发送11520B零数据并收到两个不同token的实际V2 DONE。每颗传输前后HALO2、HB6c3→6ea/6c0→6e2；100样本/149配置ACK/6.52秒，reset后data/SCK/WS恢复、UNMAP/nativeidle/联合图/PRM释放通过。197秒返回/1,400,886B新鲜deadline/Android完成34.7°C，无panic。AMP/BST/TX仍关闭；不称非零功放接收/声学或系统App播放。下一原机输出路径/增益/升压的默认关闭配置候选，首声须停叫用户。
- **AUDIO-OUTPUT-01 / DEVICE_VERIFIED（输出保持禁用）**：mode30两颗14 masked配置/颗，28次实际ACK/逐项完整读回/保留位核对通过；原机slots/DSP来源/最低数字音量/增益code5/被动boost参数等，AMP/BST输出仍0。配置后GPIO124/liveHALO两次全零数据/前后心跳/reset引脚恢复/UNMAP/nativeidle/联合图/PRM释放通过。198秒返回/1,396,874B新鲜deadline/Android完成33.7°C，无panic或ADSP失败。首声未就绪；下一原生保护暂停/恢复mailbox真实回执与IV监测前置，不能仅凭HALO心跳启输出。
- **AUDIO-PROTECT-02 / DEVICE_VERIFIED（输出关闭）**：PRM31真实两颗PAUSE1→status1/RESUME2→status0，四次实际IRQ301f02/首次poll完成、24写ACK、原maskffffffff完整恢复；随后两次livezero/HALO2/心跳/reset引脚/UNMAP/nativeidle/联合图/PRM释放全部通过。6.622389秒执行，1,406,534B新鲜deadline/199秒返回/Android完成，无panic或ADSP失败。主机负控制/日用/新双kernel通过；无首声、负载保护或系统App播放资格。
- **AUDIO-IV-01 / DEVICE_VERIFIED（只读路由/enable前置）**：PRM32实际两颗各传输前后3读，四窗口PWR2=3300/RX3=18/RX4=19；监测ADC enable与原机DSP路由通过。四mailbox实际回执/两次livezero/HALO2心跳/reset/pins/UNMAP/nativeidle/联合图/PRM释放通过；1,425,674B新鲜deadline/198秒返回/Android完成，无panic/ADSP失败。实际GENI负控制/日用/新双kernel通过。仅路由/enable，不称模拟精度或持续负载保护。
- **AUDIO-TONE-01 / DEVICE_VERIFIED（非零DSP消费，输出关闭）**：PRM33真实独占unmanaged64KiB/NormalNC，在MAP前写入11520B固定1kHz/10ms/低幅度/1ms渐变样本，完整32768读/2880写回验通过；两颗livecore窗口各一次真实非零WRITE/实际V2DONE，通过后ampreset/完整波形preimage核对/清零完整回验，再UNMAP/nativeidle/联合图/PRM释放。1,396,537B新鲜deadline/Android完成、无panic/ADSP失败。主机生成器/实际adapter/实际PRM负控制、日用及新双kernel通过。仍未开启AMP/BST，未验证波形实际抵达扬声器或声学。
- **AUDIO-PULSE-01 / HOST_VERIFIED + PREPARED（首次输出停等用户）**：默认关闭PRM34且额外`zizhan_audio_attended=1`，SE15再次核现场开关；已接原生pup/pdn/保护与故障守卫/固定低音量有限输出/完整收尾。每颗先真实零预卷V2DONE→核core/AMP/BST off→启输出→10ms固定短音V2DONE→先关AMP再关BST/恢复最低音量，单WRITE等待100ms、脉冲预算250ms、每颗16配置写。实际GENI含全部32写失败/预卷失败/播放失败与超时、故障位控制、实际SE15/PRM入口和顺序、原机输入、日用及新双kernel通过。source69匹配967B首声包，fresh后部USB-A/Android完成/无owner通过。**未上机开启输出，无声学/负载保护/App播放结论；当前停在用户要求的首声现场边界，等待叫到用户后再派唯一轮。** 无commit/push/OS安装；夜间新心跳范围见上。
- **目标ACTIVE**：转入 GNSS、运动/环境传感器、CS35L41 音频与折叠/外屏的 iPadOS 适配准备。Android 结果作为硬件线索，不能提升为 iPadOS 资格。
- **顺序**：先只读资源/接口接续，再做默认关闭的 GPIO62/Hall 与 hinge observer 和 host replay；随后分别推进其他传感器、真实 GNSS provider、CS35L41 播放链；外屏先独立静态点亮/帧推进，再接 SE19 触控、手动路由，最后实现 Hall 自动切屏。
- **无人值守边界**：主机审计、host tests、reduced/full build、候选打包与回退 preflight 可以自动推进。首次扬声器实际输出、外屏点亮/触控、权限 UI 和自动切屏须停在默认关闭的候选，等待人在场确认。
- **HALL-01 / P1 / HOST_VERIFIED**：已实现默认关闭的 GPIO62 只读观察器；维护中的 FTS 路径和保存的本机 live DT 确认 TLMM 输入布局。`zizhan_hall_observe=1` 才运行，单页只读映射、检查 GPIO 输入模式、15 ms 去抖、120秒/24000次上限，不接显示/HID。实际 probe 13项回放、ADT负控制、去抖 sanitizer、iOS SDK平台编译、精简/完整内核构建通过；尚未打包或上机，不算折叠功能通过。证据见[本批交付](2026-11-15-hall-observer.md)。
- **SENS-01 第二批 / HOST_VERIFIED（仅前置）**：已从受保护原机备份提取SLPI并验证DT/固件放置，25段/23加载段、25MiB span、5324B metadata与本机carveout匹配。新增离线preflight调用实际QcomMdt核心，8项host负控制通过；备份manifest/提取哈希及固件留仓外。SLPI没有认证/启动，GLINK/QRTR和传感器API未通。见[前置交付](2026-11-15-slpi-preflight.md)。
- **SENS-01 第三批 / IPCC实机限定兼容性通过**：共用receiver默认关闭；host实际owner11项/ADSP5项/首事件并发、日用和双kernel均通过。route0/route1同kernel/loader/OS实机，ADSP均stage13、电池发布持续更新、audio0、无panic/fatal，196/197秒返回、新鲜klog和Android完成通过。route1确认新receiver挂接，35.7°C正常返回，无冷却门槛。设备日志缺逐客户端中断计数且GLINK仍polling，只认ADSP兼容性，不认IRQ delivery或SLPI。见[实机记录](2026-11-15-ipcc-routing.md)。
- **第四批 / IPCC实际送达DEVICE_VERIFIED（限定）**：最多8次/>=10s线程快照和per-slot原子计数已双kernel/日用host通过并上机。实际8快照累计441 IRQ、477 received/delivered/ADSP callbacks，per-id AOP3/ADSP GLINK469/SMP2P5，unknown/budget/retries均0；ADSP13/真实电池正常、无panic/fatal/audio。199秒返回、1490141B新鲜deadline和Android完成PASS，34.7°C。仅覆盖观测到的ADSP/AOP链，不推断SLPI、lossless或睡眠。
- **剩余代码缺口**：IMU/磁力计/ALS 与 GNSS 尚无本机完整原生输入链；Hall 二值不提供铰链角度。Pong 音频仍为 TFA9873/SE13，CS35L41不能仅改地址启用。
- **第五批 / 共用资源HOST_VERIFIED，ADSP限定回归DEVICE_VERIFIED**：实际QMP/SMEM helper及固件vnode adapter已完成sanitizer/日用host和新双kernel。QMP同actor串行、1秒确认/超时禁复用，SMEM保留硬件mutex并拒绝过小旧item；精确允许SLPI固件文件但未安装或启动。变更后新kernel实机ADSP13、31条真实电池更新/18组不同值，无ADSP失败/fatal/panic/audio；196秒返回、1542328B新鲜deadline、Android完成PASS，33.9°C。仅原ADSP路径回归，不把SLPI host控制提升为实机功能。见[本批记录](2026-11-15-slpi-shared-resources.md)。
- **第六批 / SLPI资源计划与lease核心HOST_VERIFIED**：已实现实际cmd-db格式的纯C解析/ARC corner与BCM unit缩放、VCD完整分组、可信baseline合并和部分失败禁重试；最终sanitizer/日用host/新双kernel通过。人工cmd-db只作控制；核心未接开机或硬件owner，无手机轮次/新包/资源请求。具体见[本批记录](2026-11-15-slpi-power-lease.md)。
- **第七批 / RPMh整组API HOST_VERIFIED**：公共owner新增有限TCS4 payload接口，1–16条/实际capacity、单trigger、逐命令完成与新鲜busy/IRQ判定，10/100/10ms及迭代上限；失败保留并拒绝后续batch/legacy直到reset。两个owner生产回放/sanitizer、日用/Wi-Fi host及新双kernel通过。无boot调用者或手机轮次，实际硬件batch ACK未验。见[本批记录](2026-11-15-rpmh-batch.md)。
- **第八批 / CS35L41身份核心HOST_VERIFIED**：本机saved live DT确认SE15/GPIO56/57/40,42/reset1；发现两颗IRQ声明与pinctrl交叉，保留差异且不使用IRQ。新增32位大端DEVID/REVID只读核心，显式启用且真实平台前置齐全才运行，20/2/2ms事务/取消/中止与错误保留。实际GENI sanitizer/日用host/新双kernel通过，无SE15初始化/boot hook/包/设备I2C或声音。见[本批记录](2026-11-15-cs35l41-identity.md)。
- **第九批 / SE15平台前置观察DEVICE_VERIFIED（限定）**：固定BSP cb7a356ae138e77992ae199eebb86559d825a673明确cape-gcc匹配；新增default-off只读入口的14项实际host/日用/双kernel通过。909B候选实机看到votes0、S0/S-AHB halt、pins56/57=GPIO/pulldown、reset GPIO输入低，按预期SKIP/se_mapped0/i2c_commands0。196秒返回、1499452B新鲜deadline/Android完成PASS、33.1°C，ADSP13和30条电池更新。只认前置观察，不认供电/功放ID/声音。见[本批记录](2026-11-15-se15-platform.md)。
- **第十批 / SE15 clock/pin DEVICE_VERIFIED（限定）**：default-off有界设置的生产core/14项实际平台sanitizer、日用和新双kernel通过。909B候选实机votes0→1f，gate通过、SE读到fw303/status0/irq0/root0/cfg100000，两pin回读7/7，共3次写且reset未变/无I2C。196秒返回、1383479B新鲜deadline/Android完成PASS、33.5°C，ADSP13和33条电池更新。只认clock/pin设置，不认FIFO/timing、供电、功放ID或声音。见[本批记录](2026-11-15-se15-clock-pin.md)。
- **第十一批 / GENI timing/FIFO DEVICE_VERIFIED（限定）**：生产GENI/timing sanitizer、17项实际平台回放、日用及新双kernel通过。909B模式2实机DFS21/level0/selected1合法XO，FIFO参数201029e4/20104800宽32深16，clk71/scla02c1a及FIFO回读PASS，外部IRQ route0/reset未变/无I2C。196秒返回、1389021B新鲜deadline/Android完成PASS、34.0°C，ADSP13及30条电池更新。仅配置资格，非功放ID/供电或电气速率/声音。见[本批记录](2026-11-15-geni-timing.md)。
- **第十二批 / CS35L41数字启动/身份DEVICE_VERIFIED（限定）**：本机模块/DT核对共享GPIO1 reset、dummy供应绑定，default-off prepare3新host/18项平台/日用/双kernel通过。iPadOS实机两颗40/42均OTP3/ID35a40/REVb2，错误寄存器20000000的OTP_ERR bit31未置位，完成2/无bus错误，最终reset重新assert；无codec值写或音频输出。196秒返回、1484921B新鲜deadline/Android完成PASS、33.5°C，ADSP13/33条电池更新。仅数字身份，不含模拟电压或声音。见[本批记录](2026-11-15-amp-boot-identity.md)。
- **第十三批 / 原机功放输入HOST_VERIFIED（framing）**：已只读留存11个保护/校准/场景文件并逐个远端/本机hash匹配；新离线工具通过实际bundle、9项控制和完整日用host。WMFW各10段、T/B保护WMDR3/4块、T场景73/B22整数，角色分列；相同WMFW不代表可混用不同系数。算法metadata/base和场景语义未解，明确无写计划/加载/声音。见[本批记录](2026-11-15-amp-original-inputs.md)。
- **第十四批 / 保护内存计划HOST_VERIFIED（仅离线）**：原机模块HALO五窗口与固定源一致；解出保护镜像ID400a4/算法cd,f20b，T/B系数按byte offset+算法base定位。新工具实际输入计划67284B、9项控制及完整日用host通过，拒绝越界/版本/跨别名覆盖；无执行器，plan_executable=false。发现原机B2 errata表与通用Linux内容不同，不能整段照搬。见[本批记录](2026-11-15-amp-memory-plan.md)。
- **第十五批 / B2与OTP表HOST_VERIFIED（仅离线）**：新模块relocation解析器及位流解码器通过原机五种OTP表（每表100项/688位）、9项合成控制和完整日用host。保留位只消耗源，trim仅允许指定易失位域；原机OTP钥匙55/aa→cc/33与B2补丁5555/aaaa→cccc/3333分开。无逐芯片真实快照、钥匙/trim写、DSP加载或声音，plan_executable=false。见[本批记录](2026-11-15-amp-otp-plan.md)。
- **第十六批 / HOST_VERIFIED；实机完整快照FAIL、前置中止行为限定通过**：mode4实际GENI sanitizer/86位置失败/平台19项/日用host/新双kernel通过。197秒临时轮取得1378348B新鲜deadline，第一颗实际PWR1=0/PWR2=3321/HALO=101，AMP/boost/HALO使能位未清，按reason10在OTP前中止，第二颗状态未读；最终resetasserted、零功放值写/声音。ADSP13/31电池更新、无fatal及Android返回PASS；不能算OTP快照或初始化通过。见[本批记录](2026-11-15-amp-otp-snapshot.md)。
- **第十七批 / safe-off DEVICE_VERIFIED（限定），完整OTP快照FAIL**：host/平台20项/日用及新双kernel通过。两颗实机PWR1 0→0/PWR2 3321→3300/HALO101→100，4次清零确认/其他位保留/finalreset，通过；随后snapshot前置Busy(reason2)，无任何OTPID/word，具体phase/SE/IRQ/RX来源尚未记录。197秒返回、1371097B新鲜deadline、ADSP13/31电池更新/无fatal/Android返回PASS。无key/OTP/trim/output-enable写或音频。见[本批记录](2026-11-15-amp-safe-off.md)。
- **第十八批 / Busy来源DEVICE_VERIFIED（限定），OTP仍FAIL**：生产host/平台20项/日用/新双kernel通过。实际safe-off入口全0，snapshot phaseidle/SE0/RXFIFO0，仅IRQ4000080（bit26 RXwatermark+bit7 RXIRQ），精确busy_sources4；不是CMD_DONE/活动命令/残数据。两颗清零仍PASS，snapshot按guard拒绝未读OTP。197秒返回、1477596B新鲜deadline、ADSP13/电池/无fatal/Android返回通过。见[本批记录](2026-11-15-amp-bus-entry.md)。
- **第十九批 / HOST_VERIFIED，实机handoff FAIL/OTP SKIP**：生产host/平台21项/日用/新双kernel通过。实际一次W1C IRQ4000080→80，phaseidle/SE0/RX0保持，12samples到期限，RXIRQ bit7持续，按reason4中止；继承watermark0/RFR e已采，未修改，不能断言阈值是根因。两颗safe-off仍PASS/finalreset。196秒返回、1378520B新鲜deadline、ADSP13/30电池更新/无fatal/Android返回通过。见[本批记录](2026-11-15-amp-rx-handoff.md)。
- **第二十批 / RX阈值DEVICE_VERIFIED（限定），handoff FAIL/OTP SKIP**：host/平台22项/日用/新双kernel通过。实际RXwatermark0→f/RFR e→10两写精确读回PASS，master watermark位消失但80持续；同idle/SE0/RX0/一次清除后到期限，slave_irq08000000（RX-LAST）/DMA RX0，未清slave。两颗safe-off仍PASS/finalreset。197秒返回、1411403B新鲜deadline、ADSP13/30电池更新/无fatal/Android返回通过。见[本批记录](2026-11-15-amp-rx-setup.md)。
- **第二十一批 / HOST_VERIFIED，实机source-handoff FAIL/OTP SKIP**：host/平台23项/日用/新双kernel通过。一次slave确认后RX-LAST08000000→0/DMA RX0；再一次master确认后80仍持续，phaseidle/SE0/RX0，15samples至期限。排除只缺slave确认的假设，不忽略80；RX15/16和两颗safe-off仍通过/finalreset。199秒返回、1501763B新鲜deadline、ADSP13/31电池更新/无fatal/Android返回通过。见[本批记录](2026-11-15-amp-source-handoff.md)。
- **第二十二批 / 配置诊断DEVICE_VERIFIED（限定），OTP仍SKIP**：host/平台24项/日用/新双kernel通过。两组24项实际master80/enable40007e3f（bit7关闭、enabled-pending0），source slave08000000→0，DMA TX/RX0、phaseidle/SE0/TXRXempty/route0/DMA mode0/GSI0/FW303均确定；非简单未清来源。原handoff仍FAIL。196秒返回、1374279B新鲜deadline、ADSP13/电池/无fatal/Android返回通过。见[本批记录](2026-11-15-amp-irq-config.md)。
- **第二十三批 / HOST_VERIFIED source候选，无设备轮**：独立mode10已实现exact owned-readiness，24配置守卫/两次一致/来源一次确认/不清master，实际快照identity再验同样条件，raw80保留；固件/enable/来源/配置变化负控制和平台25项通过，日用通过，最终v2精简/完整kernel通过，source31hash一致；未打包或上机。旧strict模式保持。见[本批记录](2026-11-15-amp-owned-readiness.md)。
- **新两小时授权（08:58–10:58 UTC+8，现已收尾）**：重点音频、折叠传感器；本窗口已取得真实OTP、Hall静态基线、实际cmd-db、B2与易失trim实机证据，RAM传输计划只到host。每轮单owner与fresh全量前置保持；首次出声/外屏/自动切屏仍现场。
- **第二十四批 / 音频OTP DEVICE_VERIFIED（限定）+真实数据HOST_VERIFIED**：mode10实机readiness3样本/raw80保留/enable40007e3f/pending0通过，两颗各OTPID8/32words、PWR1=0/PWR2=3300/HALO100前后稳定、输出/boost/DSP禁止。真实逐颗host解码各98项易失更新，但未执行trim/钥匙或加载固件。197秒返回、1387566B新鲜deadline、ADSP13/电池/无fatal/Android通过，finalreset。见[实机追加](2026-11-15-amp-owned-readiness.md#新两小时窗口中的独立实机资格0906收口)。
- **第二十五批 / Hall只读基线DEVICE_VERIFIED（限定）**：独立Hall轮实机GPIO62 raw1/high/asserted0、15ms基线成立，120秒21693读取/0变化，PASS observer/readonly1/fold_qualified0。1637661B新鲜deadline/ADSP13/电池/无fatal/Android返回通过；没有机械姿态/开合见证，不推断角度或自动切屏。见[实机追加](2026-11-15-hall-observer.md#静态只读设备资格0914收口)。
- **第二十六批 / 真实cmd-db DEVICE_CAPTURED+资源计划HOST_VERIFIED**：实际80860000/20000双copy稳定；v1parserCorrupt/v2长日志截断均保留FAIL，v3短记录2048块+FNV64无损还原128KiB。实际ACV是mask类型width0，修复类型检查后真实9资源生产plan通过，普通BCM仍拒zero-width；零票/baseline未知/SLPI未启动。原host/平台9项/日志截断重建控制通过，最终修复v4新精简/完整kernel通过，source33hash一致。见[本批记录](2026-11-15-slpi-cmd-db.md)。
- **第二十七批 / B2补丁DEVICE_VERIFIED（限定）**：mode11真实固定patch20attempt/20ack，每颗6读回/2closeACK，状态PWR10/PWR23300/HALO100保持，finalreset，无buserror/uncertain/trim/OTPprogram/output。196秒返回、1460710B新鲜deadline/ADSP13/电池/无fatal/Android完成通过；closeACK不当lockproof。见[本批记录](2026-11-15-amp-b2-patch.md)。
- **第二十八批 / 易失trim DEVICE_VERIFIED（限定）**：mode12本轮两颗OTPID8展开98位域/颗，masked保留/完整readbacks98/98，143个变化字段+8keys=151attempt/151ack，closeACK2/2，postPWR10/PWR23300/HALO100/finalreset，无buserror/uncertain/OTPprogram/output/DSPstart。196秒返回、1379745B新鲜deadline、ADSP13/电池/无fatal/Android完成通过。固定B2/真实快照同轮通过。见[本批记录](2026-11-15-amp-volatile-trim.md)。
- **第二十九批 / RAM传输计划HOST_VERIFIED**：新增保护固件4字节分块与完整有序读回校验工具；实际原机输入两颗12段/59752字节、各14938次写和读的计划通过，8项错误控制及完整日用host通过。只校验输入字节，不认证设备来源；无runtime执行器/设备传输/系数/DSP/输出。本批未改driver，既有双kernel保持。见[本批记录](2026-11-15-amp-ram-transfer.md)。
- **第三十批 / AUDIO-RAM-01 / IN_PROGRESS**：用户在附近并授权继续音频，首次出声前停下叫用户。新增独立默认关闭mode13：同轮安全初始化后，原机WMFW准确hash/34068字节、六段固定RAM写和完整7469读回/颗，45秒整轮/原事务界限/finalreset。私有输入由显式临时包ADT传递，默认无payload且缺失/错大小在MMIO前跳过；不改已装OS/Data。生产核心与实际原机文件SHA/ADT、平台30项已定向通过，最终独立SHA257长度/实际输入/完整日用host及新双kernel通过，已备默认关闭mode13临时包，实机RAM读回FAIL：chip0六段7469写均ACK，但首段offset4第二个读回不符，立即中止/第二颗未执行；原B2/trimPASS、禁止状态保持/finalreset、199秒返回/1380383B新鲜日志/Android完成通过。下一仅header受限packed/unpacked诊断，不重跑完整同候选。见[本批记录](2026-11-15-amp-ram-load.md)。
- **mode14诊断DEVICE_VERIFIED（限定）**：仅header18写/18packed+24unpacked完整采集，packed11不符呈规律，但unpacked24全部24与原机逻辑字匹配，确认首72字节写正确。196秒返回/1485739B新鲜deadline/Android完成通过，35.4°C。下一独立mode15改为XM/YM12B、PM20B分组读取全量比较；GENI旧短读上限保持，新burst严格FIFO计数/5words上限，生产core/实际输入/独立SHA/平台32/完整日用和新双kernel均通过，mode15私有临时包已备，下一fresh独立全RAM上机。禁止DSP/输出。
- **mode15完整保护RAM DEVICE_VERIFIED（限定）**：两颗各6段/7469个32位完整读回，14938写ACK、3618分组读/零差异，21.7秒完成；PWR1=0/PWR2=3300/HALO100保持/finalreset。200秒返回/1394874B新鲜deadline/ADSP13/电池/无fatal/Android完成通过。前一标量读FAIL保留。下一同轮核两颗runtime算法ID/base后仅原机T/B保护系数RAM写/完整读回，仍禁止DSP/输出。
- **AUDIO-COEFF-01 / DEVICE_VERIFIED（限定）**：mode16同轮两颗实际22字算法身份/base均匹配，原机T/B保护参数1883写ACK、完整933/950读回、段1/2及状态12/18通过，2.95秒完成，PWR1=0/PWR2=3300/HALO100/finalreset，零buserror/uncertain/DSPstart/output。200秒返回/1367006B新鲜deadline/ADSP13/电池/无fatal/Android完成通过，34.0°C。见[实机记录](2026-11-15-amp-protection-coefficients.md)。
- **Android扬声器现场基线已补**：用户确认上下两个出声口“都有，都正常”；只认Android实际扬声器听测，未指定片段/音量/时长，麦克风和iPadOS声音未验。继续iPadOS默认关闭HALO无声运行/MPU/时钟与本机ASP/ADSP路由适配，首次iPadOS出声前再停叫用户。
- **AUDIO-RUN-01 / BLOCKED（外部时钟前置）**：独立默认关闭mode17，原机16速率/23MPU设置和实际metadata心跳地址已核对；同轮完整固件/参数通过后，只启核心/真实心跳采样/停止/finalreset，GLOBAL/AMP/BST/ASP输出保持关闭。生产完整链/无心跳/每事务错误/真实输入/平台35/完整日用通过，新双kernel通过，私有临时包已备，首轮实机在chip0第一项MPU读回FAIL，19写ACK/16速率读回，核心未启动；PWR1=0/PWR2=3300/HALO100/finalreset/Android返回通过。下一仅MPU原始位宽诊断，不忽略不符。见[本批记录](2026-11-15-amp-silent-run.md)。
- **MPU诊断 / DEVICE_VERIFIED（capture限定）**：两颗20项实际向量一致，78ACK/零核心启动/finalreset；200秒返回/1379159B新鲜deadline/Android完成通过，33.4°C。不将向量命名权限掩码。下一独立mode19要求此完整向量逐项精确/关闭后再次稳定，未知差异失败，mpu_rights_verified=0，之后才原无声心跳/停核心检查；生产控制/平台37/完整日用/新双kernel通过，mode19私有包已备并派独立临时验证轮，结果待日志。
- **mode19 / 执行FAIL、核心启停限定通过**：chip0精确MPU向量两次匹配，core100→101→100读回和41ACK通过；32实际样本心跳/HALO0，有限停止/finalreset，chip1未尝试。PLL10/source外部SCLK，尚未建立native播放时钟；下一只读核原机backend/slots/width/BCLK并做输出关闭的时钟资格，禁止相同轮重试或猜改PLL。
- **AUDIO-CLOCK-01 / 实际输入已取得；旧PRM阻塞已接续解决**：两格播放采样确认上层48k双声道、PCM32协商96k/3ch/S24_LE但PREPARED保留，双DSP HALO2/心跳推进；原机slotclamp4推导12.288MHz/ID204/attribute4。PRM mode1查询、mode2独立端口直请、mode3原机端口7/token0均实际remote1；全部未申请成功，无频率/pin/codec/graph/output。mode3已196秒返回/1500989B新鲜deadline/ADSP13/Android完成通过。旧三轮保留；随后已核对固件身份并由mode5/6取得真实控制链资格（见下），不再把旧拒绝当当前阻塞，也不以电池ADSP13提升音频服务资格。见[控制链记录](2026-11-15-audio-prm.md)。
- **mode4来源与主机准备（已收口）**：当前trial内ADSP与26个真实Android分段重建文件逐字节相同，排除合并身份错误。原机实录GLINK先于audio_pd UP约30ms，GPR以音频UP门控，再取得APMready。新增default-off mode4：1秒有界settle后原机source3/token0查询，真实ready1才进入原source7/token0请求/释放；无retry/PDR资格声明或输出。定向生产core、平台22项、完整日用/新双kernel通过，source43hash一致，906B临时候选随后独立上机，实际结果见下。原先“若同拒绝则先PDR”的分支未触发：真实APM已ready，下一按原机回复形状推进mode5。旧三轮remote1保留，首次声音停叫用户。见[新增证据](2026-11-15-audio-prm.md#固件身份与原机音频域时序新的有界候选)。
- **mode4 / APM就绪DEVICE_VERIFIED（限定），PRM形状FAIL**：真实查询在连接后1,004,227us发出，收到实际APMready1；随后资源收到独立02001002/payload8，但首字段不符当前opcode-echo解析，按reason4/sent2ACK1中止，request/release0、uncertain1至reset。此处remote1是前APM字段，PRM原始状态未知。199秒返回/1499624B新鲜deadline/ADSP13/电池/无fatal/Android完成34.1°C。原机callback准确校验参数ID08001032+status，下一独立mode5仅增加该准确形状与两word日志，实际ready门控和全部strict检查保留；生产core/平台24/完整日用/新双kernel已通过，source43hash一致，906B临时候选已备；无clock/pins/codec/graph/output，不猜延迟或启PDR。见[实际结果](2026-11-15-audio-prm.md#mode4真实apm就绪通过prm回复形状失败)。
- **AUDIO-PRM-02 / DEVICE_VERIFIED（控制限定）**：mode5真实APMready1后，两份实际PRM完成均为08001032/status0，sent3ACK3/request1release1/held0uncertain0；1398023B新鲜deadline/ADSP13/电池/无fatal/Android完成35.4°C。不认物理时钟/声音。下一AUDIO-CLOCK-02独立mode6只读核SCK121/WS123稳定GPIO输入后，owncore→clock204/12.288M/attr4→clockrelease→corerelease，严格参数ID防旧ACK混用；无pin/codec/graph/DMA，生产core/平台29/完整日用/新双kernel通过，source43hash一致，906B临时候选已备。见[本批结果与下一增量](2026-11-15-audio-prm.md#mode5数字核心租约实机通过下一仅内部tdm时钟)。
- **AUDIO-CLOCK-02 / DEVICE_VERIFIED（内部控制限定）**：mode6真实ready/core申请/clock204与12.288MHz申请/clock释放/core释放五步实际ACK5，准确参数32/2c/status0，held0clockheld0uncertain0。SCK121/WS123只读cfg1/1确认GPIO输入隔离，无pinwrite/codec/graph/DMA/output。196秒返回/1481827B新鲜deadline/ADSP13/电池/无fatal/Android完成35.3°C，core/平台29/日用/新双kernel通过，source43match。当前全部任务终结、无后台心跳；下一单owner输出关闭组合执行器持clocklease接准确SCK/原机PLL并验HALO心跳，未实现，首声仍停叫。见[实际时钟结果](2026-11-15-audio-prm.md#mode6内部时钟控制实机通过)。
- **无人值守监督已停止**：本批主机增量完成，剩余不足完整设备回收预算，已删除本次两小时心跳（success=true），没有替代心跳或后台设备任务。所有已派host/build/device均终结；10:52:37独立只读Android完成/30.2°C。当前已装OS/UI/helper与OS370/回退不变，无commit/push。该段记录前一窗口；本次按用户新授权前台接续，mode13独立设备轮由30分钟supervisor监督，未恢复旧心跳。
- 完整范围与安全门槛见[适配计划](../plan/2026-11-15-ipados-hardware-adaptation.md)。首次声音/外屏/自动切屏仍留现场；本批仅共用IPCC/ADSP限定回归。

## 当前主线与一小时人工验证（11-14 23:14）

- **目标ACTIVE**：优先检查 GNSS/位置、传感器、实际声音、折叠状态与双屏路由，利用用户的一小时现场配合。主 Android 基线；iPadOS 外屏、GPS、声音需各自驱动和真实设备资格。后部 USB-A、临时 fastboot boot、保留自动返回、旧Data/OS370/备份；不刷分区。
- **已获证据**：原生定位总开关及 Weather“使用 App 时允许”由正常 UI 开启；Weather 保存城市上海。Android 枚举 LSM6DSO 加速度/陀螺仪、AK0991X 磁力计、前后 TCS3720 光感、接近、Xiaomi fold_status/hinge-angle。完整合盖循环见 `fold_status` 约178°→4–7°（closed位0→1）→174–178°（回0），重复两次。Android音频路由显示主扬声器，实际声学播放/麦克风待现场验证。
- **GNSS待验证**：Android位置开关与 MIUI Weather fine/coarse appops 均允许，但 `ProviderRequest=OFF`、`mStarted=false`、last location null、0 reports/TTFF/CN0。此前打开天气时仍锁屏；当前需用户解锁、室外/无遮挡窗边将 MIUI Weather 前台切到当前位置并保持亮屏，采样约3分钟。未使用 mock location。
- **折叠与显示**：有限 display 快照显示主屏ON、第二个1080×2520 display OFF；没有证明全折叠全过程与display切换关联。Android是否自动路由仍待同步复核；iPadOS仅主内屏驱动，FOLD-01自动切屏未实现。
- **时钟线索**：Date & Time页确认“自动设置”原为开、timezone行spinner；正常UI关闭自动设置后显示Los Angeles，时间仍约06点，主机为22点。没有手工设时区。下一次应经原生Time Zone picker选Shanghai并核准当前时间，再独立观察天气网络；时区目前只是可能因素，非天气根因结论。
- **一小时下一步**：GNSS等你解锁后在室外打开MIUI Weather当前位置并保持亮屏约3分钟；Audio先验证Android本机扬声器/录音的实际功能，iPadOS AUDIO-01仍需CS35L41适配；Fold传感器已闭合资格一轮，继续同步观察Android双屏state与折叠。具体现场步骤见[检查点](2026-11-14-sensors-gps-audio-fold.md)。
- **自动返回温度门槛已按用户要求移除**：`cycle.sh`和APP-01 supervisor不再等电池≤35°C；温度仍写日志，30分钟单轮、klog、返回/Android witness保持。长轮或受热 ramoops 质量须单独评价。
- **磁盘清理已完成**：按仓外清理manifest只删除25个旧22G100编译树/Rust target目录，预计6.57 GiB；raw系统/blank Data、KDK、源码快照、receipt/log、当前root-candidate-display build及22H374回退链保留。卷可用空间约由93增至99 GiB。见私有 `work/native18-20261114/cleanup-build-artifacts-20261114.json`。
- 一小时现场顺序与来源在[本轮传感器/GPS/音频/折叠记录](2026-11-14-sensors-gps-audio-fold.md)。Safari历史限定证据与Weather维修暂保留，不抢当前现场验证。
- **接线与资源候选**：实际16MiB分配失败来自DATA大对象范围压力；已有`vm_compression_limit=1048576`候选保持压缩器和安全范围，实际DATA大对象773→3052MiB，多轮未再有该失败；容量取舍仍需压力/长稳，不推广默认。新kernel失败范围日志和可选只读静止截图已主机双配置/实机执行。
- **已修/已拒基建问题**：AMP属性漏传、命令行超限、旧UI未部署URL入口、Weather错误回退到Safari、Mach-O新增命令覆盖Safari入口均有控制及修正；真实fatal保留。HTTPS“错误页”实为站点说明的误判已撤回。缺像素整轮FAIL/逐张分判，原始负结果不删除。
- **21:45 更新**：Safari慢速真实HID输入使地址栏文本变成 `https://example.org/`，但WKWebView URL仍为 `https://example.com/`；输入未触发导航。截图1缺列、截图2完整，保留capture FAIL；不升级Safari地址功能。Sheng键盘缓冲/串行修复仅作检查参考。Weather主机已从22H374 WeatherDaemon元数据找到 `performRequestsWith:completion:` 与JWT认证入口，并实现default-off observer；host ASan/UBSan、iOS编译/签名PASS。Weather observer UI从实际keyboard-v2派生安装，整卷核验PASS，当前u轮唯一设备owner；u轮回执PASS但截图被小组件位置提示遮挡，整轮截图FAIL；请求边界仅见edge configs/bag，不见WeatherKit主预测URL。w只提前open-at，仍未看到forecast调用。x artifact标签前置FAIL但没有发送设备命令；y更正引用原图像，当前只读日期与时区Settings。参考[晚间接续调查](../research/2026-11-14-weather-safari-followup.md)。
- **22:30 更新**：aa实际Settings dump确认自动设置on=0、24小时制on=1、时区spinner消失，完整截图通过；此时没有改变时间或时区值。当前ab只选择Time Zone行查看城市选择页，不提交新城市。
- **22:24 更新**：同版 WeatherDaemon含 `Failed to obtain system timezone.`，Date & Time页确认 timezone spinner 无数据，自动设置开；系统时钟落后16小时。将时区列为强候选，但尚无天气因果证明。aa只通过正常系统UI关闭自动设置，随后确认时区控件状态；暂不修改时间或选择城市。
- **22:15 更新**：u显示 WeatherDataService ABI匹配但forecast无调用，小组件位置提示是真实modal；v/w点击时序仍早于 Weather 页面打开，不能算否决权限；Weather页面仍无温度。x因artifact路径标签错误在preflight前FAIL，phone写入为false；当前y用正确已构建artifact，只读 Date & Time Settings。host和boot epoch约22点、系统UI约06点为时区假设，尚未从设置页确认。
- **21:59 更新**：w目标是固定的真实HID点x56/y58对widget“不允许”，但v轮实测先后顺序是触点在Weather open之前，modal仍显示，不能记dismissal。w只把`pong_open_at=20`改为1秒，以在后部USB-A单设备有界周期中尝试让提示先出现。Weather仍无温度/预报，权限与保存城市未变。
- [本批交付与完整历史](2026-11-14-app-unattended.md)保存逐轮结果；[上游与修复取舍](../research/2026-11-14-main-app-repair-directions.md)、[Weather/Safari接续调查](../research/2026-11-14-weather-safari-followup.md)和[系统应用方案](2026-11-14-system-app-resume.md)提供依据。所有主机、准备/安装、设备结果分别记录，不把诊断安装和往返成功当应用通过。

## 历史：2026-11-14 回到 main 时的检查点

以下为15:59接续快照，已被顶部无人值守检查点取代；旧“下一步/已安装”不再是当前执行状态。

- 用户已将开发主线切回现有 22H374 系统应用；当前分支 `main`，原生 22G100 迁移保留在远端 `feature/native22g100-arm64e`，不再自动推进原生桌面目标或切换分支。原生后台开发提醒已撤销，余下云端 CI 仅独立只读跟踪。
- **APP-01 为 P1 主线：近期目标是固定城市天气真实温度与预报，随后补 Safari/设置/时钟/照片核心操作。** 当前天气仍 `--`，GPS 不是固定城市数据请求的前置。
- 最小下一设备交付为有效原生定位页画面和独立网络判定：重验实际安装/备份/extent/manifest/返回，使用新 run 和新鲜 epoch 重新打包同 kernel/loader/OS 的双时点截图候选。旧 v25 四项产物 hash 本轮相符，但未启动，旧时间/arm/preflight 不复用。
- 阻塞分开处理：曾有 locationd/fairplayd 同步等待（v23 干预后已改善但无完整 B2 对照）、提示取消后权限未知、绝对时钟导致截图过早、v24 的真实 64 MiB Wi-Fi DMA 分配失败、IOSurface/renderer 资源限制。不能将这些合称 GPS 或 Apple 天气服务故障。
- **15:59 只读设备状态**：Android / zizhan、boot_completed=1、100%、28.5°C，无活动设备试验/安装器。已安装旧 v23 和 canonical OS370 的区别保持；本轮尚未重新整窗读回或启动 iPadOS。旧 Data/备份保护。
- 详细应用表、证据与解决顺序见[系统应用接续](2026-11-14-system-app-resume.md)。本轮仅切换/调查/目标整理，尚无新代码构建或应用验收；下一步是新鲜前置和候选，不照抄旧 direct 脚本。

## 历史接续（2026-11-09 交接）

- **发布已收口（09:45）**：原28个累计提交已快进并推送main；GCC测试类型修正`de1ce13`的实际自动检查通过，手动云端精简内核42544865898亦SUCCESS，disk-probe SKIP。本地双配置内核及隔离UI编译/签名通过；各SHA/首次失败和最终结果分列于[发布记录](2026-11-09-publication.md)。文档收口不重复运行时构建，源码发布不等于推广已安装OS候选，独立UEFI/旧mirror待办仍留本机交接。

- **APP-01 主线**：天气仍无温度/预报。v23r5同运行时仅移除fairplayd任务后，locationd旧MIG1203阻塞解除、weatherd按需启动，用户+部分截图确认定位提示；用户误点取消。全局开关、授权成功、GPS、天气数据源均未验收。
- **实际安装为隔离v23候选，canonical仍OS370**，未默认推广，回退完整保留。v24r1定位设置导航accepted1且Preferences启动，但截图因绝对时钟偏移过早，开关状态SKIP；该轮WiFi arena失败，不能验天气网络。
- **唯一下一设备交付**：重新核对真实接口、输入与日志准备后，以新run执行已备v25双时点截图包，取得原生定位页面状态。v25从未启动（NO_FASTBOOT、未发送）；无活跃设备任务或心跳。隔夜旧arm/温度不得直接沿用。
- **09:00交接只读观察**：adb/fastboot均空；USB后续核对为05c6:f000关机充电接口。当前屏幕/实际运行状态未直接观察，不能写成Android或fastboot。恢复真实调试接口是设备前置。
- **POWER-02保持受控支线**：须同时满足`ZIZHAN_DSI_DMA: buffer ready`与128MiB布局下显示域仍`IdentityObserved`才重新进场；此前不执行panel off、锁屏或真睡眠，不继续无结果的诊断审计轮。
- 汇总和证据边界见[本次交接](2026-11-09-weather-handoff.md)，完整轮次见[天气技术记录](2026-11-08-weather-authorization.md)及[panelpool记录](2026-11-08-panelpool-128.md)。本机工件/命令/回退在仓库外`work/CURRENT_HANDOFF.md`。这些记录不另建任务队列。

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
| HALL-01 | P1 / HOST_VERIFIED | 默认关闭且有限的 GPIO62 原始电平/去抖事件观察，不连接屏幕或HID；设备资格另需新鲜日志和姿态关联。 | probe回放/实际ADT/去抖sanitizer、平台archive及精简/完整kernel通过；无设备轮次。保持候选隔离，下一可按前置完整后准备一次采样。见[交付](2026-11-15-hall-observer.md)。 |
| SENS-01 | P1 / BLOCKED | 真实运动/磁力/环境数据进入原生消费，保留协议/单位/时间戳与实际变动证据。 | 固件放置、IPCC及remote3稳定元数据已有分级证据，SLPI未启动；六项带宽继承票未知，SMP2P/RX缺失。下一按SENS-BASELINE-03限时解阻；资源和远端所有权未经确认不初始化。 |
| GNSS-01 | P1 / TODO | 真实定位请求→GNSS session/卫星/fix→iPadOS原生位置API；权限、精度、新鲜度独立验收。 | 先盘点本机GNSS固件/远端服务/协议和22H374 provider接口，不能等待或伪造Android fix代替iPadOS适配。现场定位须解锁/有有效前台请求。 |
| APP-01 | P1 / IN_PROGRESS | 主线。逐应用记录启动、内容显示、核心操作、联网、输入及前后台表现；至少覆盖 Safari、设置、时钟、天气、照片，产出状态表并据此定序。 | 11-14回到main续接。天气仍无温度/预报；v23旧locationd阻塞改善、提示取消，v24页面采样过早且64MiB Wi-Fi DMA申请失败。旧v25未发送，hash相符但epoch已过时；下一重验实际安装/extent/备份后，以新run和新鲜epoch重新打包同运行时双时点截图，取得有效原生定位页及独立网络判定，再走固定城市真实数据请求。完整方案见[系统应用接续](2026-11-14-system-app-resume.md)，不自动改变授权。 |
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
| AUDIO-01 | P1 / IN_PROGRESS（长期目标，本轮未执行） | CS35L41真实播放/停止/音量与恢复，麦克风另建链。 | 输出关闭下保护程序/数据消费与清理已有实机证据；AUDIO-PULSE-01首声包封存等现场。下一AUDIO-STREAM-01主机有限批次核心，不能直接开启Pong TFA后端或宣称App播放。 |
| BT-01 | P3 / TODO | 本机蓝牙传输/固件与原生栈，发现/配对、HID、音频分别建链及验收。 | 先做只读资源和接口盘点，再选择有界探针；音频与待机恢复分别列依赖。当前无本轮蓝牙资格。 |
| CAM-01 | P3 / TODO | 本机相机；传感器/ISP/固件、单摄预览、拍照与 Photos 保存，随后录像和其他镜头。 | 先盘点板级输入、缓冲及原生接口，选择最小闭环；新规划项，无本轮实现或设备结果。 |
| DISP-01 | P2 / TODO | 自动环境光、光度校准、睡眠恢复和运行中刷新率切换分别验收。 | 先明确电源/传感器前置条件；当前仅承认手动亮度和两种刷新率正常启动。 |
| FOLD-01 | P2 / IN_PROGRESS | 外屏独立60Hz测试图、真实帧推进及熄屏恢复；随后外触控、手动切屏、Hall自动切屏和失败恢复。 | Android fold_status设备证据已见完整178°→4–7°闭合位翻转→展开重复；有限display采样里内屏ON、外屏OFF，但未和折叠时间同步到完整路由结论。iPadOS外屏仍未实现。见[本轮记录](2026-11-14-sensors-gps-audio-fold.md)及[硬件调查](2026-11-06-fold-power-audit.md)。 |
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
