# SLPI 专属传输项接续

源码基点为`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`加未提交修改。下午按用户授权接手两小时静音无人值守；当前队列见[STATUS](STATUS.md)。本批先收口昨夜未完整交接的默认关闭探针，目标是确定真实remote3分区及SMP2P/GLINK项是否存在、尺寸和头部是否可用，从而决定后续初始化路径；不是启动传感器或重复cmd-db查询。

## 资源交接的具体阻塞

固定厂商版本`7d10d738b2c4299f3b8549c61a01ece39f19c788`的`drivers/interconnect/qcom/bcm-voter.c`、`icc-rpmh.c`、waipio节点和clock源码均已按原保存清单重新核对哈希。已检查的`bcm_aggregate`/`tcs_list_gen`/`qcom_icc_bcm_voter_commit`从Linux软件节点请求产生average/peak并调用active、wake、sleep批写；它们没有交付开机固件留存的本机APSS资源票。`icc_get_bw_stub`返回零也是stub，不是硬件baseline。当前XNU公共owner只记录本次已完成请求，仍不能为6个未写过的带宽资源给出可信继承值。

这个结论限定在上述已读函数和现有owner，不宣称硬件永远没有读回方式。最小解阻输入仍是能标明资源、voter/DRV、active/wake/sleep状态及同一次启动归属的可信handoff，或经原机依据验证的非降票独立voter方案；Android重启前的软件值不能直接跨boot带入XNU。当前没有这些输入，因此SLPI资源写和PAS12继续停止，转入与其独立的传输项前置，避免再次执行同一cmd-db观察。

## 默认关闭的真实SMEM入口

`PongAdsp.cpp`的`slpiTransportObservation`已经存在于昨夜02:10的未提交源码，但旧构建receipt中PongAdsp哈希不同；它不继承旧source70内核的构建或设备资格。仅`zizhan_slpi_transport=1`、真实zizhan兼容属性与2MiB SMEM匹配时，在现有ADSP actor和已有TCSR硬件互斥下执行；无第二DSP线程、无QRTR端点替换。

`ZizhanSlpiTransport.h`有限读取APSS0/SLPI3的分区表、481/430 SMP2P与478/479/480 GLINK描述符/尺寸。执行两遍一致性核对，最多8192次32位读取、250ms；只发布完整稳定快照，错误输出不冒充可用分区。FIFO数据负载不读；不初始化出向项、不改head/tail、不发doorbell/SSR ACK，不投票或启动SLPI。TCSR互斥自身有必要的lock/unlock写，`protocol_writes=0`不表示整次操作零MMIO写。保留的slave位仅是原始留存值，不称ready或活跃远端。

## 主机资格与下一步

实际核心18项ASan/UBSan控制已重新通过，使用生产`QcomSmem`分配与`QcomSmp2p`生产者构造夹具，核对分区/项/异板/短项/变化/超时/读取失败及不读FIFO负载。新增平台测试直接抽取实际adapter函数，13项控制覆盖缺失/关闭/错误开关、硬件身份、区域、互斥失败、损坏表、外来分区与空分区，验证正常和失败均正确释放互斥、SMEM字节不变、保留边界日志。初次平台夹具的错误身份字符串比其伪造OSData长度少1字节，sanitizer拒绝；修正夹具为相同长度的异板身份后通过，生产身份守卫未放宽，失败日志保留。

已将两项接入日用host runner，完整日用回归、新精简与完整默认内核均通过，72项runtime源哈希保持匹配。已备独立910B静音包与单变量实验记录；精简kernel SHA-256为`c01692d8b604cae8c9f639c75f4b99815d4dc2336a73080ae01fc53bb1c3963b`，完整为`59591d6c2fcf2a55fd4a23362e4063418da654df2053b7c3f274eb04a52bdf20`。下一fresh安装/备份/唯一owner/rear USB-A前置后，派一次原OS静音观察；缺分区为SKIP、损坏/变化为FAIL，都不反推传感器状态。首次音频/外屏/用户动作边界不变，不commit/push。


## 第一轮：实际元数据已见，完整结果记录FAIL

v1通过原OS/manifest/extent/备份/后部USB-A/唯一owner前置，17:08实际临时启动，196秒返回，1,506,718B新鲜deadline及独立Android完成PASS。ADSP13、实际电池更新、无panic/ADSP失败/音频启动。5条唯一原始项记录为：481/430/480缺失；478描述符32B、479发送缓冲区16384B存在。原始前缀还记录remote3分区offset17f000/196608B、2copy/196read、present0xc，留存位未给ready资格。

完整结果长行超过实际日志单记录预算，末尾`finished=1`被截断，紧接的ADSP_PM SKIP出现在同一行。严格review因此FAIL，不能凭可读前缀提升完整验收。第一版review把拼接行内SKIP误判为整个探针SKIP，修正分析为按记录前缀识别后仍明确FAIL terminal format mismatch；两份记录均保留，不修改原始日志或设备结果。

下一候选仅将状态/边界与终结记录拆为两条短行，平台测试真实256B缓冲并拒绝截断；核心读取、身份、锁、预算及硬件操作完全相同。这个不同输入复核只解决日志交付缺口，不扩资源写/PAS12或称传感器已经工作。新host/日用/双kernel/匹配包及fresh前置仍须完成后才派下一轮。


v2的31项定向sanitizer（包含256B日志拒截断）、完整日用host及新精简/完整默认内核均通过，72项runtime源哈希匹配。精简kernel SHA-256为`c60d7195a7c82ea91a766c2166d1f00b77bc4f5292d0d0861bea47ad78031207`，完整为`bc4ee8a1389efcd80925957aba029c82deb6c261c1cc203350f181662061eb60`；旧v1构建保留。独立910B v2候选和实验记录已备，仅日志格式有差异；下一fresh实际安装核验后按原监督派一次复核，不扩大硬件动作。


## Afternoon final verified checkpoint

SENS-TRANSPORT-01: DEVICE_VERIFIED for stable metadata only. V2 captured two consistent copies with 196 reads: remote3 partition 0x17f000/196608 bytes, GLINK descriptor478 (32 bytes) and TX479 (16384 bytes) present; SMP2P481/430 and RX480 absent. Complete short state and terminal records pass strict review. Fresh1372786-byte deadline log, ADSP13, real battery updates and Android completion pass; no panic or audio startup. V1 truncated receipt remains FAIL. No SLPI startup, baseline, sensor-data or API qualification. No OS installation. Next: host preparation for remote3 initialization with qualified remote-quiescence/PAS lifecycle and isolated QRTR ownership; no repeated inventory. Six unknown bandwidth baselines still block hardware startup.
