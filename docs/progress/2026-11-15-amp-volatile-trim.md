# CS35L41 实际OTP到易失trim的受限写入候选

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。接续[实机B2固定补丁通过](2026-11-15-amp-b2-patch.md)，本批目标是每颗本轮真实OTP到允许易失位域的初始化、读回与关闭ACK，不编程OTP或启声音。当前队列见[STATUS](STATUS.md)。

## 输入与独立模式12

只支持本机实际OTP_ID8。公开源码仅寄存器/shift/width描述：原机100项表的前99项与固定Linux v6.6 otp_map_1寄存器事实一致，最后为reg0/bits0保留终止；不包含任何本机校准值。运行时每颗从自己的32word bit80开始，消耗688位到768，跳过保留目的得到98项互不覆盖更新。未知表号、不完整快照、错身份、字段错误拒绝，不用Android或host值代替。

必须本轮owned snapshot/两颗35a40B2/输出boostHALO禁止，以及固定B2补丁全部20ACK/两颗关闭ACK和禁止状态读回通过；入口再核实际GENI配置及每颗状态。仅known trim目的可写，OTP存储0x400等、钥匙之外的控制、电源/AMP/HALO enable不在trim写集合。每字段先读原值、masked保留其他位、值变化才写、随后完整32位精确读回；不会盲覆盖整寄存器。

采用原机trim字节钥匙55/aa→cc/33，与B2的5555/aaaa→cccc/3333分开。每颗98读回、最多98 trim写与4钥匙写，总最多204值写，字段循环5秒检查/固定计数，事务沿用20/2/2ms有限处理。确定总线且读回失败时有限关闭；bus error/timeout uncertain不再派清理，原平台最终共享reset。关闭ACK记录独立，不称硬件锁已读回。

所有原模式1–11保持范围，不加载保护RAM/固件、ADSP路由、校准程序或音频输出。成功也只是易失初始化资格，复位后不保留trim；不写OTP/MTP。

## 主机验证与试验

实际生产GENI模型执行safe-off→owned real-shape snapshot→fixedB2→trim完整链，使用分颗测试位流、共享寄存器多字段保留、钥匙阶段和字宽、每字段完整读回与最终禁止状态；逐一在所有trim事务位置注入bus失败，禁止重试/uncertain后操作。覆盖未知OTPID、缺B2关闭证据、active状态、readback错后关闭、冻结时钟超时。首个测试因人工fixture仍混用ID1而正确被拒，改成当前明确支持的ID8后通过，原失败留证。

生产C解码器与独立Python bitstream模型对40组32word输入逐项比较98个reg/mask/value；actual-module到runtime B2表一致性和原有测试保持。实际平台27项控制通过。完整日用host与新reduced/full kernel收口后才包独立设备轮。

设备验收要求readiness/snapshot/B2各PASS，trim completed2/readbacks98每颗/closeACK2每颗/key attempts8、trim变化写<=196且尝试确认一致、最终statecount3每颗与禁止状态相同/finalreset，零OTPprogram/outputenable/firmware/audio；fresh wholeOS/extent/manifest/backup/单owner后部USB-A/deadline klog/ADSP13/电池/无fatal音频/独立Android完成与30分钟监督仍必须满足。任何partial不算trim通过，不改失败为“部分可用”。

## 当前边界

定向host已通过；完整构建/候选/实机按实际收口。保护固件RAM加载、运行时算法/保护状态及ADSP播放链仍待实现，首次真实声音现场。没有commit/push或OS/Data推广，原输入与回退保留。

## 本轮实际易失初始化通过（10:29）

独立mode12在fresh全量前置下完成：实际trim PASS/completed2/readbacks98/98，143个值变化的易失位域写+8钥匙写=151attempt/151ack，closeACK2/2、每颗poststate3读回均PWR1=0/PWR2=3300/HALO100、finalresetasserted。无buserror/uncertain/OTPprogram/output-enable/DSP start/audio。未变化字段跳过写但仍精确读回，不能把143写误当只验证143字段；总196字段已验。

本轮readiness、本芯片OTPID8/32words和固定B2补丁也各通过。10:25:04临时启动、196秒返回，1379745字节新鲜deadline、ADSP13/真实电池更新/无fatal音频/Android完成通过，10:29:41独立Android/zizhan/boot_completed1、35.9°C。独立审阅PASS_SCOPED_DEVICE_VOLATILE_TRIM_READBACK，主机算法证明与实际读回分别保留。钥匙ACK仍不是硬件锁读回；最终复位后易失设置不作为已安装状态。

下一音频最小交付为保护固件RAM加载的受限执行/完整回读候选：沿用已有原机保护WMFW与T/B系数私有来源/hash及HALO窗口计划，不启动HALO、不启输出。先实现有限传输/窗口/offset/别名/段hash与读回验证，再runtime算法ID/base实际核对后才系数；静态计划不能替代芯片运行时身份。剩余监督时间只允许有完整返回预算的设备轮，若不足先做主机候选。保护固件执行、ADSP路由与首次声音仍未通过。
