# 统一时钟租约与功放无声执行器

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。任务AUDIO-SILENT-01，接续[实际APM/内部时钟资格](2026-11-15-audio-prm.md#mode6内部时钟控制实机通过)。用户授权继续推进至首次出声前；输出启用与首声听测仍留现场。当前队列见[STATUS](STATUS.md)。

## 最小交付和验收

统一owner在同一线程取得真实APM、核心和12.288MHz时钟ACK，再同步完成本机SE15安全初始化、两颗实际OTP/B2/trim、完整固件与各自保护参数读回。输出关闭状态保持，随后仅连接GPIO121/SCK、按原机表设置PLL，检查两颗保护程序各100个实际样本、约一秒窗口内至少10次心跳变化和HALO_STATE2，并逐颗停止核心。

依赖释放顺序固定：先停HALO，assert共享GPIO1 reset，再恢复本轮SCK GPIO配置和输出锁存，最后释放本轮时钟及核心，各自要求独立真实完成ACK。只有功放资格、复位/引脚恢复与租约释放全部通过才认组合PASS。心跳失败但GPIO清理成功可正常释放本轮租约；未知复位/引脚状态或通道关闭不得继续释放依赖，保留held/uncertain直到平台自动复位。

此交付不启GLOBAL/AMP/BST/ASP输出、不创建PCM graph或DMA，不播放音频。成功只认保护程序无声运行及组合清理；不能替代播放数据链或听测。若失败，保留确切阶段和实际状态，下一增量按新证据选择，不重复不变试验或猜PLL源。

## 准确输入与实现

`zizhan_audio_prm=7`独立默认关闭；要求确切zizhan、ADSP2、audio0、无独立SE15 prepare/observer。旧模式1–6及prepare1–19保持。组合内部context20只有实际租约证明完整时才可同步调用，不能通过boot prepare启动，也不派第二个worker。缺私有固件/系数在本轮codec MMIO前跳过并返回真实未验状态。

PLL使用原机CS35L41模块12.288MHz→33表项和六个masked步骤，`2c04`逐步完整读回；只允许准确的mask/value和94次总写上限。原观测到的20项MPU向量仍逐项准确并在关闭钥匙后稳定，`mpu_rights_verified=0`保持。两颗GLOBAL/AMP/BST关闭状态在运行采样中逐次核对，任何变化中止。

SCK来自本机saved live DT与固定cape pinctrl：GPIO121、ALT1、6mA、bias-disable/output-high；只接受本轮已核GPIO输入配置1/低电平。连接先写输出锁存，再设置mux/OE；停止后先assert共享reset，再隔离为原GPIO输入，最后恢复锁存。WS与PCM data引脚不连接。原机寄存器表/DT/固件与模块反汇编、hash和组合构建收据留仓外，不发布私有输入。

控制命令仍每步2秒、实际通信预算8秒；同步持有期间没有协议命令在途，仅从通信预算排除实际测得、上限60秒的持有时间。完整worker90秒/9000迭代，RAM/系数原期限及新HALO5秒/每颗100样本上限均保留。全轮继续使用30分钟监督、后部USB-A、fresh全量OS/manifest/extents/备份/单owner、临时boot、自动/panic/power-key返回、pinned新鲜deadline与独立Android完成见证。温度仅记录。

## 主机验证与设备边界

```sh
python3 tools/zizhan/tests/cs35l41-run.py
python3 tools/zizhan/tests/cs35l41-setup.py
python3 tools/zizhan/tests/cs35l41-prm.py
python3 tools/zizhan/tests/cs35l41-prm-platform.py
python3 tools/zizhan/mac/check-daily-use-host.py
```

生产核心覆盖完整加载链、原机六步PLL序列、持续100样本、错误PLL读回、心跳停滞/错误HALO状态、超时和事务失败；平台41项覆盖旧模式、实际租约证明、缺输入、复位和单次调用；PRM平台37项覆盖同线程持有、功放失败但可清理、清理失败禁止释放、持有超时、通道关闭与冲突。SCK生产模板独立验证原机配置、所有权和恢复失败。

上述定向host、完整日用及新reduced/full内核均通过，45项runtime/source hash与构建收据一致；906字节命令行的独立临时候选已备，三份原机私有ADT输入准确匹配。尚未上机。主机合成心跳不是设备执行证据，设备结果待独立新轮。

## mode7实机：心跳FAIL，组合收尾限定通过

actualAPM/core/clockACK后，同轮真实RAM及保护参数完整通过，SCK121准确配置284/io2，chip0 PLL10→670六步完整读回。100个实际样本HALO0/心跳0，没有VIO/WDT错误；core100→101→100精准启停，47次写ACK，reason4中止，chip1未运行。此处不是保护程序执行通过。

同步持有28,208,785us后，共享resetassert/SCK恢复均成功，再取得独立clock/core释放ACK，sent5ACK5/held0clockheld0uncertain0，组合安全收尾通过。199秒返回、新鲜deadline及Android完成见证/ADSP13/真实电池/无fatal通过。原mode19与本轮无心跳分别保留；不将“请求时钟成功”和PLL配置回读提升为物理SCLK或锁定资格。

下一独立mode8只采PLL锁定/失锁前置：保持相同真实租约和安全加载，原机PLL步骤后读取两颗固定IRQ sticky/raw状态，有限64样本，不启HALO、不改IRQ或清状态。不重复原样核心运行；按实际锁定状态再选择时钟或DSP启动增量。无出声。

mode8生产核心锁定位0/2控制、实际SE15平台41项、PRM平台38项、完整日用及新reduced/full内核均通过，source45hash一致；906B三私有ADT临时候选已备，尚未上机。12个PLL写ACK仅配置，不替代锁定结果；两颗采集完整但锁定位0应报告为采集通过/锁定未取得。

## mode8实机：两颗均未观测到PLL锁定

两颗各64组sticky/raw采集完整、各六步PLL配置读回完成，12次PLL写ACK，零rate/MPU/core启动/IRQclear写。两颗IRQ raw组3在64次中始终为`20000010`，PLL_LOCK bit1为0、PLL_UNLOCK bit4为1；sticky组3均为`20000000`。因此没有锁定证据，不能认为正确PLL配置和内部clock ACK已经给功放送达有效串行参考时钟。其余原始四组first/last/OR/AND留仓外，未知位不命名或清除。

采集、reset/SCK恢复/clock和核心释放各自通过，199秒返回、1,413,014字节新鲜deadline、ADSP13/电池/无fatal及Android完成通过，35.2°C。采集通过与锁定未取得分别记录；不再无前置启动保护核心。下一具体增量是取得本机原机AGM TDM hardware endpoint协议，并在输出和PCM data隔离下建立真实端点时钟。当前只读提取原机libagm/GSL/GPR并逐远端/本机SHA匹配；不能复用Pong I2S模块或按通用端口猜配。

## 锁定参考后的独立mode12候选

mode11已取得两颗64/64实际PLL_LOCK，且ASP/AMP/BST关闭和GLOBALon/off清理通过。mode12保持这套真实时钟前置，GLOBAL运行期间先采原64样本锁定证据，再严格原机MPU全字读回，合法后才启HALO；不沿用GLOBAL0时的经验向量。双芯片各100样本/HALO2/至少10次心跳变化、VIO/WDT0和core实际停止是独立验收。

运行期间逐次保持AMP/BST关闭、ASP整个使能寄存器0。停止后核心100读回、GLOBAL0读回、共享resetassert/SCK恢复，再graphSTOP/CLOSE与clock/core释放。总寄存器写上限100、运行8秒，其余已有组合持有/命令/设备期限不变。实际PLL未锁定或MPU未知时不启动核心，失败仍按阶段原样记录。

生产核心锁定前置/严格MPU/持续心跳/关闭守卫与错误控制、PRM平台47、SE15平台41、完整日用与新双kernel均通过，source46hash一致，907B临时候选已备，尚未上机。此候选仍无PCM stream/DMA/扬声器输出。

## mode12实机：锁定前置通过，strict MPU读回FAIL且核心未启动

chip0实际PLL前置通过、GLOBAL运行/ASP关闭，但第一项`2bc3000`原机FFFFFFFF写后读回3，reason14在stage3中止；rate16、MPU1读、27写ACK，started0。此失败不允许直接替换为任意mask或忽略差异。GLOBAL未通过本函数清零（on1/off0），由外层真实共享resetassert收容，再恢复SCK、关闭图并释放本轮clock/core，held0/uncertain0和Android完成通过。不要把reset收容倒改为GLOBAL清零读回成功。

下一独立mode13保持实际PLL/GLOBAL条件，完整采两颗20项MPU向量并关闭钥匙后的稳定向量，不启动核心。旧GLOBAL0时经验向量不能直接提升为本条件读回资格；只有完整实机向量齐全且稳定后才定义下一准确profile。旧mode12strict失败完整保留。

mode13完整powered向量及post-close稳定控制、PRM平台48、SE15平台41、完整日用和新reduced/full内核通过，source46hash一致，907B三私有ADT临时候选已备。采集完整不认MPU权限或保护程序执行通过；未上机。

## mode13实机：完整powered向量及稳定性采集通过

两颗各20项实际MPU读回完整：`3,1,ff0000,fffff,0,0,0,0,f,0,0,0,0,0,0,0,0,0,0,0`，与关闭钥匙后重读逐项一致（20/20稳定各一颗），实际PLL锁定前置通过。96次配置写ACK，GLOBALon/off与ASP/AMPBST关闭、共享复位/SCK恢复/图关闭/clockcore释放通过；1,388,512字节新鲜deadline/ADSP13/电池/无fatal/Android完成通过。仅完整采集，不认MPU权限或核心执行。

独立mode14在这一实际powered/locked完整证据上定义准确全向量和关闭后稳定守卫，未知差异仍失败，旧strictFFFFFFFF模式12失败不变。之后才做双核心100样本/HALO2/至少10次心跳变化及停止/完整清理。不是忽略位或任意掩码，`mpu_rights_verified=0`保持。新host/双kernel完成后才派独立无声轮；扬声器输出始终关闭。

mode14准确powered全向量/关闭稳定与未知变化负控制、PRM平台49、SE15平台41、完整日用和新reduced/full均通过，source46hash一致；907B三私有ADT候选已备，尚未上机。旧mode12strict失败和mode13纯采集证据分列。

## mode14实机：准确MPU/核心启停通过，实际无心跳FAIL

chip0实际PLL锁定前置通过，准确20MPU向量和关闭后20稳定均匹配，core100→101→100实际启停；100样本HALO0/心跳0，VIO/WDT0，49次写ACK、reason4stage5。chip1未继续。GLOBAL本函数on1/off0保留，外层共享resetassert收容后SCK恢复、图STOP/CLOSE和clock/core释放通过，held0/uncertain0。1,413,167字节新鲜deadline/ADSP13/电池/无fatal/Android完成通过。不得记保护程序运行或声音通过。

停止不变候选重跑。下一本机只读根因复核原机`wm_halo_apply_calibration`/MAX_LRCLK_DELAY前置、WS/LRCLK和DSP控制步骤；原机代码明确pre_run先于MPU/core。已经验证的时钟/MPU和安全收尾保持分列，新的实现必须有准确原机输入和有限无输出验收。

## 原机帧时钟前置的独立mode15候选

本机复核实际模块：96kHz分支15cd8–15ce8设置GLOBAL_FS编码3（通用上游为4，不能迁用），原机pre_run按固件metadata写MAX_LRCLK_DELAY32；该控制位于实际global XM2800804。此前WS123一直GPIO输入，数据线隔离不变。mode15仅补这一明确原机帧前置组合：WS123 nativeALT1/8mA配置2c4、globalFS低5位准确3且保留其他位、MAX_LRCLK_DELAY32，逐项完整读回；随后原实际锁定/准确MPU/100样本无声运行。不能单独归因组合中的某一项。

GLOBAL/AMPBST/ASP守卫保持；退出先停核心/GLOBAL0并assert共享reset，再恢复WS和SCK，最后关闭图和释放时钟。两颗各两个固件/采样率写，总104写上限。无PCM stream/DMA/data脚连接/AMPBSTASP输出。新host/双kernel完成后才派独立轮，失败不放宽心跳。

同时只读刷新原机校准缓存得T/B原值与ambient0；它们尚未在此候选应用，更没有运行声学校准。外部委员会分析创建被自动审批拒绝，两个代理均未创建，无外部分析结论；本候选依据本机原始模块与metadata。

mode15生产WS/SCK所有权与恢复、FS3保留高位/MAX_LRCLK32完整读回及错误控制、PRM平台50、SE15平台41、完整日用与新reduced/full均通过，source46hash一致；907B三私有ADT候选已备，尚未上机。仅主机资格，不认帧输入或心跳实机通过。

## mode15实机：帧前置准确生效，心跳仍FAIL

WS123实际2c4、chip0 FS3/3、MAX_LRCLK_DELAY32/两写读回通过，实际PLL/准确MPU/core100101100成立，但100样本HALO0/心跳0，51写ACK、reason4stage5，chip1未继续。停核心及共享复位、WS/SCK恢复、graphclose/clockcore释放全部通过。1,406,797B新鲜deadline/ADSP13/电池/无fatal/Android完成通过，34.4°C。不得记保护程序运行或首声资格。

停止这一候选原样重跑。下一读取原机定义的CORE_SOFT_RESET、DSP_DEBUG、timer/resume、scratch、memory master与AHBM窗口调试寄存器，在启动前/启动中/停止后有限快照，以判断核心运行/存储前置；未知字段仅保留raw，不把DEBUG值冒称程序计数器或写未知使能位。原机校准缓存与控制metadata已只读留存，尚未应用。

## 独立mode16只读核心状态候选

保持mode15全部配置写与无声边界，仅新增15个确切原机/公开header可读寄存器的六时点快照：core启动前、enable读回后、sample0/49/99、停止读回后。覆盖CORE_SOFT_RESET/DSP_DEBUG/TIMER/STREAM_ARB/RESUME、四scratch、clock override、XM/YM/PM master和AHBM debug窗口。只读、不写DEBUG selector、不新增未知使能位；没有已核PC字段定义，未知值只记raw。

即使心跳失败，也在安全停止后有限采最后快照，保持原失效状态；采集完整可单列通过，不能改为HALO执行通过。生产核心六时点完整/停止后状态/无心跳故障控制、PRM平台51、SE15平台41、新双kernel/完整日用全部通过，source46hash一致；907B临时候选已备，尚未上机。

## mode16实机：六时点核心原始状态完整，心跳仍未取得

chip0六时点×15寄存器完整采集，向量前中后相同：CORE_SOFT_RESET0、DEBUG/TIMER/STREAM_ARB/RESUME与scratch均0，clock override0、XM/YM master7/7、PM master1；AHBM debug0=0/debug1=d000000。按固定公开AHBM flags低8位定义错误flags0；地址域/其他位不推断成PC或新故障。没有软复位保持的正证据，也没有scratch进展；这些原始值不能证明CPU已执行到固件入口。100样本HALO/心跳仍0，core实际停止、共享reset/双时钟脚恢复、图关闭/lease清理与Android返回通过。

不猜写DEBUG/resume/master使能位。下一有准确输入支持的原机pre_run增量是保护固件index9的缓存CAL_R、CAL_CHECKSUM=r+1、CAL_STATUS1，以及T角色ENABLE_FULL_US_BYPASS1；全部来自实际原机模块/metadata，校准缓存已只读刷新，不运行或伪造声学校准。CAL_AMBIENT仅index10路径，此处不得写入假环境温度。该增量仍在输出关闭和有限心跳/停止守卫下单独验收。

## mode17原机保护固件pre_run候选

原机`wm_adsp_event`在MPU/core前调用`wm_halo_apply_calibration`。保护fwindex9除已有MAX_LRCLK_DELAY32外，还把缓存的正电阻值、R+1校验和及状态1写到每颗的易失RAM；只有T/TL角色写ENABLE_FULL_US_BYPASS1。CAL_AMBIENT只属于fwindex10分支，本候选不写、不猜环境温度。缓存输入不是新声学校准结果。

新mode17通过两个私有临时boot参数传值，缺值、零值或校验和无法容纳24位时在功放MMIO之前SKIP；固定地址、序列、逐项完整读回。保持PLL/准确MPU/帧参数、100样本HALO2及至少10次心跳变化、停止/共享复位/双引脚恢复/图和lease清理要求。AMP、boost、ASP输出和播放数据仍关闭。52项实际PRM平台、44项SE15及生产核心正负控制通过；日用/双kernel验证中，尚无mode17设备结果。首个沙盒构建因sysctl被拒在make准备阶段失败，证据保留；解除该构建环境限制后重建，不把环境失败当驱动编译结论。

mode17后续主机验收：完整日用、新精简/完整kernel均PASS，46个运行源hash一致；943B候选与三份私有ADT输入逐字节检查通过。fresh Android完成/后部USB/无owner已核，实际设备结论等待独立有界无声轮。

## mode17实机：pre_run读回通过，保护心跳仍FAIL

chip0缓存电阻/校验和/状态/topbypass四项完整读回通过；PLL64/64LOCK、准确MPU与core100→101→100通过。100样本HALO/heartbeat0、VIO/watchdog0，55配置写均ACK，chip1未派。函数未独立清GLOBAL，由外层真实共享reset收容，再恢复WS/SCK、STOP/CLOSE图及释放clock/core；最终held/uncertain均0。200秒返回、1,410,442B新鲜deadline、ADSP13/电池/无fatal、Android完成见证通过，34.6°C。不得把pre_run成功提升为程序执行资格。

后续原机反汇编找到一个具体遗漏：初始化`cs35l41_fs_errata_patch`的16项值1，不等于启动时最终速率。`wm_halo_init`用清零分配建立RX/TX速率数组；`wm_halo_start_core`在置core bit0前调用`wm_halo_set_rate_block`，逐项以mask1f更新数组值0并保留高位。独立mode18拟补此启动步骤，保留旧errata控制与全部输出关闭/心跳要求。该结论是原机默认启动输入，尚未声称当前Android活动播放时速率值已采得。

mode18源实现已完成：保留16项初始化errata1与MPU，core前新增RX/TX各8项以mask1f更新0，完整前后读回/保留高位，143配置写上限。模型覆盖全部16个错误读回在core前失败、高位保留、无心跳有限停止；53项PRM/45项SE15通过。同步修正PLL摘要固定capture_only/core_start字段为实际值，避免日志误报；无新的音频输出写。新日用/双kernel中，尚未打包或设备验证。

mode18主机收口：生产核心合成及真实原机WMFW/T/B输入、53PRM、45SE15（加强后置断言的独立复核）、完整日用及新双kernel通过。46个运行源hash一致；943B候选/私有ADT逐字节匹配、fresh Android完成/后部USB/无owner通过。下一单owner原30分钟监督无声轮，设备结果尚待采集。

## mode18实机：启动速率读回通过，心跳仍FAIL

chip0全部16个native start速率字段实际1→0完整读回，cachedpre_run四项、PLL64/64、准确MPU/core100101100通过。但100样本HALO/heartbeat仍0、VIO/watchdog0，71配置写全部ACK；chip1未派。共享reset/WS+SCK恢复/图6ACK/clockcore5ACK且held/uncertain0通过。200秒返回、1,413,029B新鲜deadline、ADSP13/电池/无fatal/Android完成见证通过，35.3°C；旧失败保留。

下一独立mode19针对原机DSP输入时序：实际模块`cs35l41_dapm_widgets`不可变表与relocation确认ASPRX1/2为4800的bit16/17，而ASPTX1..4为bits0..3；RX是接收门，不是扬声器输出使能。现有候选将整个ASP寄存器保持0，和原机ASPRX依赖在DSP启动前打开的路径不同。拟仅开ASPRX1输入bit16（完整寄存器必须10000，TX全0），AMP/BST保持off、数据脚仍GPIO输入、无PCM。成功后显式清RX，失败则共享复位收容，所有lease仍按实际清理证据释放；不得推广为播放或首声。

mode19 source与主机：147写上限；PLL后/MPU前只启ASPRX1，反复完整比较ASP=10000，AMP/BST掩码31仍0；成功corestop/GLOBALoff后清RX0，失败不假称清零而由共享reset收容。生产核心合成和真实WMFW/T/B、54PRM/46SE15正负控制通过；初版双kernel通过，旧摘要固定asp_writes=0已修成准确asp_tx_enable=0后，最终双kernel重建与日用收口中。尚未包/设备轮。

mode19最终主机收口：更正日志后的完整日用和精简/完整kernel通过，46个运行源hash一致，943B候选/三份私有ADT核对通过。fresh后部USB/Android完成/无owner通过；下一原30分钟单owner无声轮，设备结果待采。

## mode19实机：接收门通过，保护心跳仍FAIL

chip0 ASPRX1完整寄存器10000读回且后续重复守卫通过，所有TX位0、AMP/BST关闭/数据脚隔离。nativeRate/pre_run/PLL64/64/准确MPU/core100101100仍通过，但100样本HALO/heartbeat0、VIO/watchdog0，72配置写均ACK，chip1未派。失败时RX仍由函数保留，不能宣称函数显式关闭；外层真实共享reset收容后双时钟脚恢复、图6ACK/clockcore5ACK、held/uncertain0全部通过。198秒返回、1,414,657B新鲜deadline/ADSP13/电池/无fatal/Android完成通过，35.4°C。

下一明确原机输入增量为SP_FORMAT4808：已核ACDB TDM为4×32bit、shortsync、normal、delay1；原机set_dai_fmt的DSP_A/slave/normal对应format/master/invert字段0，pcm_hw_params以physical slotwidth32设置RX高8位。独立mode20只将此寄存器maskFF000755更新value20000000，完整前后读回并保留TXwidth/force/reserved位。逻辑位宽24的RX_WL、数据/发送/功放开关不在本增量改动内；不据未读当前默认值推断因果。需新host/双kernel和fresh独立无声轮，心跳要求不变。

mode20源实现及定向主机完成：固定SP_FORMAT单寄存器mask/value/完整读回，149写上限；模型证明保留非mask位（包括TXwidth与force）、其他位错读时在PLL/GLOBAL/core前失败、无心跳仍有限停核。55项PRM/47项SE15通过，新日用/实际原机输入回归及双kernel中。未备包/上机。

mode20主机收口：生产核心/真实原机输入、55PRM/47SE15、完整日用与新精简/完整kernel通过，46运行源hash一致，943B候选备妥；fresh后部USB/Android完成/无owner通过。下一独立有界无声轮。另已核原机probe2138..2148在trim cache后、wm_halo_init前将CCM_CORE_CTRL完整写0；当前流程仅mask1停核心并保留100。此为下一独立源输入，不混入本轮。

## mode20实机：接收格式生效，心跳仍FAIL

chip0 SP_FORMAT实际18180200→20180000，两次完整读回/非mask位保留通过。RX10000/TX0/nativeRate/pre_run/PLL64/64/准确MPU/core100101100通过，100样本HALO/heartbeat0、VIO/watchdog0，73配置写全部ACK；chip1未派。共享reset/WS+SCK/图6ACK/clockcore5ACK、held/uncertain0均通过，200秒返回、1,413,181B新鲜deadline/ADSP13/电池/无fatal/Android完成通过，35.2°C。

下一mode21回补更早的原机probe行为：2138..2148在trim cache后、wm_halo_init前完整写CCM_CORE_CTRL=0；现有safe-off仅mask1，保留100。新固定native RAM入口拟在同轮B2/OTP/trim后、任何固件字节写前执行整字0及严格读回，PWR1/PWR2须保持，原trim receipt不改写。原始固件RAM写数仍14938，新增core-init写另记2次；RAM状态对照独立新expected状态。新状态下先全量参数读回、PLL/帧/RX保持、采全部20 MPU原始值并关闭后重复稳定核对，无核心启动。不得默认迁移旧core100状态的经验权限向量；只记录新实际状态，后续再定执行资格。

mode21源与定向主机：新native RAM入口将两次核心初始化写与14938固件写分列，trim原receipt保持100，新的RAM/参数状态严格为0。新profile14捕获完整20MPU并关闭后20稳定、nativeRate0、GLOBAL/RX显式关闭，无core/HB执行。生产核心/56PRM/48SE15通过；完整日用/真实原机固件参数/新双kernel中。所有诊断capture成功与amp_pass继续分开，不能报保护程序运行。

mode21主机收口：完整日用、真实WMFW/T/B输入、56PRM/48SE15及新精简/完整kernel通过，46运行源hash匹配，943B候选/三私有ADT核对通过，fresh后部USB/Android完成/无owner通过；下一独立有界资格采集，实际结果待验，不称DSP运行。

## mode21实机：原机完整core-init与新状态采集通过

两颗核心控制均100→0完整读回，PWR1=0/PWR2=3300不变，各6状态读；新增2次核心初始化尝试=ACK，与14938固件RAM写独立计数。原trim的core100证据保持，之后两颗RAM/分声道系数完整验证通过；新状态下PLL64/64/nativeFORMAT/RX/rate0、20MPU原始值及关闭后各20稳定通过。两颗向量完整相同，也与历史core100向量实际相同：3,1,ff0000,fffff,0,0,0,0,f,其余0；这是新采集结果，不是默认迁移或权限语义证明。145配置写均ACK，无核心启动/心跳资格。

两颗GLOBAL/RX显式关闭、共享reset/双引脚/图6ACK/clockcore5ACK、held/uncertain0通过。200秒返回、1,419,824B新鲜deadline/ADSP13/电池/无fatal/Android完成通过，35.1°C。下一mode22以本轮core0新向量为明确前置，唯一新增为core0→1→0及100样本HALO2/≥10心跳执行观察，输出仍关闭。需新host/dual/fresh独立轮；不能把本次采集成功称为程序运行。

mode22源实现与定向主机：独立profile15仅接受coeff核心状态0、使用本轮重新实测的完整MPU向量/关闭后稳定性；实际core0→1→0/100样本HALO2/≥10心跳、无VIO/watchdog、GLOBAL/RX关闭/最终复位要求保持。149配置写上限，核心初始化2写仍另记。生产核心/57PRM/49SE15通过，新完整日用/真实原机输入及双kernel中，未备包/上机。

mode22主机收口：生产核心/真实原机WMFW/T/B、57PRM/49SE15、完整日用与新精简/完整kernel通过；46运行源hash一致、943B候选备妥，fresh后部USB/Android完成/无owner通过，下一独立有界无声执行轮。实际心跳和双芯片执行仍待采集。

## mode22实机：两颗原机保护程序执行通过

两颗均100次真实采样，HALO2、heartbeat a→da9、99次变化，core0→1→0精确启停，VIO/watchdog均0；149配置写全部ACK，耗时6,507,782微秒。nativefullcore0/RAM/分声道参数、PLL64/64/nativeFORMAT/RX/rate0/新实际完整MPU向量和关闭稳定性全部通过。之前所有core100/101/100无心跳失败保留；这次证明完整原机初始化后的短时执行，不将唯一差异扩张为所有场景根因证明。

两颗GLOBAL/RX显式关闭，随后共享reset/WS+SCK恢复、图6ACK/clockcore5ACK/held0uncertain0全部通过。200秒返回、1,380,219B新鲜deadline、ADSP13/电池/无fatal/Android完成通过，35.3°C。此项最小交付已满足，仅保护固件短时运行及安全停止；未证明实际播放时长期保护、声学/调音、微音器或首声。

下一AUDIO-PCM-01建立与已验证本机TDM端点相连的播放stream，先资格化格式/OPEN/CFG/PREPARE/START/STOP/CLOSE，不发数据。实际缓冲区必须来自UEFI保留分配，不能猜低地址；saved本机DT的msm_audio_ion确认为appsSMMU15000000/SID1801、78CB/120SMR上限（仍需实际ID交叉核对）。SMMU/内存映射、buffer完成、串行数据引脚、DSP接收路由与首次输出分别验证；PongTFA初始化继续禁止。首次出声前停叫用户。

## AUDIO-PCM-01 / mode23默认关闭播放图候选

新设备层`ZizhanPcm.h`和实际ShengAudioReach encoder建立一个WR_SHMEM模块，SG4101/container5101/端口2103/IID6181是client-owned ID，连接已验TDM IID6281；无解码/转换/重采样假设。媒体来自已验原机TDM96k/24有效位/三声道：32存储位、MSB对齐2、Q31、little-endian1、interleaved1、channel-map1/2/3。官方fixed header的alignment/interleaved为两个8位字段，payload19/参数31字节，独立8字节包对齐；不复制旧通用encoder将pad计入字段的做法。

流程为TDM启动→PCM OPEN/MEDIA/PREPARE/START→原mode22双功放保护程序资格与reset/恢复引脚→PCM STOP/CLOSE→TDM STOP/CLOSE→clock/core释放。所有回复严格源/目标端口、token/opcode/精确8字节status；错误/未知transport保留依赖，不假造关闭。无内存映射、缓冲区、DMA、数据脚或输出操作。实际encoder/官方格式golden与六命令失败控制、63项PRM平台通过；新完整日用/双kernel待验，尚未备包/设备结果。

mode23主机收口：PCM编码/严格生命周期、63PRM、完整日用、新精简/完整kernel通过，47运行源hash一致、943B候选/私有ADT已备；fresh后部USB/Android完成/无owner通过，下一原30分钟独立无声图资格轮，无DMA/map/data。

## mode23实机：PCM六ACK通过，TDM停止超时

PCM OPEN/MEDIA/PREPARE/START/STOP/CLOSE六个真实status0回复全部齐，phaseDone/opened0/running0/自身uncertain0。两颗HALO各100样本/99心跳/core0/1/0再次通过，reset/双引脚恢复通过。但之后独立TDM STOP在2秒内无回复，sent5/ack4，未派CLOSE及clock/core释放；上层按规则保留held1/uncertain1。最终1,427,048B新鲜deadline/Android完成/35.1°C返回通过。不能把PCM局部六ACK或自动复位当完整图与lease清理成功。首版成功review过早假定PCM自身FAIL必uncertain，断言失败保留；独立实际范围review重新分列本地PCM完成与整体清理FAIL。

原机实际libar-gsl的STOP构建在1cefc..1cf34按实际SG数量创建list（payload=(count+1)*4），1cedc将所有匹配SG逐项加入；官方APM SG-list允许多个子图。下一mode24在同一STOP/CLOSE命令中列PCM SG4101与TDM SG4105，避免先拆除已连接stream后独立停端点。是否解决必须新设备轮，不能把关联当既定因果。无DMA/数据/输出；严格实际ACK和超时/依赖保留要求不变。

mode24源与主机：STOP/CLOSE的参数体精确12字节，两SG列表，单个实际回复同时更新两个子图状态，但TDM自身sent/ack仍4/4，不伪造独立6ACK。总实际命令15；不再派重复TDM停止。编码golden/两命令失败控制、66项PRM通过；unsupported模式测试后置范围初版过宽失败，已限定有效23..24，v2通过，原日志保留。新日用/双kernel中，尚无候选/设备结论。

mode24主机收口：PCM/66PRM/完整日用/新精简完整kernel通过，47运行源hash一致、943B候选备妥，fresh后部USB/Android完成/无owner通过；下一独立原30分钟监督无声轮，实际联合收尾待验。

## mode24实机：联合图收尾与保护程序通过

PCM OPEN/MEDIA/PREPARE/START四回复0，两个子图的联合STOP/CLOSE各72字节、两次真实回复0；PCM六ACK/phaseDone/opened0/running0/uncertain0。TDM自身启动四ACK保留4/4，由包含其SG的实际联合命令完成收尾，最终phaseDone/opened0/running0/uncertain0，不伪造独立6ACK。clock/core五ACK/held0uncertain0通过。

两颗保护程序各100样本/99次心跳/HALO2/core0→1→0、149配置ACK、无VIO/watchdog及reset/WS+SCK恢复继续通过。200秒返回、1,389,932B新鲜deadline/ADSP13/电池/无fatal/Android完成35.1°C通过。此项最小交付是本机nativePCM格式/图生命周期与安全收尾，尚无缓冲区、DMA、数据脚或声学资格。mode23旧超时保持FAIL。

下一AUDIO-DMA-01先只读实际chosen的UEFI保留64KiB范围与SMMU：真实全局锁，能力与本机DT78CB/120SMR交集上限，两次完整有序ID/SMR/S2CR/CBcontrol快照稳定比较，实际SID1801现有映射及可用计划。采集PASS和可建立映射分别记录，不调用任何writer，不触碰音频缓冲区或发DSPmap/data。

## AUDIO-DMA-01 / mode25只读内存前置候选

`ZizhanAudioDma.h`对真实UEFI保留extent作非零/页对齐/64KiB/低4GiB边界核对；实际platform先拿全局SMMU锁，用只读映射采ID/SMR/S2CR/CBcontrol各两遍（最大954读）且完整稳定，按硬件ID与本机78CB/120SMR上限交集；调用现有纯读计划器，不调用writer。记录所有原始slot/bank和可用计划/具体拒绝原因；capture通过不代表plan可用，不能假称映射建立。缺extent/锁/map在图与功放操作前失败，保留已取得PRM依赖到真实复位。生产只读核心8控制与70PRM（含平台extent/锁/只读map/清理）通过；首次fixture插入位置错误导致off断言失败，修正后v2通过，生产默认off不变。新日用/双kernel中，尚无候选/设备结果。

mode25主机收口：只读核心/70PRM/完整日用/新精简完整kernel通过，49运行源hash一致，943B候选备妥；fresh后部USB/Android完成/无owner通过，下一原30分钟监督独立只读资格轮。实际extent/SMMU/plan待采，不先写映射。

## mode25实机：真实保留区与SMMU只读前置通过

实际UEFI给出非零页对齐64KiB、低4GiB保留区。全局锁下SMMU完整两遍908读稳定，ID0=4c017e71/ID1=6000004b/ID2=5111，实际75CB/113SMR/pages128，与DT上限交集。SID1801计划可用slot112/bank73，无复用/迁移；slot112原SMR0/S2CR20000，bank73原SCTLR2000230a且M=0、CBAR/CBA2R0；bank74被其他映射占用，计划器未选它。计划PASS不代表已建映射，实际writes0/data0。

同轮nativePCM联合收尾、两颗HALO各100/99心跳、reset/双时钟脚/clockcore释放held0uncertain0通过。199秒返回、1,425,228B新鲜deadline/ADSP13/电池/无fatal/Android完成35.8°C。下一有界增量只建立保留区限定映射与ADSP句柄再撤销，不发送PCM/接数据脚/启输出；必须独立保留原CB/SMR状态、真实表所有权与TLB、错误保留及实际恢复读回。

## AUDIO-DMA-02 / mode26真实映射与句柄候选

按每轮实际UEFI保留64KiB及fresh独占SID1801计划建立仅16页的native4K映射；拒绝复用/迁移，页表由真实连续分配取得且与资源不重叠，保存原CB/SMR/S2CR。写前清洁真实页表，并用共享审计器遍历完整物理表（包括空位）、激活后观察实际TTBR/配置与fault/TLB。ADSP内存申请必须收到实际非零MAP_DONE句柄，随后UNMAP实际status0；普通basic成功不能冒充句柄。核对固定LinuxAudioreach原生alloc_apm_pkt不含命令头，单独设备层MAP44B/UNMAP28B，不更改旧通用编码器。无缓冲区访问、PCM发送、数据脚或声音输出。

联合图收尾后，先把本SID置fault、关闭自有CB并TLB同步，再恢复原CB控制和SMR/S2CR完整读回；未知写/协议/撤销失败保留真实页表直到复位，不释放未知依赖。核心事务逐写失败/权限或fault拒绝、8项实际平台生命周期、协议控制、75PRM平台通过；初版平台模型W1C及测试unused变量错误已修，负日志保留。actualAudit先用实际ID再调用，避免未初始化能力。精简构建仅补共享4K实现，完整构建复用Adreno archive，尚待新双kernel/完整日用与实机资格。

mode26主机收口：事务逐写失败、实际平台8控制（含物理页表全图与生命周期）、ADSP协议/75PRM、完整日用及新精简完整kernel通过，60运行源hash一致；943B候选/私有ADT核对，fresh后部USB/Android完成/无owner通过。下一原30分钟独立无声内存句柄资格轮，尚不发送数据。


## mode26首轮：实际映射StateMismatch，数据与功放阶段未执行

实际native4K程序返回StateMismatch(7)，failure phase6，attempted1/installed0/restored0/uncertain1。现有日志没有失败前阶段与各字段原始值，不能据此推定是哪项寄存器或放宽判定。DSP内存sent0/handle0，两图各启动4ACK，但后续保护执行/联合收尾/资源释放未进入；页表和依赖保留至自动复位，不记清理PASS。没有PCM数据/数据脚或功放输出。

198秒自动返回，1,386,198B新鲜deadline，ADSP13/无panic或fatal/Android完成及33.9°C通过。主机通过与设备失败分列，旧候选保留。下一只加入设备层有限写轨迹及expected/actual原始配置诊断，不变native4K严格比较、不重跑未改候选；新host和双kernel后独立资格轮。

诊断增量主机收口：仅板级32项写尝试轨迹及prior/expected/program-readback、实际ID/恢复标志；shared native4K代码与严格判定不变。实际平台10项（含硬件SCTLR高位/CBAR读回不符）、完整日用及新双kernel通过；60运行源hash一致、943B新候选/fresh后部USB/Android完成/无owner通过。下一独立诊断轮；现有首轮FAIL保持，不先推进数据。

## AUDIO-DATA-01 / 独立单缓冲协议HOST_VERIFIED，未接运行内核

等待上述诊断设备轮时，新增独立ZizhanAudioWrite.h及生产协议sanitizer控制。按已存原生wr_sh_mem_ep V2契约构造68字节GPR+44字节body，限定本机endpoint6181/port2103，单次11520字节（96kHz/三通道32位/10ms），调用方必须先真实拥有零缓冲区、映射和非零句柄。完成事件32字节没有长度字段，不伪造长度；严格核对peer/domain/token/address/handle/status和无metadata，basic成功不当消费完成。超时/错地址/句柄/metadata/返回失败不归还所有权。18种协议控制及倒退时钟通过。

该header尚无runtime调用者，未加入新kernel/包或上机，不访问缓冲区/发数据/接数据脚/出声；它仅准备下一最小增量，必须先解决实际映射与句柄。

## mode26诊断：TCR2两位与所请求策略不符

真实prior/expected/program-readback和15次写尝试完整到达，trace overflow0。所有CBAR/CBA2R/TCR/TTBR0/1/MAIR均匹配；唯一非预期配置差异TCR2写38011、读38071，即TBI0/1两位60。SCTLR在禁用配置检查时为0且没有enable写，是本阶段预期；不能把它另算错误。实际故障0、恢复disable1/TLB1，但主流程仍严格FAIL并保留图/PRM到自动复位。1,389,600B新鲜deadline/ADSP13/无fatal/Android完成通过，DSPsent0/handle0/保护执行未进入，无数据或输出。

下一仅板级显式请求ignore_top_byte=1，与现有通用native4K低32位策略一致：只接收实际UEFI低4GiB/16页，公共编码器拒绝upper/wider VA；忽略顶字节不扩展调用方允许的地址集。TCR2应精确38071，其余根、宽度、cache、fault、ASID和全部字段仍完整比较，不把观察差异掩掉。旧FAIL保留，新host和双kernel后独立上机验证，实际handle仍待取得。

TBI增量source/定向host：仅板级显式ignore_top_byte=1；实际平台11控制将真实TCR2 bits60纳入MMIO模型，并对另一个TCR2位错误仍严格FAIL。原状态必须同样含实际bits60才能完成精确恢复，初版fixture未建模原状态导致恢复FAIL已保留，v2纠正后通过。新双kernel/日用运行，WRITEheader仍无runtime调用者。

TBI候选主机收口：实际平台11控制/完整日用/新精简完整kernel通过，60运行源hash一致；943B候选/fresh后部USB/Android完成/无owner通过。下一原30分钟独立资格，真实映射/句柄/恢复仍未通过，不能把源码修正当设备成功。

数据准备补充（host-only、未接内核）：原引导源分配EfiReservedMemoryType的16页并zero/flush。新增ZizhanAudioZero.h的真实extent/16页managed拒绝/两遍全64KiB零检查核心，22项sanitizer通过；后续调用方必须提供只读Normal-NC CPU映射，在DSP取得数据前采集，不写buffer。当前无runtime调用者，不将参考源或host模型当成实际CPU buffer资格。

## mode26显式TBI：配置阶段通过，启用后ASIDPNE差异

实机TCR2精确38071，CBAR/CBA2R/TCR/TTBR/MAIR均匹配；实际出现enable写1027，读回27，即SCTLR ASIDPNE bit12未保留，严格StateMismatch。raw FSR400保留（共享fault mask未涵盖该位），不声称raw fault0或判定为致命fault；disabled1/TLB1，但图/PRM仍保留至reset。DSPsent0/handle0/功放未调用。1,397,235B新鲜deadline/ADSP13/无fatal/Android完成35.9°C通过，旧FAIL不覆盖。

下一显式采用现有native4K shared_asid=1策略（SCTLR期望27），并先在全局锁下核其他实际活动CB控制和两个TTBR的ASID两遍稳定且均不为1801，冲突/变化在任何写前拒绝。不掩码读回，不改共享encoder/programmer或其他bank；仅实际低32UEFI/16页。须新host/双kernel和独立无声映射/句柄资格。

shared-ASID增量source/定向host：全局锁下其他活动CB的SCTLR/TTBR0/1两遍完整稳定、ASID1801不冲突才继续；只显式shared_asid1，低32/TBI1/全部精确比较保留。实际平台14控制含lower/upper ASID冲突及活动控制变化写前拒绝通过；实际MMIO模型保留FORMAT400，任意真实fault位仍由原mask拒绝。新日用/双kernel运行。

公开[Linux v6.12 arm-smmu.h](https://github.com/torvalds/linux/blob/v6.12/drivers/iommu/arm/arm-smmu/arm-smmu.h)定义FSR FORMAT为bits10:9，400是格式值2；FAULT mask不含该字段。[arm-smmu.c](https://github.com/torvalds/linux/blob/v6.12/drivers/iommu/arm/arm-smmu/arm-smmu.c)同样先用FAULT mask判断。继续记录raw400，同时明确masked_fault0；共享native4K原mask未修改。该定义核对不将前次SCTLR不符提升为PASS。

shared-ASID候选主机收口：实际平台14项/完整日用/新精简完整kernel通过，60运行源hash一致；943B候选和fresh后部USB/Android完成/无owner通过。下一原30分钟独立资格轮，实际映射/句柄/恢复待验。

## mode26共享ASID：映射/真实句柄已观测，整轮因恢复panic拒绝

实际ASID namespace两遍148读/active0/no-conflict；native4K精确配置、完整16叶物理审核/TLB通过，rawFSR400/masked0分列。ADSP实际44B MAP返回非零handle、28B UNMAP回复0，随后两颗100/99保护心跳与reset/时钟脚恢复通过。联合STOP/CLOSE两次真实ACK后，在SMMU恢复阶段panic；无最终恢复/PRM释放，整轮REJECTED，不能提升为完整lease或日用基线。

68秒返回，713,963B新鲜panic日志（没有deadline或截图），Android完成34.4°C通过。实际同kernel符号化worker调用dma_map_end，write32的真实STR指令触发implementation-specific error。图关闭后SMMU资源前置丢失为具体假设，尚无直接clockgate读回，不写成已证根因。下一只调整时序：同一owner中真实MAP→UNMAP→完整SMMU恢复在图START和PRM core/clock仍持有时立即完成，再执行原保护程序/联合图关闭/PRM释放；不改变寄存器内容或判定。不重试同候选。

收尾顺序增量source/定向host：只将实际UNMAP后完整SMMU恢复提前到功放执行/图关闭之前；生产PRM75控制通过，实际平台stub要求pcm只4ACK/功放未调用时restore，恢复失败保持图和PRM直到复位。新日用/双kernel中，无新设备结果。

提前恢复候选主机收口：75PRM/完整日用/新精简完整kernel通过，60源hash一致，943B包/fresh后部USB/Android完成/无owner通过；下一原30分钟监督独立资格，旧panic候选不重用。

数据准备独立平台host补充：ZizhanAudioZero.cpp使用当前真实映射extent、拒绝16页中任意managed VM页后建立只读Normal-NC CPU view、实际两遍全64KiB读取，关闭CPU映射后才允许后续DSP操作；实际平台5种控制通过。尚未加入build脚本/PRM，无kernel或设备资格。运行中的earlyrestore候选60源未改变。

## 提前恢复控制轮：图仍START时同样panic，旧解释被排除

实际同样完整映射/非零MAP handle/UNMAP ACK0，立即恢复（功放未调用、没有STOP/CLOSE、PRM仍持有）仍在write32触发同implementation-specific panic。前次“图CLOSE才使资源前置丢失”解释被该负控制排除，不能沿用为结论。40,452B新鲜panic、36秒返回、Android完成32.8°C；没有deadline/截图/完整清理，整轮REJECTED。

现在阻塞为真实ADSP UNMAP之后的SMMU MMIO访问条件未确定，暂停新数据和新设备候选，核原机SMMU时钟/电源/owner要求；不能继续仅变顺序或放宽读回。独立零缓冲/WRITE host准备可继续。

## AUDIO-DMA-03 / 本地映射恢复负控制（host候选）

固定原机BSP cb7a356ae138e77992ae199eebb86559d825a673的arm-smmu.c/h/qcom.c已取回并hash存仓外。qsmmuv500_impl不采用另一compatible的qcom_smmu_write_s2cr bypass_quirk，不能照搬其TYPE替换；saved live apps节点也无显式clock/regulator声明，不能猜GCC位作为修复。实际clock/HYP原因尚未确定。

补齐恢复前真实所有权核对：在第一次恢复写前读exactSMR/S2CR及共享ObserveOwnedContext完整image/fault，固件变更时零写拒绝；host包括SMR/S2CR/TTBR/SCTLR/fault五项新负控制通过。独立default-off mode27只做contiguous真实SMMU begin/end，零DSP memory命令，保持已知可访问/全局锁/图START/PRM资源；这与已失败“DSP UNMAP之后恢复”区分，不再改变同一时间点重跑。实际PRM78控制通过（v1fixture插入位置错误、v2修复），实际平台14/事务逐写失败通过，新日用/双kernel中。该轮只能决定本地恢复路径是否独立就失败，不能证明DSP句柄生命周期通过。

mode27主机收口：恢复核心/实际平台14/PRM78/完整日用/新精简完整kernel通过，60源hash一致，943B包/fresh后部USB/Android完成/无owner通过；下一原30分钟独立本地恢复对照。

## mode27本地对照：零DSP仍恢复异常，旧时序解释排除

实际39,904B新鲜panic/36秒返回/Android完成31.7°C，完整mapping/16leafaudit/namespace通过，之后未发送任何DSP内存命令即在本地恢复writer panic，整轮REJECTED。实际恢复前所有权观察未报失败；没有功放执行或数据。该对照排除必须经过DSP或graphclose的解释。

**纠正前文“第一条恢复写”表述**：实际symbol只定位共享write32中的STR，调用者没有单寄存器证据，可能是恢复中的后续写；在具体before-MMIO轨迹取得前不当根因。原图关闭/DSP撤销前置推断均被此对照取代。下一仅在已有32条以内恢复写尝试之前持久记录index/phase/offset/bits/value，零DSP对照内容和全部严格判定不变，真实异常处理保留，日志明确before_write不等于写完成。

恢复before-MMIO轨迹候选：仅日志增量的实际平台14/完整日用/新精简完整kernel通过，60源hash一致，943B包/fresh后部USB/Android完成/无owner通过；下一独立mode27诊断，真实异常保留，不发任何DSP内存命令或数据。

## mode27轨迹：实际CBAR73=0写触发异常，退出原始上下文恢复方案

5条before-MMIO轨迹完整：S2CR slot112置fault20000，CB73 SCTLR0，ASID TLBI1801，TLBSYNC ffffffff，进入phase5后尝试offset1124/CBAR73=0时panic。进入phase5前共享disable/TLB函数已返回成功；真实异常不在第一次恢复写。39,087B新鲜panic/36秒返回/Android完成32.2°C；零DSP命令/功放未调用/没有PCM数据。实际hypervisor或安全机制未直接读回，不伪称已确认具体策略；该trace足以拒绝继续写回原始CBAR0。

下一改变交付为**安全释放自有上下文**：保持已实机精确通过的native stage1/AArch64格式，SCTLR实际0、ASID TLB同步完成、原unusedSMR0/S2CR20000恢复及读回，完整native idle其余字段精确比较并核fault mask。没有有效stream/enable/TLB引用后释放表；未来owner必须重新编程再发布stream。明确context_retired1/prior_context_restored0，不能说完整原始CB已恢复；旧原始恢复资格仍REJECTED。先同mode27零DSP独立通过，之后才能重做实际DSP句柄全寿命。

native idle候选主机收口：核心逐写失败/新鲜ownership负控制/实际平台14/完整日用/新精简完整kernel通过，60源hash一致；943B mode27包/fresh后部USB/Android完成/无owner通过，下一独立零DSP本地安全释放资格。

## mode27native idle安全释放DEVICE_VERIFIED（仅本地）

真实UEFI16页/完整物理audit/TLB/namespace成立；随后stream_restored1/context_retired1/prior_context_restored0，实际disabled1/native_idle_readback1/TLB1/uncertain0，真实资源释放后继续双保护HALO100/99心跳、reset/双时钟脚、联合图关闭和PRM5ACK held0/uncertain0全部通过。199秒正常返回、1,442,241B新鲜deadline/无panic/Android完成35.0°C。原始CBAR恢复失败不提升为通过；此新交付只认本地native idle释放，无DSP memory命令/PCM数据/声音。下一相同kernel/source60，唯一变量boot mode27→26加入真实MAP/UNMAP，再按已验证nativeidle释放闭环。

真实句柄v2候选已用同一60运行源/已过双kernel构建打包，943B，fresh后部USB/Android完成/无owner通过；原完整wholeOS/extent/backup预检仍由新轮监督独立执行。下一mode26 nativeidle释放，尚无完整DSP lease设备PASS。

mode28 source/定向HOST：已接实际CPU只读零检查、真实MAP/单WRITE/UNMAP/nativeidle释放，错零/错误完成/超时/UNMAP或释放失败全部保持资源直到reset。实际PRM84控制通过；首版C++包含C零初始化触发Werror，改显式memset后v2通过。独立协议/零核心/平台5加入日用，新增Zero.cpp构建链接，63运行源hash组的新日用/双kernel中。该source较仍在设备运行的leasev2旧60源多出数据候选，不将新源码称已上机。

## mode26leasev2：实际DSP句柄与native idle释放DEVICE_VERIFIED

相同已验证kernel/source60、仅mode26加入44BMAP/28BUNMAP，实际非零MAP_DONE句柄和UNMAP0通过。stream_restored1/context_retired1/prior_context_restored0，disabled1/nativeidle完整回读/TLB1/uncertain0，实际内存sent2/ack2/phaseDone/uncertain0。之后两颗HALO100/99/core0→1→0/reset/时钟脚恢复、联合PCM六ACK/TDM原四ACK+联合关闭、PRM五ACK/held0均通过。1,385,319B新鲜deadline/无panic或ADSP失败/Android完成36.6°C。原始CBAR0恢复资格仍REJECTED，此项只认新native idle安全释放。无PCM数据或声学验证。

下一mode28 source63的真实零缓冲单次消费：协议/零核心/实际CPU平台5/PRM84/完整日用/新精简完整kernel已通过，旧C++初始化Werror负日志保留；设备结果尚无。

mode28主机收口：实际WRITE协议/零核心22/CPU平台5/PRM84/完整日用/新精简完整kernel通过，63运行源hash一致；943B候选/fresh后部USB/Android完成/无owner通过。前置mode26完整lease设备PASS已取得，下一原30分钟独立10ms真实全零buffer消费资格，不连接数据脚或启输出。

## mode28真实全零PCM消费DEVICE_VERIFIED（非功放接收或声音）

实际UEFI16页unmanaged，CPU只读Normal-NC两遍全64KiB共16,384读全零。真实MAP句柄后单次68B WRITE至本机endpoint6181/port2103，11,520字节=96kHz/三通道32位/10ms；实际56B GPR+32B V2 DONE，严格地址/handle/token/peer/status0/无metadata，sent1/ack1/uncertain0。随后UNMAP0/nativeidle安全释放/unused stream恢复及实际SCTLR0/TLB读回通过；两颗HALO100/99/reset/时钟脚/联合图/PRM5ACK held0通过。200秒正常返回/1,432,324B新鲜deadline/无panic或ADSP失败/Android完成37.2°C。

此项仅证明真实缓冲区由DSP消费并归还。数据引脚尚隔离，AMP/BST输出关闭，两颗保护运行在该发送之后依次执行，**不称同时运行或在数据输入期间持续保护**，不能提升为功放收到正确样本、声学验证或系统App播放。下一最小增量接GPIO124原机ALT1/8mA/no-output-high路径，并把有限零发送放在实际保护核心运行窗口，仍输出关闭；只在保护实时状态/回读、数据完成、reset/pin恢复和资源清理都成立后考虑首声候选。

## AUDIO-DATA-02 / mode29 数据脚与核心运行中零传输source/定向HOST

已接原机GPIO124数据1 ALT1/8mA/bias-disable配置c4，严格输入1/io0双读所有权，只写config、不写GPIO latch；两颗初始化禁用状态读回后才接，shared reset真实assert后才恢复GPIO输入。核心增加明确同线程有限零传输回调：每颗sample49、实际HALO2/已有≥10心跳变化且AMP/BST/TX关闭才调用；返回真实完成后再核core/power/HALO2/心跳变化，其余100样本/8秒总上限/149配置写不变。两颗仍依次运行，不声称同时或负载保护。

PRM mode29在AMP运行窗口保留真实MAP handle，两个独立token各11520B全零；若任一失败，先共享reset与引脚收容，未知buffer/图/clock保持到自动复位，不提前UNMAP/free。成功后再UNMAP/nativeidle/TLB/联合图/PRM释放。输出仍0。生产核心7新负控制、GPIO9、WRITE第二token/旧token拒绝、PRM92、SE15入口52通过；初版测试把AMP位模拟置1导致旧模型禁止cleanup，修为核心状态变化负控制后corev3通过，失败日志保留。65源的新双kernel与完整日用运行，未包/未上机；新source不能冒充上一source63设备结果。

mode29主机收口：核心7新负控制/PIN9/WRITE双token/PRM92/SE15入口52/完整日用/新精简完整kernel通过，65运行源hash一致；943B候选/fresh后部USB/Android完成/无owner通过。下一原30分钟独立无声资格，实际GPIO数据链和核心窗口传输结果未取得。

## mode29首轮：遗漏native RAM入口，guard阻断，未接数据脚

实际新SE15 mode35不在旧nativeRAM full0的33/34分支中，走到legacy burst；RAM/系数传输通过但两颗aftercore100，非本模式要求的完整0。PWR10/PWR23300输出/boost仍关闭；data_pin both_off guard含core0条件，因此未claim引脚、零回调、零WRITE，sharedreset与SCK/WS恢复成立。已申请的MAP handle/图/PRM依赖按未知整体结果保留到自动reset，不记完整清理PASS。1,364,712B新鲜deadline/无panic/Android完成34.4°C，整项FAIL。

已修唯一生产变量nativeRAM入口范围为33–35；不是放宽core0守卫。新`cs35l41-wire-platform.py`执行实际SE15 prepare/owner和GPIO/reset代码，仅用受控芯片事务proof输入隔离芯片核心（核心另有完整GENI回归），7项包括native入口选择、旧core拒绝、foreign pin、claim/restore和callback失败；原漏选分支将不能通过pass用例。当前定向host通过，需新日用/双kernel后独立v2资格；首轮失败保留。

mode29v2主机收口：实际owner7项和替换回旧分支的预期失败复现、完整日用及新精简完整kernel通过，65运行源hash一致，943B v2候选/fresh后部USB/Android完成/无owner通过。COREINIT日志由固定请求标记改为实际loaded.native_core_init，旧日志中flag1不能替代零writes/reads的实际证据；v2仍需真实2次coreinitACK和逐颗6读。下一独立无声v2资格。

## mode29v2 实机：接数据脚与核心运行窗口零传输通过

nativeRAM full0实际两次写ACK、逐颗6读、RAM/系数完整验证通过；GPIO124从1/0到c4/0，原机ALT1/8mA/bias-disable，无latch写。每颗核心运行到sample49/HALO2/≥10HB变化后，各11520B零数据/独立token7a1b0001和2得到真实56B V2 DONE；核心回调前后HALO2，heartbeat top6c3→6ea、bottom6c0→6e2。两颗仍依次启动/停止，不是同时。149配置ACK/100样本每颗/6.520156秒/VIO-WDT0；未启AMP/BST/ASP TX或输出。

实际sharedreset后data pin恢复1、SCK/WS恢复，UNMAP0/nativeidle/TLB/unused stream恢复/完整读回，PCM联合关闭/PRM五ACK held0全部通过。197秒正常返回、1,400,886B新鲜deadline/无panic或ADSP失败/Android完成34.7°C。首轮漏native入口FAIL保留，v2仅修分支及实际日志标记，新owner回归复现旧错误并验证修复。

该项只认原机数据脚配置与HALO实际运行期间真实零缓冲区消费、前后核心状态/心跳；零样本不能证明非零波形到达功放、通道/声学质量、持续负载保护或系统App播放。下一默认关闭的输出配置必须从原机模块/DT核DSP PCM source、slot、数字音量/增益、boost参数和原生pup/pdn序列，再准备受控首声，不能现在直接打开音乐。

## AUDIO-OUTPUT-01 / mode30默认关闭输出配置source/定向HOST

从本机原始cs35l41_dlkm的重定位/控制结构/实际set_pdata和set_vol汇编，配合saved DT与原机XML，确定14项有限配置：1000nH/15uF/4000mA对应K1/K2=24/24、slope75/ind0/IPK40；TEMPwarn3、SDOUTHIZ3、RXslots0200/0401、PCM DSP32、DSP RX来源8/9或8、增益code5（原机handset使用，不能等同实际响度）、数字volume index0→6000mask3ff8/value2678与4ms ramp、ClassH tracking1/target0。原module/DT/plan hash与ABI来源留私有source-review，固定公开BSP ABI核对soc_mixer与soc_enum。

运行核心先实际读取两颗PWR1/PWR2/core与本轮完整RAM/coeff证据，再每次masked RMW前后核同状态、完整读回14/颗；28写/2秒上限，错误/部分传输保留、sharedreset最终收容。零AMP/BST/GLOBAL enable、无OTP/key/校准测量写；设置boost参数不等于开启boost。随后沿已验证的GPIO/live-core zero两窗口和清理，不发非零声音。GENI核心9控制+28写失败/主机输入保留位/读回和超时通过，PRM94/实际owner9通过；首版测试未用helper/Werror、错误复用身份读寄存器白名单导致hostFAIL保留，改独立14项只读白名单后corev4通过；PRMunsupported负控制范围修正后v2通过。新66运行源的双kernel/完整日用运行，未包或实机。

mode30主机收口：生产GENI输出配置核心/逐28写失败/PRM94/实际owner9/完整日用/新精简完整kernel通过，66运行源hash一致；943B候选/fresh后部USB/Android完成/无owner通过，下一原30分钟独立输出关闭配置资格，第一声未启。

## mode30输出关闭配置DEVICE_VERIFIED（11月16日00:00收口）

真实两颗14项masked RMW/28ACK/14完整读回每颗，所有保留位与预期一致，最低数字volume index0/gain code5/原DTboost被动参数与DSP/slot配置通过；没有AMP/BST/GLOBAL输出使能/OTP/key写。配置后两次真实零WRITE/V2 DONE、HALO2/心跳变化、100样本/149运行配置、reset/data+SCKWS恢复、UNMAP/nativeidle/TLB、联合图和PRM五ACK held0通过。198秒返回、1,396,874B新鲜deadline/无panic或ADSP失败/Android完成33.7°C。

该项未启升压或扬声器，没有非零声音、响度/音质、模拟电压或系统App音频资格。下一原生mailbox暂停/恢复真实握手，以及VMON/IMON实际保护输入前置；原机set_csplmboxcmd的IRQ/status/mask/13020命令/13004返回均已有原始汇编证据，不能用HALO心跳或软件成功代替命令回执。首次实际输出仍停叫用户。

本批收尾：66运行源与最后通过的双kernel hash一致，完整日用/porting解析/公开文档相对链接/git diff --check通过；Android实际boot_completed1、设备锁与构建/phone owner均无。工作树仍main未提交，未commit/push/更改已装OS，没有新心跳或后台继续声明。下一最小交付为输出关闭时真实原生PAUSE/RESUME回执资格，其次IVsense前置；此时尚不能听测或打开系统音乐。


## mode31实机原生保护暂停恢复通过（11月16日）

源码仍为`main`未提交工作树，基于`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`。在mode30输出禁用/两次livezero基础上，默认关闭PRM31/SE15mode37/profile16只增加原生mailbox协议；单命令100ms/5次1ms poll、独立12写/芯片预算，不放宽149核心配置预算。IRQ2 W1C、IRQ1 W1C后核fresh低，保存/取消mask，写PAUSE1或RESUME2，仅认可真实IRQ与13004状态，完整恢复原mask；不可靠总线不再清理，最终共享reset收容。

实际GENI控制覆盖24个写位置通信失败、陈旧IRQ、错误状态、超时、mask读回、禁用/旧core；原机固件系数输入、实际SE15/PRM平台、日用host及新reduced/full内核通过。初版编译误插入独立output_read以及第二版测试模型缺命令地址白名单已修，失败日志保留仓外。

实机两颗各PAUSE status1/RESUME status0、IRQ301f02（含完成bit200000）、每次poll1、maskffffffff恢复，各12/12ACK。149/149核心配置、100样本/颗、HALO2/心跳、两次独立token真实V2DONE、reset与data/SCK/WS恢复、UNMAP/nativeidle/联合图/PRM释放通过，执行6.622389秒。1,406,534B新鲜deadline，199秒自动返回，独立Android完成见证通过，无panic/ADSP失败。严格本机review已通过；无输出/非零波形/声学/持续负载保护/App播放结论。

下一AUDIO-IV-01：原机module默认DSPRX3=18/DSPRX4=19，ADC widget明确PWR2 bits12/13；只读核对实际运行窗口前后路由与enable。首声仍停叫用户。


## mode32实机IV监测路由与enable前置通过

默认关闭PRM32/SE15mode38/profile17只在原mode31两次livezero窗口前后增加只读PWR2/DSPRX3/DSPRX4，不增加芯片配置写。原机模块`cs35l41_reg`给出RX3=18/RX4=19，原生VMON/IMON ADC widget明确2018 bits12/13。实际GENI错误路由、ADC关闭、中途路由变化控制、SE15/PRM平台、日用host及新双kernel通过；首版测试模型编译失败已修并保留。

设备四个窗口各完整3读：两颗PWR2=3300/RX3=18/RX4=19，输出/boost仍禁用。mailbox实际状态1/0与IRQ/mask恢复、149core配置、两次全零V2DONE、HALO/心跳、reset/pin/UNMAP/nativeidle/联合图/PRM释放通过。1,425,674B新鲜deadline，198秒正常返回、Android完成，无panic/ADSP失败，严格本机review通过。此结果只是监测路由/enable前置，不代表非零ADC、模拟精度或负载保护通过。

下一AUDIO-TONE-01：先在输出关闭下验证固定有限短音buffer实际消费/清零，不扩展其他硬件。首次启用输出前停叫用户。


## mode33真实有限非零数据消费与清零通过（输出仍关闭）

PRM33在真实MAP前，用独占unmanaged reserved64KiB的Normal-NC CPU映射检查完整零preimage，填入11520B/960帧/96kHz/3ch32storage/24logicalMSB有限样本：1kHz、-36dBFSpeak、首尾1ms渐变，第三通道0。全64KiB读回验证后才交DSP；两颗live HALO2/mailbox0/IVguard窗口各送一次68B WRITE并收到实际56B V2DONE。仅在两次完成且功放reset之后，检查完整波形preimage、清零并全64KiB读回，再UNMAP/nativeidle/联合图/PRM释放。CPU写范围仅11520B，失败或部分结果不得重试覆写未知内存。

生成器独立数值检查/10项核心控制、实际CPUadapter7项、实际PRM准备/清零失败控制、日用host与新精简/完整kernel通过，source68/hash匹配/943B包。初版测试根路径和新增成功case分类失败已修，负记录留仓外。

实机prepare与clear各16pages/32768reads/2880writes/verified1，两个真实非零WRITE完成、原mailbox/IV/HALO/心跳/output配置与reset/pin/内存/图/PRM完整清理通过。1,396,537B新鲜deadline，正常回Android并完成见证，无panic/ADSP失败，严格本机review通过。AMP/BST/TX仍off；本结果不证明非零波形已到功放模拟输出或声学。

下一AUDIO-PULSE-01准备现场默认关闭候选，实际首次输出前停叫用户。


## mode34现场首声候选已备，尚未开启输出

AUDIO-PULSE-01的交付是一个等待用户现场听测的候选。默认不开启；PRM34另须`zizhan_audio_attended=1`，SE15执行前再次核对该开关。主机/原机输入/新精简与完整kernel都通过，source69与967B包完全匹配；当前手机Android完成，fresh后部USB-A/无owner检查通过。没有派发首声设备轮。

候选保持两颗顺序执行。每颗原生5行pup patch在GLOBAL上电前执行；mailbox/IV/HALO/保护前置成立后，实际保留区尾部全零10ms预卷使用不同地址与token，真实V2DONE才能进入输出。再次检查实际运行/输出关闭/保护状态、IV路由及原机故障位（sticky+raw mask800281c0），使用固定数字volume index793与既定gaincode5、固定低幅度1kHz10ms渐变数据；boost与AMP逐项写入读回，单次数据等待100ms，脉冲预算250ms。完成或已知失败先关闭AMP再关闭BST并恢复最低音量；不确定总线禁止再发I2C，由实际owner立即共享reset收容，保留未知buffer/lease。GLOBAL关闭后原生5行pdn patch；成功才清零/UNMAP/nativeidle/联合图/PRM释放。每颗输出相关写预算16，不放宽149核心配置预算。

主机证据覆盖实际GENI完整固件/系数链、全部32输出配置写入通信失败、预卷拒绝/播放失败/超时；纯核心进一步覆盖故障位/晚发故障/错误状态和全部事务失败；实际SE15与PRM覆盖双入口默认拒绝、四个独立token/尾部零与首部短音的顺序、预卷和短音等待失败。日用host及新双kernel通过。初版测试的旧双WRITE计数/新成功case遗漏等失败均修复并保留仓外。

首个输出前按用户要求停叫用户；候选准备通过不代表扬声器有声、模拟音质或持续负载保护通过，也没有系统App音频服务。后续设备回执与用户听到什么分开记录。无commit/push/OS安装，未创建后台心跳。
