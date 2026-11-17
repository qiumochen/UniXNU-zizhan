# GENI RX汇总持续时的只读配置证据

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。接续[slave来源已清但master80持续](2026-11-15-amp-source-handoff.md)，本批最小交付是取得同组实际中断/模式配置，从具体反例决定下一改动，不再重复相同位清除资格。当前队列见[STATUS](STATUS.md)。

## 只读记录范围

独立mode9沿用mode8的阈值、safe-off、source-handoff和原快照门槛，只在handoff前后各采24个寄存器。包含phase/SE、主副固件revision、master/slave IRQ status与enable、SE route、DMA mode/byte granularity、RX packing、TX/RX FIFO status、RX阈值、DMA TX/RX status与enable、GSI event、DMA general、IO与DMA debug。各值为按序单次读，不能说是原子快照；valid记录实际已读个数。日志在最终共享reset后输出，避免打印本身拉长交接。

不读FIFO data、清除/控制寄存器或I2C payload，不增加硬件写/重试，也不提升guard。默认关闭和独占平台前置仍必须满足。生产host验证有界24读、原值/partial validity和zero writes，原identity/snapshot/safeoff/source控制保持；平台24项控制通过。完整日用host与新双kernel收口后才打包。

## 对照与验收

控制为mode8实际slave LAST→0/DMA0但master80持续，enable/路由/接收配置未知；变量为mode9增加两组有序只读值，其他输入不变。验收为两组valid24、阶段/phase明确、原handoff/OTP终态和finalreset保留，能对固定vendor FIFO-mode差异给出具体判断。此诊断PASS可伴随handoffFAIL/OTPSKIP，不能作为OTP或播放资格。

仍需fresh wholeOS/extent/manifest/backup、后部USB-A/单owner、自动/panic/power-key返回/30分钟监督、deadline klog、ADSP13/真实电池/无fatal音频和独立Android完成返回。未知或缺失的配置值不补默认，失败不原样重跑。

## 当前状态

生产host、平台24项、完整日用host、新reduced/full kernel及909字节候选完成，唯一配置诊断轮取得两组valid24。**配置采集限定DEVICE_VERIFIED，原handoff仍FAIL/OTP SKIP。**

08:26:55临时启动，196秒返回fastboot，1374279字节新鲜deadline klog，fresh wholeOS/extent/manifest/backup PASS；ADSP13/真实电池更新/无ADSP失败/fatal/panic/音频，Android完成返回通过。返回温度34.2°C，08:31:46独立观察Android/zizhan/boot_completed=1、34.9°C。

| 实际配置 | 交接前 | 交接后 |
| --- | --- | --- |
| phase / SE status | 0 / 0 | 0 / 0 |
| master IRQ status / enable | `80 / 40007e3f` | `80 / 40007e3f` |
| slave IRQ status / enable | `08000000 / 03001e36` | `0 / 03001e36` |
| FW master / slave | `303 / 303` | `303 / 303` |
| external route / DMA mode / GSI | 0 / 0 / 0 | 0 / 0 / 0 |
| DMA TX status / enable | `0 / d` | `0 / d` |
| DMA RX status / enable | `0 / 1d` | `0 / 1d` |
| TX FIFO / RX FIFO | 0 / 0 | 0 / 0 |
| RX watermark / RFR | `f / 10` | `f / 10` |

RX packing `7f8fe/ffefe`、gran0、DMA general f、IO7/debug0前后不变。master status bit7虽然持续，但对应enable bit7为0，`status & enable=0`；slave来源清零，DMA/活动命令/数据/路由均未提供活动来源。这是本轮实际屏蔽状态证据，不等同于所有硬件情况下bit7无害，也不能把原始80写成0。

原source-handoff仍在15samples后reason4失败、finalresetasserted；source清除1/master确认1，无新写入。OTP明确SKIP，零ID/word。独立配置审阅与后续分析分开保存，诊断PASS不替代快照/声音资格。既有“任何raw IRQ非零都忙”检查保留于旧模式，但实际配置已提供一个更窄的候选判断条件，不应再重复相同W1C试验。

## 下一单一增量

新增独立默认关闭的owned-readiness候选，只在successful safe-off、真实firmware master/slave303、phaseidle/SEinactive/TX与RXempty、slave和DMA TX/RX来源均0、外部route0/DMA mode0/GSI0、master仅已知80且其enable位关闭/全部enabled-pending0时，明确分类为“已知且屏蔽的RX状态”。来源清除仍限已测RX-LAST一次，不再尝试对持续master80反复清除；原raw80、屏蔽位、来源值和分类标志都须保留。对enable bit7开启、未知IRQ/error、任何活动/数据/来源/配置变化继续失败，旧严格模式保持对照。

这项计划取代此前一律要求raw IRQ必须0的候选约束，依据是本轮实际status/enable/source证据；它不是无条件忽略80，更不是伪造ready。新生产负控制必须覆盖屏蔽开关/全部来源/固件差异/稳定两次读回与阶段间变化，然后新双kernel及fresh设备前置验证；若任一条件未实测满足就不采OTP。当前尚无此候选代码/包。OTP/保护固件/播放仍未通过，首次声音现场，无commit/push或OS/Data推广。
