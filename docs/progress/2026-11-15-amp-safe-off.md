# CS35L41 清除使能位与逐芯片OTP快照

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。接续[上一轮快照前置失败](2026-11-15-amp-otp-snapshot.md)，只解决真实PWR2/HALO使能位未清这一条件，不放宽原快照门槛。当前队列见[STATUS](STATUS.md)。

## 默认关闭的最小操作

新增独立 `zizhan_se15_prepare=5` 和生产 `zizhan_cs35l41_safe_off`。模式1–4保持原范围。实际数字启动/身份后先读取两颗的PWR1/PWR2/HALO并要求B2和GLOBAL_EN=0，任何一颗不满足都在功放值写入前中止。随后仅清除PWR_CTRL2的AMP/BST mask `0x31` 与HALO core-enable mask `1`，保留其他位；每次写前重读global和目标，状态变化即失败，写后及整颗完成后均精确读回。不硬编码上一轮整寄存器值，不设置任何使能位。

单事务沿用20ms/cancel2ms/abort2ms与迭代上限，正常路径最多36个事务、4次BE32寄存器地址+值写；已清零的目标不写。尝试与确认写入分别计数，失败不重试、保留owner/部分结果，由平台最终assert共享reset。没有钥匙、OTP编程、trim、路由、固件或输出使能写入。

safe-off通过才接原只读快照，前后状态/逐芯片32word检查保持；失败则快照SKIP。原始OTP只留私有klog，公开文档不带校准payload。快照成功也不代表保护固件、调音、ADSP路由或实际声音已适配。

## 主机与候选

实际CS35L41+GENI sanitizer通过：测试BE32值写、保留其他位、最多4写/已清零0写、两颗global前置及运行中变化、36事务位置逐项bus失败、写确认与回读不符、revision、状态变化、冻结时钟和超时；再接原快照成功、数据分颗且总122事务。所有先前identity/snapshot控制保留。

实际平台入口20项控制、完整日用host、新reduced/full kernel均通过。候选已打包，909字节命令行、模式5/audio0/ADSP2/route1，210/150秒自动返回及30分钟监督。这里只记PREPARED_ONLY；fresh wholeOS/extent/manifest/backup、实时设备owner和后部USB-A仍须在试验前重新满足。

## 实机验收与当前状态

本轮已终结：**clear-only safe-off限定DEVICE_VERIFIED；完整OTP快照FAIL（Busy前置），没有取得OTP数据**。相同已安装OS/UI/helper，控制为上一轮mode4，变量只增加清零操作，旧快照门槛保持。

06:48:47临时启动，197秒回fastboot，1371097字节新鲜deadline klog。fresh wholeOS/extent/manifest/backup PASS，ADSP13、真实电池发布后31条更新，无ADSP失败/fatal/panic/音频；Android完成返回PASS，返回温度34.1°C，06:54:48独立观察Android/zizhan/boot_completed=1、33.9°C。

两颗实际均为 `PWR1 0→0`、`PWR2 0x3321→0x3300`、`HALO 0x101→0x100`；前后各3项读回，safe-off PASS/completed2/attempted4/acknowledged4，其他位保留，全局输出始终未开启。最终共享resetasserted，无总线错误或uncertain，key/OTP/trim/output-enable写均0。这里只证明指定使能位清零并读回，不代表保护DSP、模拟链或声音可用。

紧接的snapshot在重新进入身份/总线前置时返回reason2（Busy），completed0/words0，两颗OTP_ID和OTP word均未读取。平台终态FAIL；核心前置SKIP，不能把日志零值当硬件数据。当前日志未记录是软件phase、SE active、IRQ还是RX残留触发，**Busy具体来源未确定**。独立审阅同时保留safe-off通过与snapshot失败；原acceptance reviewer断言完整snapshot应PASS失败保留，没有改为忽略。

## 下一单一动作

在快照入口保存并输出实际software phase、SE status、IRQ和RX FIFO原始前置值，补充“事务完成后状态仍暂留/电平重置位”的生产host控制，定位并实现同owner有界交接。当前不能盲目忽略Busy或清未知IRQ，也不把原因直接归为迟到RX/完成位。先保留这些真实输入；只有实际idle/empty和明确已管理的状态才能决定是否清理或等待，其他错误继续拒绝。改变输入后仍须受影响host及新双kernel，再准备独立候选；不原样重跑mode5。

真实快照取得后才按各自OTP_ID host解码，不用人工值补齐。首次实际声音留现场，已安装OS/Data/OS370/回退链不推广，无commit/push。
