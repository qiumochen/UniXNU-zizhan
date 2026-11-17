# CS35L41 路径的厂商RX阈值初始化

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。接续[RX交接期限失败](2026-11-15-amp-rx-handoff.md)，本批解决已确认的FIFO RX初始化缺项，验证它是否影响持续的RX IRQ，不预设因果结论。当前队列见[STATUS](STATUS.md)。

## 固定源与最小变更

已定位同一厂商revision `cb7a356ae138e77992ae199eebb86559d825a673` 的真实helper：[drivers/platform/msm/msm-geni-se.c](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/cb7a356ae138e77992ae199eebb86559d825a673/drivers/platform/msm/msm-geni-se.c)。`geni_se_init` 通过 `se_set_rx_rfr_wm` 写RX watermark/RFR；已留存的同revision I2C调用点用TX depth−1与TX depth作为这两个参数。原目录接口HTTP500与先前错误路径404保留，成功源码/hash留仓外。

本机TX/RX均32位宽、深16已有实机资格，继承RX watermark0/RFR14与厂商初始化不同。独立 `zizhan_se15_prepare=7` 在复位释放/I2C前增加生产 `prepare_rx`，要求已选合法时钟、I2C协议/FIFO模式、空闲SE/IRQ/RX及两侧精确深度16。最多两次阈值写 `0x810=15`、`0x814=16`，各自精确读回，已匹配则不写；失败不释放复位或发I2C。只导入这两项，不复制helper的输出控制、IRQ使能或全位清除。

旧模式1–6保持原范围。之后仍是原safe-off→一次有界RX交接→原快照，没有忽略bit7或增加确认次数。交接额外只读slave IRQ/DMA RX IRQ，若仍持续可据实定位底层来源，不清它们。原始OTP若取得只留私有klog。

## 主机验证与试验

生产RX核心12项控制覆盖0/14→15/16精确两写、已匹配0写、无时钟/协议/DMA/active/IRQ/RX/不符深度零写、任一目标写拒绝和读回失败；所有原时序/事务/快照/交接负控制保持。实际平台22项控制通过，完整日用host与新双kernel收口后才打包。

控制为mode6实机阈值0/14且清一次后IRQ80持续，变量为mode7在I2C前初始化阈值15/16；OS/UI/helper/总线/audio0/ADSP2/route1与返回路径不变。验收要求RXsetup PASS及阈值读回、safe-off/交接/OTP各自终态，每颗真实OTPID/32word/禁止输出、finalreset，最多两阈值写与一次已知RX确认；fresh wholeOS/extent/manifest/backup、单owner后部USB-A、deadline klog、ADSP13/真实电池/无fatal音频和独立Android完成返回仍必须满足。

## 当前状态

完整日用host、新reduced/full kernel及909字节候选均完成。**RX阈值初始化限定DEVICE_VERIFIED，handoff仍FAIL，OTP SKIP。**

07:48:12临时启动，197秒返回fastboot，1411403字节新鲜deadline klog，fresh wholeOS/extent/manifest/backup PASS；ADSP13、真实电池发布后30条更新、无ADSP失败/fatal/panic/音频，Android完成返回通过。返回温度34.0°C，07:52:54独立观察Android/zizhan/boot_completed=1、34.9°C。

实际RX setup：TX `0x201029e4` / RX `0x20104800`，watermark `0→15` / RFR `14→16`，两次写及精确回读，通过；当时I2C命令0、reset未变。两颗safe-off仍PASS/4次清零确认，最终resetasserted。与上一轮相比，master watermark bit26不再出现，但master RX IRQ `0x80` 仍持续：handoff在phaseidle/SE0/RX0下，一次W1C mask80后IRQ80/80，16个样本至期限reason4失败。不能说单改阈值已解决交接。

本轮另读到 `slave_irq=0x08000000`、`dma_rx_irq=0`。固定厂商头文件定义slave bit27为 `S_RX_FIFO_LAST_EN`，slave status在0x640、clear在0x648。此证据指向尚未确认的slave RX-LAST来源，master汇总位继续存在；本轮没有清slave或DMA状态，因此不能将它记为已验证清除方案。snapshot明确SKIP、没有OTP_ID/word，不把未读零值当数据。

独立审阅保存 `PASS_SCOPED_RX_THRESHOLD_SETUP__FAIL_HANDOFF_SLAVE_RX_LAST__OTP_SKIPPED`；完整snapshot reviewer断言失败保留，监督PASS只是返回/日志收集。没有key/OTP/trim/output-enable操作、保护固件加载或声学资格。

## 下一单一增量

保留已通过的RX15/16和safe-off，新增独立默认关闭的source-first交接：同owner且实际phaseidle/SEinactive/RXempty、DMA RX IRQ0、slave仅已观测RX-LAST bit27、master仅已知RX汇总位时，最多一次确认slave当前RX-LAST，再有限读回slave/汇总状态；必要时只对仍在允许集合内的master位一次确认，不能循环写或清全位。未知/error/active/data/DMA状态全部拒绝，失败retain owner/finalreset。写入顺序与边界须生产模型覆盖slave-last汇总依赖、迟到清零/重现/冻结时钟/部分失败/未知位，再新双kernel和fresh设备前置下新轮。旧mode7保留当前负结果，不调回阈值、忽略80或重复master清除。

真实OTP取得前不解码或补默认值；保护固件/播放路由仍未通过，首次声音现场，无commit/push或OS/Data推广。
