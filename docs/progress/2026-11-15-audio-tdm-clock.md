# 本机TDM端点的无输出时钟资格

源码基点`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`加未提交修改。接续[统一无声执行器与PLL实际状态](2026-11-15-audio-silent-owner.md#mode8实机两颗均未观测到pll锁定)。任务AUDIO-TDM-01，最小交付为真实TDM端点生命周期及功放PLL锁定观测，不含播放流或首声。当前队列见[STATUS](STATUS.md)。

## 阻塞与准确输入

mode7正确加载/参数、PLL配置读回与核心启停均成立，但100个样本没有心跳。独立mode8两颗64个raw样本均为PLL未锁定状态，`raw3=20000010`。因此内部PRM时钟请求完成不能替代端点产生外部串行时钟的资格，继续原样启动功放核心无意义。

实际Android AGM服务的打开文件明确选择Forte标定库与对应delta。只读保存原机libagm、GSL、GPR及所选标定文件，逐远端/本机SHA256匹配，全部留仓外。原机libagm的TDM配置从`gsl_get_tagged_data`取得，再用硬件声明补充lpaif/interface、group slot mask，不能只读DT或照Pong I2S推定。

新增离线`cs35l41-tdm-inputs.py`按实际ACDB关系解析：TMLU/TMDE绑定RX标签与TDM模块；MTKT/MTLU/MTDE/MTDO/POOL绑定通道标签配置；CSLU/CDLU/CDDE/CDDO/POOL绑定同一实例的格式、frame、lane与clock参数。边界/长度/重复/模糊映射均拒绝。实际基表得到唯一24-bit/channel3 RX候选，结构控制与原机库取值流程交叉核对；delta保留，不把这一离线基表核对提升为完整运行图资格。

- TDM sink模块`0700100e`，接口参数`0800101b`：LPAIF0/tertiary2、内部同步、data OE1、slot mask7、4槽×32位、short sync0、normal0、delay1、reserved0。
- 硬件格式`08001017`：96kHz、24位、3通道、fixed-point1；frame factor1。
- lane参数`0800106a`：mask2、reserved0，对应第二条数据线；此诊断中data pads仍保持GPIO输入。
- clock参数`0800113c`：自动ID/自动频率、attribute4/root0；原机kernel推导12.288MHz与4槽×32位一致。

公开协议固定来源：[Graph Manager](https://github.com/AudioReach/audioreach-graphmgr/tree/41119f8a40e958b923bc93b9ae8f4a1ef3eb4080)、[Graph Services](https://github.com/AudioReach/audioreach-graphservices/tree/5e21b5c9e197bdf45fb20fcb2690366c2a66e541)。这些版本提供协议与取值流程；本机取值由实际文件验证，不从较新上游推断本机实现。原机ACDB、模块及反汇编留仓外。

## 独立默认关闭mode9

同一owner先取得实际APM/core/clock ACK，并只读确认121/123/122/124均为GPIO输入。随后仅创建TDM hardware sink graph：OPEN→CFG→PREPARE→START，真实成功后同步运行原mode8双PLL采集，不启HALO、不创建stream/shared-memory/DMA或样本发生器。仅SCK121由已验组合执行器暂接；WS/data仍隔离，GLOBAL/AMP/BST/ASP输出不启用。

SG4105/container5105/module6281、GPR源端口2102及独立令牌是客户端分配的标识，处于公开范围，不是Pong板级资源。方向RX2、global-device位置、一个sink输入端口，无模块连接。模块参数size精确记录body长度，packet padding独立对齐，不把填充计入TDM28字节或硬件格式12字节。

退出顺序：功放resetassert/SCK恢复→graph STOP/CLOSE分别真实ACK→本轮clock/core释放。任一图命令拒绝、超时或通道关闭，都保留opened/running/uncertain与本轮租约直到平台自动返回，不推定图已释放。严格检查peer/domain/port/token/opcode/payload/status，不假造完成消息。每图命令2秒、启动最多800迭代、关闭400迭代；原同步持有60秒/worker90秒与设备30分钟监督保持。

验收分开记录：图六个真实ACK与关闭、双PLL原始采集完整、实际锁定位次数、功放及租约清理。即使图通过，锁定位仍0也不得提升为时钟或心跳通过。首声前仍需实际保护程序运行及完整播放链；本轮不听测。

## 验证状态

```sh
python3 tools/zizhan/tests/cs35l41-tdm-inputs.py
python3 tools/zizhan/tests/cs35l41-tdm.py
python3 tools/zizhan/tests/cs35l41-prm-platform.py
python3 tools/zizhan/mac/cs35l41-tdm-inputs.py --acdb "$PRIVATE_ACDB" --output "$PRIVATE_REPORT"
python3 tools/zizhan/mac/check-daily-use-host.py
```

实际基表解析、12项输入结构控制、TDM28字节独立golden/五参数size和六命令成功/失败控制、实际PRM平台44项均通过。完整日用与新reduced/full构建均通过，46项source hash一致；906B三私有ADT临时候选已备，尚未上机。临时轮继续fresh全量OS/manifest/extents/备份、单owner/后部USB-A、自动/panic/power-key返回、新鲜pinned deadline与Android完成见证；温度仅记录，不改已装OS/Data，不commit/push。

## mode9实机：TDM图生命周期通过，双PLL仍未锁定

实际OPEN/CFG/PREPARE/START/STOP/CLOSE六份应答均status0；准确TDM模块和参数被DSP接受，opened0/running0/uncertain0收尾。四pad初始GPIO输入隔离，只有SCK暂接。相同双PLL64采样仍各lock0/raw3固定20000010；不提升为物理参考时钟或HALO执行通过。reset/SCK恢复、图关闭、clock/core释放顺序与ACK5全部通过。200秒返回/1,413,229B新鲜deadline/ADSP13/电池/无fatal/独立Android完成通过，33.6°C。

下一新原机输入是实际`cs35l41_dai_set_sysclk`的高频分支：频率大于6.144MHz时，写`2d10=24010`后才返回。此前只做component PLL步骤，没有做DAI频率监测配置；不能归为已完成的启动链。下一独立mode10仅增加这个准确寄存器配置及读回，保留mode9端点和无核心PLL采集；同时只读记录SCK pad256次高低/变化与CLOCK_DETECT_1原始值。pad采样不测量精确频率，也不把零变化单独当无时钟证明；不改PLL频率或来源、不启GLOBAL/AMP/BST/核心。

mode10原机频率监测执行器的生产核心（准确24010、错误读回中止、无核心/IRQ写）、PRM平台45与SE15平台41通过；完整日用及新双kernel均通过，source46hash一致，907B三私有ADT临时候选已备，尚未上机。旧mode9无锁定结果不改。

## mode10实机：SCK pad有活动，PLL仍未锁定

两颗原机频率监测`2d10`均由20016→24010、各一次完整读回通过，14次配置写ACK。SCK121只读256采样见135次高/137次变化，说明pad采样有活动，但不测量精确频率。两颗CLOCK_DETECT_1均0，PLL64样本仍无锁定/raw3=20000010。graph六步、resetpinrestore和clock/core清理通过；不能重复声称TDM端点缺失已解释全部失锁。

后续仍需核芯片内部运行门控：目前GLOBAL_EN始终0，不能将外部SCK活动等同于内部PLL已运行。原机GLOBAL单bit语义与AMP/BST/ASP独立使能需要明确守卫，任何仅内部运行候选必须先读回AMP/BST及ASP全部关闭，保持无核心/无数据输出并最终恢复GLOBAL0；未具备这些准确守卫前不派设备。

## 独立mode11：输出守卫下的内部GLOBAL门控候选

原机与固定公开定义中PWR1 GLOBALbit0、PWR2 AMPbit0/BSTbits4:5和SP_ENABLES4800相互独立。mode11仅在前后完整功放状态符合、AMP/BST关闭且ASP使能整个寄存器为0时，临时置内部GLOBALbit0并读回；64次PLL raw采样的每次状态检查均再次核对AMP/BST及ASP全0，核心保持100不启动。结束后清本轮GLOBAL并完整读回0再继续下一颗；失败保留阶段，外层共享resetassert后才恢复SCK/关图/释放租约。14配置写加两颗各on/off共18写上限，不引入任何AMP/BST/ASP使能或播放数据。

该候选不认定GLOBAL已经解释失锁，仅以新原机门控条件为单一变量；需要新host/双kernel/fresh独立设备轮。首声和核心执行仍未开始。

mode11生产核心输出守卫/GLOBAL读回/分阶段bus错误控制、PRM平台46、SE15平台41、完整日用与新reduced/full内核均通过，source46hash一致；907B三私有ADT临时候选已备。尚未上机，不能声称内部门控或PLL通过。

## mode11实机：输出关闭下双PLL锁定通过

每颗内部GLOBAL实际on1/off1，ASP全寄存器68次均为0，AMP/BST始终关闭、核心未启动。两颗64个PLL raw样本均读到LOCKbit1，失锁位清除；本机真实TDM/SCK/原DAI监测/nativePLL与内部运行门控在此范围内成立。最终GLOBAL0、共享reset/SCK恢复、graphSTOP/CLOSE、clock/core释放均通过。不能提升为精确外部频率测量、保护程序心跳或声音资格。

下一独立mode12保持同一已验时钟/内部门控，实际PLL锁定前置齐全后才原机rate/MPU/核心启动，严格原机MPU完整读回，不默认沿用GLOBAL0时的经验向量。每颗100个实际样本要求HALO2与持续心跳，始终AMP/BST/ASP0；先停核心、清GLOBAL0、reset恢复引脚，再关图释放租约。未知MPU/核心/心跳失败如实中止保留，不忽略差异。
