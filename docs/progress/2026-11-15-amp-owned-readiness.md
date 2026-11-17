# GENI已知屏蔽状态的独立owned-readiness候选

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。依据[mode9实际status/enable/source配置](2026-11-15-amp-irq-config.md)，新增默认关闭模式10。交付范围是主机可测试候选，未启动手机，不代表OTP或声音通过。当前队列见[STATUS](STATUS.md)。

## 明确的窄条件

旧模式1–9和严格raw IRQ检查保留。新API要求successful same-owner safe-off、两颗禁止输出/boost/HALO的完整读回和写确认一致。每个24项有序配置样本必须匹配本机实际固件303/303、phaseidle/SE0、TX/RXempty、无slave及DMA TX/RX来源、route0/DMA mode0/GSI0、已测packing/阈值15/16与使能/接收配置。master只允许0或80，并且bit7在master enable中关闭，全部enabled-pending为0。

初始只有已测slave RX-LAST时可确认一次slave来源，绝不清master80。最多500us/26样本，要求两次间隔20us完整配置完全一致且来源均0；未知IRQ/error、使能位开启、固件/模式/来源/数据/配置变化全部拒绝。原始IRQ80、enable和masked分类标志保留，不把80变成0，不伪造前置。进入实际快照identity时再次核对相同配置，输出/boost/OTP读取边界与有限事务保持。

这个窄条件候选取代“任何raw IRQ非零都忙”的新模式前置；依据只覆盖实际已测mask与零来源组合，不宣称masked bit一般无害，不改变旧模式。没有key/OTP编程/trim/DSP/output-enable操作，最终共享reset由原平台保证。

## 主机验收

生产GENI测试覆盖raw80及raw0成功链、IRQenable bit7开启、21项固件/模式/路由/IRQ/DMA/FIFO/阈值/packing配置差异拒绝、未知master位、软件phase忙、缺失safe-off证据、uncertain、样本变化与冻结时钟持续来源；确认没有master W1C、最多一次slave来源确认、原始entry_irq仍为80。原identity/snapshot/safe-off及旧交接控制保持，实际平台25项控制通过。早期fixture把idle状态模型错误套用到正常I2C completion已修正，失败日志保留；不将人工数据当实机OTP。

完整日用host和最终核心/平台控制通过；最终v2精简与完整内核均通过，运行时31项输入hash与receipt重新核对一致。v1双构建也通过但早于最终after-config采样位置修正，只保留历史，资格认v2。没有打包、安装或临时启动；当前是HOST_VERIFIED source候选。当前无人值守心跳原期限08:50:36，剩余时间不足原30分钟监督流程的完整返回预算，因此本批不派phone任务。后续只有重新核对fresh live/manifest/wholeOS/extent/backup/owner与足够监督期限才允许准备并运行独立mode10试验。

## 未完成与下一动作

实际masked-readiness、逐颗OTP_ID/32word、OTP host解码和保护初始化均未在设备上验证。候选通过主机只证明预定守卫和事务范围，不提供硬件readiness事实；输出仍默认关闭。下一步为充分监督期限下的独立临时轮，分别验readiness原值/proof、真实OTP/禁止状态和finalreset/Android返回；任一失败保留具体原因，不补默认数据。首次声音、外屏与自动切屏仍现场，无commit/push或OS/Data推广。

## 新两小时窗口中的独立实机资格（09:06收口）

用户新增两小时监督后，候选已独立打包并按fresh全OS/extent/manifest/backup/单owner/后部USB-A运行。09:02:38临时启动，197秒返回fastboot，1387566字节新鲜deadline klog；ADSP13/真实电池更新/无fatal、panic或音频，Android完成返回通过。09:07:33独立观察Android/zizhan/boot_completed=1、34.2°C。

**限定owned-readiness、逐芯片OTP快照DEVICE_VERIFIED，真实数据host解码通过。** readiness实际3样本/一次slave确认、rawIRQ80/enable40007e3f/enabledpending0/maskedflag1，真实identity入口保留irq80/busy_sources4并通过窄条件，未伪造IRQ0。原严格模式未改。

两颗各OTP_ID8、32个独立word完整读到；前后状态均PWR1=0、PWR2=3300、HALO100，输出禁止/boost禁止/DSP停止各为1。每颗按原机实际表得到98项host易失trim更新，含保留字段的位流规则匹配；原始OTP与实际值计划仅仓外。没有在芯片上执行trim，`otp_unpacked_on_device=false`、`otp_programmed=false`、`plan_executable=false`，不能当完整初始化或播放通过。

本轮仅原safe-off4次清零、一次slave确认及只读数据，最终共享resetasserted，无bus error/uncertain/key/OTP/trim/output-enable操作。独立审阅 `PASS_SCOPED_DEVICE_OWNED_READINESS_OTP_AND_HOST_DECODE` 与主机receipt分开留证，已装OS/Data/OS370/回退链不推广，无commit/push。

下一音频增量可使用这两颗实际数据准备默认关闭的B2补丁/易失trim执行和readback/relock候选；钥匙字宽/顺序保持原机差异，任何失败保留owner并reset，不碰OTP编程或输出。当前并行工作顺序先完成独立Hall只读实机资格，防止只在音频前置循环。
