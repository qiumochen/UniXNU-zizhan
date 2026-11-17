# CS35L41 共用复位与数字启动身份

源码基点为 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。本批在已通过的[GENI配置](2026-11-15-geni-timing.md)之后，增加显式`zizhan_se15_prepare=3`：两颗功放共用复位、只读OTP启动状态和ID/版本，完成后重置两颗。默认关闭，不启用声音。当前任务见[STATUS](STATUS.md)。

## 本机依据与供电边界

Android本轮只读确认`2-0040`和`2-0042`绑定实际`cs35l41_dlkm`模块；同地址的`0-0042`属于另一总线的FSA4480，不混作功放。两功放供应者均为`regulator-dummy`。新鲜Android启动日志明确VA/VP供应缺失、采用dummy，以及两颗成功识别`35a40/B2`；第二颗记录`Reset line busy, assuming shared reset`。这些是Android证据，不能继承为iPadOS识别成功。

原始模块只读保存于仓外并记录SHA-256。`cs35l41_probe`反汇编及调用重定位确认：获取两项regulator后enable，reset使用`devm_gpiod_get_optional`、GPIOD_OUT_LOW参数3，等待2000–2100微秒，再输出1并等待2000–2100微秒；第二颗可接受EBUSY共用reset。保存live DT的GPIO1直接reset消费者为这两颗，pin配置为GPIO、bias-disable、16mA。debugfs GPIO/供电摘要在本机不存在，读取失败保留；没有挂载或更改Android驱动。

dummy供应不构成电压或功率证明，也没有可由驱动开启的真实VA/VP控制器。本批把“供电绑定已核对为不可控”与“数字启动实际响应”分开：没有填`PowerVerified`标志；必须在本次iPadOS的I2C读取中看到每颗OTP_BOOT_DONE、无OTP_BOOT_ERR，再读取身份。数字启动证明不扩为模拟电压、功放输出或调音资格。

## 实现与失败边界

`ZizhanAmpReset.h`只接收GPIO1页，拒绝未知mux/输出/EGPIO所有者，要求既有GPIO输入低。先预置data低、配置16mA输出低，等待2100微秒并核读，再拉高、等待并核读。保留高位，不碰GPIO89或IRQ。对应实际板级共用owner，不套用TFA主动高复位。

模式3在既有clock/pin/timing检查通过后，独占这一启动worker的SE15及两颗功放，调用新`zizhan_cs35l41_boot_identify`，只允许读`0x1001c/0x10018`（OTP完成/错误）、DEVID0和REVID4。每颗OTP轮询最多100ms/64次，单次I2C仍有20/2/2ms事务/取消/中止上限。OTP未完成、错误、总线异常、未知ID或第二颗失败均停止，不重试整轮。没有解锁OTP key、下载DSP或写codec寄存器值。

无论reset/OTP/身份结果如何，取得GPIO1后都重新拉低并核读，保持该输出至reset；总线异常时保留uncertain状态，不把软件Idle当硬件总线已释放。声音和ADSP音频图始终关闭。原模式1/2不改变reset或发I2C，新增行为只由模式3启用。

## 验证及设备设计

生产reset core的所有者/引脚/脉冲/回读失败及最终assert、生产身份+GENI的真实只读编码/OTP完成和错误/时间与次数上限/部分成功、以及18项实际平台控制均通过ASan/UBSan。平台还验证reset释放失败时禁止后续I2C且执行最终assert。夹具ID和状态不作硬件证据。

新日用host和精简/完整内核通过后，唯一模式3轮次才可启动。控制为上批模式2，同OS/UI/helper/ADSP2/audio0/route1；变量为板级reset及OTP/ID读取。验收分别列reset读回、每颗OTP/真实ID/版本和最终reset asserted，再独立验新鲜deadline、无fatal、原ADSP/电池及Android返回。ID缺失或NACK是保留的FAIL，不用相同输入反复尝试，也不开启音频服务弥补。

保持后部USB-A、temporary boot、fresh完整身份/备份/extent、210/150秒返回与30分钟监督，不安装OS/固件或离线改Data。首次扬声器出声仍需人在场；本批只是数字启动与只读身份资格。实际build/prepared/device结果在终态分开补记。

本批日用host与新双kernel均已通过，最终运行时source hashes与receipt一致。精简kernel SHA-256为`66e3677b4e92f2aab963862befdd5ee3b22c73b34ee1d8eb01acdc2ae623656e`，完整为`6a68bf600513e532d206bc3df50d910d91bc8c0696378a396e7c5272ccf18916`。909B模式3候选已准备，启动前实际Android完成/29.8°C。

## iPadOS 实机数字启动与身份结果

`amp-boot-1115-v1`经fresh全OS/manifest/extent/backup preflight，05:12:11实际临时启动，196秒后返回bootloader；1484921B新鲜deadline klog。05:16:02独立Android/zizhan/boot_completed=1、33.5°C，supervisor于05:16:17终结PASS。ADSP13、电池源发布后33条真实更新，无ADSP failed/fatal、panic或PongAudio启动。

本次iPadOS实际结果为：

```text
reset result=0 claimed=1 released=1
PASS otp-identity reason=0 digital_ready=2 completed=2
id=35a40/35a40 rev=b2/b2 otp=3/3 error=20000000/20000000
bus_error=0 irq=0 uncertain=0 reset_asserted=1
codec_value_writes=0 audio_output=0 finished=1
```

两颗分别完成OTP→错误状态→ID/版本读取，不借用Android缓存或模拟服务。OTP错误寄存器非零`0x20000000`原值保留；本次判断只检查已定义的OTP_BOOT_ERR bit31未置位，不能把整寄存器描述为零。最终GPIO1输出低回读通过，结束后两颗均留在reset，不存在本轮播放服务。

限定结论为`PASS_SCOPED_CS35L41_DIGITAL_BOOT_AND_IDENTITY`：本机CS35L41 B2两颗的复位/数字启动/I2C只读身份通过；不包括模拟电压测量、扬声器/麦克风、升压、DSP保护/调音、ADSP播放图或音质。此结果解除“还没读到功放ID”的前置，不能作为首次声音的替代验收。

下一交付是原机speaker-protection固件与T/B参数的私有manifest，以及默认关闭的codec/ADSP播放候选：先核对本机真实初始化/保护输入，再独立推进，不重复本轮已通过身份读取。第一次实际输出仍需人在场，不能仅因ID成功而启用输出。

仅temporary boot，无OS/固件安装、Data离线修改或commit/push；原OS/UI/helper、OS370/全部回退链保持。真实模块、raw日志与哈希、review、source/build/package身份均留仓外。
