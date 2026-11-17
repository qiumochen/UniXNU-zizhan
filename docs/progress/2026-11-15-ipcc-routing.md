# SLPI 前置：可选共用 IPCC 接收器

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交工作树。当前队列见 [STATUS](STATUS.md)；本批建立通信资源共用前置，不包含 SLPI 启动或传感器成功声明。

## 问题与交付

本机 live DT 的 IPCC 位于 `0x0ed18000+0x1000`，父中断为 GIC SPI229，即 INTID261。ADSP 使用 LPASS client3 的 signal0/2 与 AOP client0/signal0；SLPI 的 GLINK/SMP2P 使用 client4/signal0/2。原 `PongAdsp.cpp` 直接注册 INTID261，并清除收到的所有 IPCC doorbell；`ArmCpu::armcpu_spi_attach` 只允许一名 owner。直接再注册 SLPI receiver 会冲突，简单共享 GIC callback 也不能恢复已被第一个 callback 清除的 doorbell。

新增 `ZizhanIpcc.cpp` 作为**显式启用**的单一 IPCC receiver。`zizhan_ipcc_route=1` 才选择它；默认值零/未提供继续使用原 ADSP 路径。接收器验证 ADT 的精确基址、4KiB span 和INTID261，一个接收循环最多取32条消息；每条先clear再向对应的唯一 callback分发。未知消息和用满单次预算分别计数，不把缺callback的消息当成功送达。

订阅表为固定容量、追加发布、启动生命周期有效：有界writer门控制注册，callback/context发布后不撤销或复用，IRQ不等待、不分配；同ID/同callback/context可幂等调用，不同owner冲突会拒绝。首个callback必须在GIC启用前发布，避免启用瞬间的pending消息被当成unknown清除。缺ADT、映射失败或已有GIC owner均拒绝；GIC尚未就绪可由调用者有限重试。

ADSP可选分支只订阅自己的三个doorbell。新路径错误后不回落到第二个direct GIC receiver；原启动/认证/固件/电池客户端不在本批重构。统计`native_adsp_stats[2]`在新路径表示routed doorbell callback次数，旧路径仍为IRQ entry次数，不能混用作性能对比。

这套receiver本身不启动DSP、修改PAS、供电/clock、GLINK或SMEM内容。MMIO动作仅在显式启用并注册时配置已知接收id及处理收到的通知；不是只读探针。首次设备资格必须独立于SLPI启动执行，以确认现有ADSP电池链和返回路径没有退步。

## 主机验证与修正

`ipcc-router.py`在ASan/UBSan下回放实际production router、实际owner和实际ADSP分支；只移除主机不能执行的ARM barriers并替换硬件/内核服务：

- callback/context并发发布、重复/冲突、容量和无效输入；乱序ADSP/SLPI消息、unknown、clear-before-deliver、空FIFO及32条预算。
- 11项实际owner控制：默认关、未知mode、非法ID/空callback、坏metadata/宽度、map失败、GIC独占、GIC-not-ready重试、两客户端路由和启用瞬间首事件。
- 5项实际ADSP控制：legacy、可选路由、busy/not-ready后成功、owner冲突、241次/60秒有限等待后失败，均检查不出现双receiver。
- 实际ADT encoder输出及错INTID（把SPI229误当INTID）等负控制。

首版源码和双配置build通过后，代码复核识别首事件窗口并修正。旧host/build结果保留为首版，最终以带首事件控制的v2为准；不沿用旧source哈希证明修正版。v2精简和完整默认GPU/Wi-Fi内核均`KERNEL_DONE`，source hashes已重验一致：精简17847952 bytes/SHA-256 `2c014a2c43d13358aa0b2ed0b481ab9869d25cf171c3c1d1dce09162f2d90b08`；完整44469048 bytes/SHA-256 `6fe285d866666e9ee7236c336fe9f65cf2dabd59fe1c246427cd12243cbfeec6`。包含新用例的日用host runner通过。

准备设备回归时发现公开`app-preflight.py`还保留35°C拒绝条件，已按用户此前要求移除；温度值继续留报告，不改变身份、备份、extent、whole-OS或自动返回要求。

## 当前范围

route0控制轮已实机完成：同版新kernel、现有OS/UI/helper、ADSP2/audio0、共享路由关闭。ADSP到stage13，电池服务实际发布并持续34行更新，无panic/DSPfatal/audio启动日志；196秒回fastboot、回收1387192B新鲜deadline klog、Android product/boot_completed witness通过，返回温度33.1°C。它是当前新kernel的ADSP控制资格，不是新receiver通过。route1需要新鲜epoch并重验身份后单独执行。

随后route1以新鲜epoch重包并重新整OS/manifest/extent/backup核验，01:58:32临时启动。新鲜日志确认`ZIZHAN_IPCC: PASS receiver-attached spi=261`及ADSP新分支；其中日志的`spi=261`表示GIC INTID261，Linux相对SPI为229。ADSP到stage13、真实电池发布并持续31行更新，无panic/DSPfatal/audio启动；197秒返回fastboot，1487256B新鲜deadline klog和Android完成见证通过，02:02:22返回温35.7°C。两轮同kernel/loader/OS/UI/helper，音频均明确关闭；没有安装新OS或修改离线Data。

结论仅为 `DEVICE_VERIFIED_SCOPED_ADSP_COMPATIBILITY`：共用receiver挂接和现有电池链兼容，默认仍关闭。这版设备日志没有输出`zizhan_ipcc_stats`逐客户端计数，ADSP GLINK仍有100ms polling，因此不能用电池更新推断真实IRQ送达、无丢通知或SLPI端事件已通。下一最小增量为线程侧有限的实际IPCC统计快照，观察非零received/delivered、unknown/budget和ADSP callback变化后再接SLPI；不重复相同轮次以补不存在的统计。

SLPI尚未认证/启动、未注册客户端或得到传感器数据。后台下一轮仍以新鲜preflight和单一owner为前置，不同时引入SLPI、声音或外屏。

## 第四批：补足中断计数的有限观察

沿已限定通过的route1实现，给不可复用的subscription slot增加原子callback完成计数。在ADSP worker的`service()`之后读取软件计数，最多8次、相邻至少10秒；IRQ只更新计数，不打印、不等待或分配。每条含IRQ entry、received、delivered、unknown、32条预算耗尽次数、subscriptions/retries、ADSP callback数，以及AOP0/ADSP0/ADSP2/SLPI0/SLPI2分别的callback数。各字段单独原子读取，不能要求一次采样中各字段之和瞬时相等；这不是全硬件同时锁存。

新增host snapshot控制覆盖route关闭/未attach时零日志、正确已投递值、时间倒退/10秒边界/8条限额，以及观察函数不增加map/enable动作。生产router并发发布/每slot计数、12项owner控制、5项ADSP接线、完整日用回归通过。新增`ipcc-review.py`只审新鲜deadline日志中的真实统计；5项合成负控制确保缺数据、无progress、全零、unknown/budget、bad time/序号和真实fatal不判通过，且log-only结果不自称设备返回/SLPI或lossless资格。

此版精简17847952B/SHA-256 `f35b683b1905ede981cd986b6df2f4b8f767c1945b673ac1863caf2293478928`，完整44469184B/SHA-256 `f83d52689fb316983671bd63568d126335d269e9c0e4b2a9673122254413919a`，均已成功构建；当前runtime源哈希与build receipt一致。新stats包887B、ADSP2/audio0/route1、210/150自动返回；源和包有新身份，不重复旧无统计包。设备结果见下方已终结的第四批实机记录。

### 第四批实机结果

`ipcc-1115-stats-v3`在02:16:59临时启动，199秒回到fastboot，收回1490141B新鲜deadline klog，02:20:51 Android完成见证通过，返回34.7°C。supervisor在02:21:06完成PASS。整OS/manifest/extent/备份仍在启动前独立核验，未安装OS，音频明确关闭。

8条实际快照覆盖单调时间24.652–95.108秒，各间隔至少10秒。首条IRQ10/received11/delivered11/ADSP callbacks11；末条IRQ441/received477/delivered477/ADSP callbacks477，per-id末值AOP0=3、ADSP GLINK=469、ADSP SMP2P=5、SLPI两路均0。unknown、budget、retries全程0，subscriptions=3；ADSP到stage13、真实电池服务发布并继续更新，无panic、DSPfatal或音频启动。

日志审阅器结果`PASS_SCOPED_OBSERVED_IRQ_DELIVERY`，独立supervisor与Android见证补齐返回条件。至此**实际观测到的ADSP/AOP通知IRQ→共用receiver→对应callback链**有设备证据；不是仅由GLINK polling推断。8个采样窗口不证明全运行时无丢消息，也不覆盖SLPI端、真正睡眠或长期稳定；默认route仍关闭。无需为这一已完成的观察重复同包，下一增量进入SLPI独立启动/GLINK握手。
