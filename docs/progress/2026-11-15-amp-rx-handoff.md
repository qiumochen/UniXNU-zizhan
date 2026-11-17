# CS35L41 已知RX中断的有界同owner交接

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。接续[实际Busy来源](2026-11-15-amp-bus-entry.md)，本批仅解决safe-off与OTP快照之间已知RX状态位残留，不放宽原快照前置。当前队列见[STATUS](STATUS.md)。

## 默认关闭的交接核心

独立 `zizhan_se15_prepare=6` 在已有safe-off成功后才调用生产handoff，旧mode5保留原拒绝路径。要求两颗safe-off完成、写尝试/确认一致、输出/boost/HALO禁止读回及无uncertain，继续由同一独占worker持有总线。

每次采样必须software phase idle、SE无active、RX FIFO empty、IRQ仅含已观测 `0x04000080`。初次存在已知RX位时，最多一次W1C当前置位值到IRQ_CLEAR，绝不写全1或重复确认。IRQ原本0则不写。随后最长500微秒、最多26样本，要求间隔20微秒的两次IRQ0；若清零后重新置位、出现未知IRQ、活动命令或未读数据立即FAIL，持续RX状态到期限也FAIL。冻结时钟仍有迭代上限，不读RX data、发I2C命令、修改阈值或清错误。最终reset/owner保留由原平台负责。

额外只读记录继承的RX watermark/RFR阈值，若电平一直重现可据实决定下一步，不猜默认值。交接通过才进入原快照，原输出和总线检查保持；失败则snapshot SKIP。没有钥匙/OTP编程/trim/固件/路由或输出使能操作。

## 主机与试验

实际生产GENI sanitizer覆盖IRQ0零写、单RX位/两RX位一次清除、延迟清零、持续level/冻结时钟、清零后重置位、未知位、active命令、RX数据、前置缺失、旧模式和safe-off→handoff→原snapshot完整122事务路径。所有原identity/snapshot/safe-off测试保持；实际平台入口21项控制通过。完整日用host与新双kernel收口后才允许打包。

控制为mode5已知idle/SE0/RX0但IRQ4000080拒绝，变量为mode6的上述交接，OS/UI/helper/总线和audio0/ADSP2/route1不变。验收分别要求safe-off、handoff、实际OTP快照明确终态，最多一次精确RX确认及零IRQ读回、每颗真实OTPID/32word/禁止输出状态，最终reset；还需fresh manifest/extent/wholeOS/backup、后部USB-A/单owner、deadline klog、ADSP13/电池/无fatal/音频和独立Android完成返回。任何缺失或partial不算通过。

## 当前状态

生产host、平台21项、完整日用host与新reduced/full kernel通过，909字节候选完成唯一临时轮。**handoff FAIL（RX IRQ持续），OTP SKIP**，没有实际校准数据，不能记为OTP通过。

07:27:11临时启动，196秒返回fastboot，1378520字节新鲜deadline klog；fresh wholeOS/extent/manifest/backup PASS，ADSP13、电池发布后30条更新，无ADSP失败/fatal/panic/音频。Android完成返回通过、34.2°C，07:32:37独立观察Android/zizhan/boot_completed=1、34.3°C。两颗safe-off再次4次清零确认并精确回读，最终共享resetasserted，无bus error/uncertain，零key/OTP/trim/output-enable操作。

实际handoff结果：phaseidle/SE0/RX0保持，IRQ `0x04000080→0x80`，一次W1C mask `0x04000080`，12个样本后到500微秒期限，reason4超时。bit26 watermark已清，bit7 RX IRQ未清，不能从本轮断言其底层来源；候选没有再次确认或忽略bit7，snapshot明确 `SKIP handoff-prerequisite`、零OTP word。实际继承RX watermark=`0`、RFR=`0xe`，只采样未修改。独立审阅保存 `FAIL_RX_HANDOFF_PERSISTENT_RX_IRQ__OTP_SKIPPED`；完整验收reviewer的通过断言失败也保留，监督PASS仅为返回/日志收集。

## 下一单一增量

定向核对固定厂商 `geni_se_init` / FIFO RX初始化的实际实现和bit7来源，落实阈值及接收状态的必要条件后再改候选。已有同revision I2C调用点会以 `tx_depth-1, tx_depth` 调用 `geni_se_init`，header明确它配置RX watermark/RFR；本机实际TX/RX深度16已在先前实机确认。这是核对入口，尚不能据此把继承RX watermark0直接宣布为bit7根因或随意写15/16。此前错误的soc helper路径404保留，不重复下载同失败路径。

若来源确认最小FIFO初始化缺项，则实现默认关闭、精确深度/idle/empty/owner检查和阈值读回的新模式，再保留一次有界交接及原snapshot门槛；若bit7还有其他接收来源，则只增加必要只读状态，不猜测清除。生产负控制/新双kernel及fresh设备前置后才新轮，禁止旧mode6原样重跑、重复清除或忽略bit7。

真实OTP未取得前不host解码或编造输入；保护固件与首次声音仍未通过，首次输出现场，无commit/push或OS/Data推广。
