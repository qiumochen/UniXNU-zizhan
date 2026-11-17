# CS35L41 slave RX-LAST来源先确认的有界交接

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。接续[mode7实际接收完成来源](2026-11-15-amp-rx-setup.md)，本批针对已观测slave RX-LAST与master RXIRQ共存的情况，不改变功放输出边界。当前队列见[STATUS](STATUS.md)。

## 独立默认关闭模式

新增mode8和独立source_handoff，保留旧mode7的master-only负控制。仍由实际safe-off成功的同一独占worker持有总线，要求两颗输出/boost/HALO禁止、写确认一致和无uncertain。沿用已通过的RX阈值15/16与原快照检查。

每个样本检查phaseidle、SEinactive、RXempty、DMA RXIRQ0、slave IRQ仅允许已观测RX-LAST bit27、master仅允许已知RX集合。先读slave，再读master汇总，避免用源变化前的汇总值判断。当初始slave RX-LAST非零时，只向0x648一次W1C它的当前值；slave未清零前不清master。slave为0后若master仍仅为已知集合，最多向0x618一次确认；清源后汇总自动消失则不写master。最多500us/26样本，需要两次间隔零源/零汇总；未知/error/active/data/DMA非零、清零后重现均FAIL，持续level到期限FAIL。无重复写、FIFO drain、额外I2C命令或使能位写。

失败或完成仍最终assert共享reset、保留owner；失败不进入OTP。没有钥匙、OTP编程、trim、保护固件或播放路由操作，首次声音仍现场。

## 验证与试验

生产GENI模型新增source→summary依赖，覆盖源清理后汇总自动消失或需要一次确认、迟到清零、持续level/冻结时钟、源重现、DMA/未知slave/master/残数据、过程中active以及零源零写；另接原实际snapshot成功路径。测试发现源/汇总采样顺序问题后已调整为先源后汇总；早期失败和人工重现时点/日志断言修正记录保留，不以fixture通过冒充硬件结果。

实际平台23项控制通过，完整日用host和新双kernel收口后才打包。控制为上一轮RX15/16已通过但slave08000000/master80未清，变量仅改mode8的有界来源先确认，OS/UI/helper/总线及audio0/ADSP2/route1保持。

验收需RXsetup、safe-off、source_handoff、真实逐颗OTP四项终态和各自证据：最多一次slave已知确认、其清零后才可选一次master确认，idle/empty/DMA0、稳定零样本；每颗真实OTPID/32words与禁止状态，finalreset；还需fresh wholeOS/extent/manifest/backup、后部USB-A/单owner、deadline klog、ADSP13/电池/无fatal音频和独立Android返回。任何partial不算通过。

## 当前状态

生产host、平台23项、完整日用host、新reduced/full kernel和909字节候选均完成，唯一临时轮已终结：**source_handoff仍FAIL，OTP SKIP。**

08:08:37临时启动，199秒返回fastboot，1501763字节新鲜deadline klog；fresh wholeOS/extent/manifest/backup PASS，ADSP13/真实电池发布后31条更新，无ADSP失败/fatal/panic/音频。Android完成返回通过、34.0°C；08:13:30独立观察Android/zizhan/boot_completed=1、34.9°C。

RX15/16初始化及两颗safe-off再次通过，最终resetasserted。来源先确认实际一次slave W1C `0x08000000` 后，slave读回从 `0x08000000→0`，DMA RXIRQ保持0；源零后一次master确认mask80，master仍 `0x80→0x80`。phaseidle/SE0/RX0保持，15样本到期限reason4失败。候选没有重复任一层确认，snapshot明确SKIP，未读OTP_ID/word。这里只确认slave RX-LAST已清，不代表整个交接通过。

这个结果排除了“只缺slave RX-LAST确认即可使master80消失”的假设；不能据此把bit7当无害或忽略。早前仅按头文件将它称为RX汇总，尚未对当前固件的持续位语义/使能配置形成完整证明。完整reviewer通过断言失败保留，独立审阅记录 `FAIL_SOURCE_HANDOFF_MASTER80_PERSISTENT_AFTER_SLAVE_LAST_CLEAR__OTP_SKIPPED`。监督PASS只表示返回和日志收集；无key/OTP/trim/output-enable操作或声学资格。

## 下一单一交付

围绕这个确定反例，核对当前GENI固件的master bit7语义和最小FIFO配置：记录master/slave IRQ enable、SE路由、RX/DMA模式及相关接收配置，在来源确认前后保留同一组只读值，和固定vendor helper的FIFO mode实现作有界差异核对。实际固件和硬件值优先于按通用头文件推断，只有具体配置缺项得到证据时才修改，不整段照搬helper的IRQ全清/使能/输出控制。不再重复同一slave/master清除试验，也不把完整架构审计当进展。

下一候选的最小目标是确定来源清零后为何仍为80这一具体条件，保留guard；实际IRQ/路由状态未完整前不增加写入或忽略bit7。受影响生产控制及新双kernel通过后才fresh前置下独立临时轮。OTP真实数据未取得前不补零、默认值或Android替代，不执行校准值写入。已安装OS/Data/OS370和回退链不推广，无commit/push。
