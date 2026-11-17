# CS35L41 逐芯片OTP与输出状态只读快照

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。接续[离线OTP计划](2026-11-15-amp-otp-plan.md)，本批最小交付为默认关闭的实际逐芯片快照路径，不执行初始化、trim、固件加载或声音。当前队列见[STATUS](STATUS.md)。

## 代码和验收范围

`zizhan_se15_prepare=4` 单独启用；模式1–3保留原范围。沿用严格板型/模式互斥、单worker、SE15 clock/pin/timing和共享GPIO1 reset owner，实际数字OTP启动/身份通过后，只读两颗B2功放的 `PWR_CTRL1/2`、HALO core control、OTP_ID及32个OTP word。单次事务仍为20/2/2ms，快照阶段2秒检查加有限读取次数，冻结时钟也不会无限循环。每颗完成后重读3个状态寄存器并要求一致；输出/boost相关位非零、未知OTP_ID、其他revision、部分读取、超时或总线错误均保留失败。

HALO的core-enable位单独报告，不从复位默认值推断停止。即便快照完整，也只代表实际数据和状态稳定，不代表DSP停止或允许初始化。没有任何功放寄存器值、钥匙、trim、OTP编程、DSP控制或音频输出写入；I2C写相位仅发送读地址。每个成功读到的word有独立index，部分失败不补零；原始校准数据只留私有klog。平台完成或失败后重新assert共享reset并保留owner。

## 主机与候选

实际生产CS35L41+GENI sanitizer通过，覆盖完整两颗BE32及不同数据、86个读取位置逐项总线失败、部分计数、活跃输出/boost、未知表号/revision、状态变化、总deadline、冻结时钟及默认关闭。实际平台入口19项控制通过，包含新mode4在reset失败时仍assert、旧模式/互斥/映射不回归。

完整日用host与新reduced/full kernel均通过。首次构建因沙箱禁止sysctl导致XNU MakeInc非数字参数而中止，未进入源码编译错误；保留日志后，在允许读取主机参数的环境重建双配置通过。早期两个单测失败是人工deadline时钟与前置事务pause计数控制不正确，控制修正后通过，失败日志保留。

候选已打包，命令行909字节、音频0/ADSP2、模式4，保留panic/power-key及210/150秒自动返回、route1和独立监督30分钟上限。这里只记录PREPARED_ONLY，不当已装OS或实机结果；实际运行前仍需fresh installed manifest/extent/备份/owner和后部USB-A前置。

## 实机状态

本轮已终结：**完整快照FAIL，输出前置门槛按预期中止**，不是OTP读取通过。06:30:03临时启动，197秒返回fastboot，取得1378348字节新鲜deadline klog，Android完成及独立返回观察通过。fresh manifest/全OS/extent/backup均通过；ADSP到stage13，真实电池发布后31条更新，无ADSP失败、fatal、panic或音频启动。返回温度33.0°C，稍后独立观察Android/zizhan/boot_completed=1、33.2°C。

第一颗（地址0x40）实际读到 `PWR_CTRL1=0`、`PWR_CTRL2=0x3321`、`HALO=0x101`：全局输出关闭，但AMP bit0、boost字段2和HALO core-enable bit0仍置位。不能把复位当作这些位已清零，也不能把配置位等同于实际声音或DSP已执行程序。候选在3项状态读回后按reason10中止，未读OTP_ID/OTP word；第二颗状态未读。日志中的未读零值有count0，不能当真实硬件读回。

最终共享reset已assert，bus_error/irq/uncertain均0，codec_value_writes=0/audio_output=0。新候选没有声学资格；独立审阅保存 `FAIL_SNAPSHOT_OUTPUT_PREREQUISITE`，监督脚本的PASS仅说明完整返回和日志收集通过，其历史device_acceptance RUNNING已由独立审阅收口。原始失败与候选保留，不原样重跑、不放宽快照门槛。

## 下一单一实现

已有真实值及固定寄存器位定义，下一批可实现默认关闭的B2 safe-off：要求GLOBAL_EN实际仍为0，仅对PWR_CTRL2的AMP/BST mask `0x31` 和HALO core-enable bit0做保留其他位的清零，精确读回确认，失败保留owner并assert共享reset；不设置任何使能位，不写钥匙/OTP/trim。实际GENI写事务、部分失败/超时/回读错误及生产平台入口须新增控制并重建双kernel后，才准备新的独立设备轮。当前没有此代码或候选。

safe-off资格取得后再接原只读快照和逐颗host解码；禁止用Android数据、人工值或另一颗芯片补齐。场景/ADSP路由/保护运行仍未解决，首次声音仍现场。传感器、GNSS、外屏与自动切屏未由本试验验证。已安装OS/Data/OS370与回退链不推广，无commit/push。
