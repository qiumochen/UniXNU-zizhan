# CS35L41 快照入口Busy的原始状态记录

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。接续[safe-off通过、快照Busy](2026-11-15-amp-safe-off.md)，本批交付为确定Busy来源所需的实际状态记录，不重复证明功放清零。当前队列见[STATUS](STATUS.md)。

## 最小变更

原生产identity前置结果增加valid、entry phase、SE status、master IRQ和RX FIFO快照。valid bit0表示实际软件phase已采，bit1表示三项硬件原值均已采；未采值不能作硬件证据。busy_sources分别为1软件phase、2控制器active、4任意IRQ非零、8 RX count非零，可并列。硬件输入各读取一次，不因前一项不符而丢失后续项。

默认关闭与板型/模式/owner检查不变；软件phase不空闲则保持原样返回，硬件错误/活动/残留仍拒绝。只增加原值记录，不清IRQ、不读空FIFO、不忽略Busy、不增加重试。平台在safe-off和snapshot两个入口分行记录，终态仍保留PASS/FAIL/SKIP和最终reset。

生产GENI sanitizer新增独立phase/SE/IRQ/FIFO及组合拒绝、事务成功后completion重新出现的控制，保证证据完整且zero extra writes；此前identity、OTP/safe-off、36事务位置失败和所有guard仍通过。实际平台20项控制通过，完整日用host与新双kernel用于本次候选资格。

## 试验设计

控制是上一轮mode5两颗safe-off通过、snapshot Busy但原值未知；变量只增加只读入口采样与日志，仍mode5/audio0/ADSP2/route1。目的为收集两个入口valid/phase/SE/IRQ/RX及busy_sources，确定来源；即使明确拒绝并无OTP，也可完成此诊断交付，不能提升为完整OTP资格。

上机仍要求fresh installed wholeOS/extent/manifest/backup、后部USB-A/单owner、新鲜deadline klog、自动/panic/power-key返回及30分钟监督。验收还需safe-off终态、原快照终态、finalreset、ADSP13/电池/无fatal/音频、独立Android完成返回。未知/缺失的原值不能当已定位原因。

## 当前状态与下一动作

生产host、平台20项、日用host与新reduced/full kernel均通过，909字节候选完成唯一临时轮。07:05:46启动，197秒返回fastboot，1477596字节新鲜deadline klog；fresh wholeOS/extent/manifest/backup PASS，ADSP13/真实电池更新/无fatal/音频，Android完成返回通过。返回温度34.4°C，07:10:27独立观察Android/zizhan/boot_completed=1、35.3°C；温度只记录，没有冷却门槛。

实际两个入口为：

| 入口 | valid / phase | SE status | IRQ | RX FIFO | busy_sources |
| --- | --- | --- | --- | --- | --- |
| safe-off | 3 / 0（idle） | 0 | 0 | 0 | 0 |
| snapshot | 3 / 0（idle） | 0 | `0x04000080` | 0 | 4（只有IRQ） |

固定厂商头文件将bit26定义为RX FIFO watermark，bit7为RX IRQ；这里不是CMD_DONE（bit0），也不是活动命令、软件phase忙或未读FIFO。只能证明本次采样的这两个RX状态位置位，不能进一步推断watermark配置或为何重新置位；原始路径与固定revision/hash保留仓外。

safe-off两颗再次精确读回PWR1 0/0、PWR2 3321/3300、HALO101/100，4次清零确认、finalresetasserted，无bus error/uncertain或key/OTP/trim/output-enable/audio写。snapshot仍按Busy(reason2)拒绝，未读任何OTP_ID/word，不能说OTP通过。独立审阅记为 `PASS_SCOPED_DEVICE_BUSY_ENTRY_DIAGNOSIS`，快照资格仍 `FAIL_BUSY_BEFORE_OTP`；监督脚本PASS仅说明返回和收集。原负结果完整保留。

下一单一实现：独立默认关闭的同owner交接，在已经完成safe-off且实际phase idle、SE无active、RX FIFO empty、IRQ只含已观测RX mask `0x04000080` 时，一次性仅确认清除当前置位的这两个位，随后在短期限与固定迭代内读回IRQ/idle/empty；不重复写、不清错误或未知位，不借此处理非独占总线。若错误/活动/残数据/重置位不消失则FAIL保留owner并最终reset，禁止忽略guard。需要加入实际生产清除/延迟/level reassertion/超时/未知位负控制及新双kernel后，才准备独立设备轮。若再次出现同样电平，先记录实际watermark配置再改变策略，不猜测阈值。

OTP、保护固件与播放链仍未通过，首次声音现场，无commit/push或OS/Data推广。
