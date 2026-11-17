# SE15 GENI 时序与 FIFO 准备

源码基点为 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。上一批已经实机通过clock/pin设置，本批增加显式`zizhan_se15_prepare=2`的时钟选择、时序与FIFO准备。模式1保留原clock/pin范围；默认仍关闭。本批不释放reset、不发I2C、不读取功放，当前任务见[STATUS](STATUS.md)。

## 真实输入与实现

保存的本机DT中，GCC的`bi_tcxo`指向waipio RPMh clock0，`xo_board`为76.8MHz；固定匹配RPMh实现的divider4得到19.2MHz。GENI根时钟地址为`cmd_rcgr=0x2e014`；固定BSP [cb7a356ae138e77992ae199eebb86559d825a673的clk-rcg2.c](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/cb7a356ae138e77992ae199eebb86559d825a673/drivers/clk/qcom/clk-rcg2.c)确认DFS enable在+0x14，八个perf配置在+0x1c起。source0为XO、mode0且HID0/1为不分频。

新`ZizhanGeniTiming.h`读取root/current/DFS状态；root正在update则SKIP。DFS开启时只选实际表中的XO不分频项；未开启时只接受当前root已经匹配XO，不假造档位，不写GCC/RCG/PLL去强配。找不到合法项时零SE写入退出。上轮原始`cfg=0x100000`本身不等于速率证明，必须结合父时钟与状态分支核对。

随后检查实际I2C固件、FIFO接口、无M/S active、无待处理IRQ/RX数据，以及TX/RX FIFO宽度32位、深度至少2。全部通过才采用[固定BSP i2c-msm-geni.c](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/cb7a356ae138e77992ae199eebb86559d825a673/drivers/i2c/busses/i2c-msm-geni.c)的nominal100kHz参数：divider7/high10/low11/cycle26。这里只认配置和回读，不将其称为已测量的电气总线频率。

通用`GeniI2c.c`抽出原FIFO准备实现，新增`geni_i2c_prepare_poll`：保留原packing/状态处理，但屏蔽SE外部四条IRQ路由，供后续owner polling，不安装GIC handler。原`geni_i2c_prepare`的中断路由语义保持。新代码回读时钟选择、时序、FIFO mode、4-byte packing、byte granularity、watermark和IRQ配置；失败保留并明确FAIL，不假装总线已准备成功。

SE只写准备寄存器，禁止命令/控制/FIFO数据、output-control、force-default、OTP或功放值写。共用GPIO1保持输入低，reset/供电/控制器后续事务owner仍需分别资格；配置成功也不调用身份核心。

## 验证与设备安排

实际GENI核心与新timing wrapper已通过ASan/UBSan：current/DFS分支、八个档位、root busy、无XO、protocol/active/IRQ/FIFO负控制、寄存器写拒绝/readback失败及原中断prepare。实际平台回放从14扩到17项，新增timing成功、clock选择失败与FIFO错误，逐字节限制可写寄存器，禁止任何I2C命令/reset写。人工FIFO/clock数据仅是控制夹具。

日用host与新双kernel通过后，准备唯一模式2设备轮次。控制为上一批模式1同OS/UI/helper/ADSP2/audio0/route1；新增变量是合法时钟选择及GENI配置。验收分别记录clock选择、timing/FIFO回读、外部IRQ masked、reset未变/零I2C，再独立确认自动返回、新鲜deadline、ADSP/电池和Android完成。若不能选择XO或FIFO前置不符，保留SKIP/FAIL，不把整轮回返升级为GENI成功，不原样重复。

仍只用后部USB-A和temporary boot，fresh安装/extent/备份、210/150秒返回、30分钟supervisor；不安装OS/固件、不离线改Data。实际构建、准备与设备结果在本批终态分别补记。首声/外屏/自动切屏和现场GNSS权限边界保持。

本批日用host与新双kernel已通过；源码哈希与最终receipt一致。精简kernel SHA-256为`80eb2ddfa93c1a6ac8a0a03cd3481f4a8463cc41a8e6319c087fab1a1bb2ad35`，完整为`cd722137e2b2827fae946ac8aab8cf12a7c3235f09136d763ee003b8f47d793a`。909B候选只将prepare模式1改为2并采用新kernel；启动前实时Android完成/30.0°C。

## 实机配置结果

`se15-timing-1115-v1`的fresh全OS/manifest/extent/backup preflight通过，04:43:44实际临时启动，196秒后返回bootloader。1389021B新鲜deadline klog，04:47:34独立Android/zizhan/boot_completed=1、34.0°C，supervisor于04:47:47终结PASS。ADSP13、真实电池源发布后30条更新，无ADSP failed/fatal、panic或PongAudio启动。

实际clock-select为`root=0 cfg=100000 dfs=21 level=0 selected=1 result=0`：DFS enable确实为1，第0档选到XO不分频。原clock/pin路径正常，新GENI结果为：

```text
PASS timing-fifo result=0 tx=201029e4 rx=20104800
clk=71 scl=a02c1a route=0 retained=1 reset_unchanged=1
i2c_commands=0 amp_identity=UNTESTED finished=1
```

两FIFO参数解码宽度32位/深度16，clk/timing和FIFO packing/mode等全部经过生产回读，SE外部IRQ route为0。只认`PASS_SCOPED_GENI_TIMING_FIFO_CONFIGURATION`，不认已测电气频率、总线事务、功放供电或身份；GPIO1原输入低状态未变。没有第二轮相同输入重跑。

下一交付为共用GPIO1的owner与安全释放、功放真实供电前置，取得具体板级依据后才调用已有有界身份核心。不能仅把四个verified标志填满越过缺口；必要时保留具体阻塞并独立推进GNSS/provider或其他传感器输入。原OS/UI/helper及OS370/Data/回退链保留，本轮未装OS/固件或离线改Data；raw日志、review和source/build/package身份留仓外，无commit/push。
