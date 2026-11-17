# SLPI前置：原机cmd-db只读稳定采集

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。目标是取得传感器SLPI资源计划所缺的真实cmd-db，不启动SLPI或推定继承资源票。当前队列见[STATUS](STATUS.md)。

## 真实输入定位与被拒输入

Android只读检查确认 `/reserved-memory/aop_cmd_db_region@80860000` compatible=`qcom,cmd-db`、BE64 reg为 `0x80860000+0x20000`，实际platform device和cmd-db driver存在；debugfs相关路径、/dev/mem/kmem及nvmem导出不可用，没有挂载debugfs或改驱动。现有设备DTS已no-map该区，ADT也传cmd-db-base，Wi-Fi现有读取路径使用同一地址。

原机备份AOP/AOP config A/B四个输入均按原partition manifest核对解压大小/hash，私有保留。六处cmd-db magic命中均为代码/常量邻接，不符合完整资源表header，明确拒绝，不将字符串或signature当真实表。这只说明这些候选不能用，不证明runtime生成表不存在。

## 新默认关闭候选

`zizhan_cmd_db_observe=1` 才启动。严格匹配zizhan板型与ADT base，只映射实际保留128KiB区域为uncached/read-only，两个各128KiB堆缓冲。生产capture核心恰好最多65536次32位读，每64word检查2秒deadline，冻结时钟也有固定迭代上限；两份完整内容相同才调用现有生产 `zizhan_slpi_power_plan`。这不是原子快照，不能声称AOP全部运行状态不变。

成功输出实际9资源的地址/计划值/kind/VCD，以及私有klog中1024条各128B的完整表hex，供host还原/hash/实际parser交叉核对。数据和原始日志留仓外，公开记录只计数/寄存器事实；不发布原机固件或payload。缺数据、变化、超时、解析失败均FAIL，不补默认值或重试读取。成功也明确 `votes=0/baseline_verified=0/slpi_start=0`，没有资源执行器、PAS或固件安装调用。

## 主机与候选验收

实际核心sanitizer覆盖两份匹配/逐字节一致、变化拒绝、无效表、deadline、冻结时钟、关闭/错误参数；调用实际生产资源parser，人工资源表只作控制。实际平台9项控制覆盖关闭零映射、板型/ADT错、thread/map/alloc失败、无效表与成功解析，验证只读map/全部释放/内存不变。成功路径全部1024条hex还原后与人工原表相同，9资源输出一致。完整日用host与新reduced/full kernel通过后才准备独立包。

设备验收需新鲜actual region两copy/65536reads/parser0/9resources/PASSterminal，完整1024条唯一offset还原128KiB并host实际parser匹配，ADSP13/真实电池更新/无fatal或音频，fresh wholeOS/extent/manifest/backup、后部USB-A/单owner、自动返回/30分钟监督/deadline klog及独立Android完成。任何partial不算通过。

## 当前边界

定向主机已通过；完整回归/构建与实机按实际收口。即使表采集成功，开机固件继承APSS票仍没有可信baseline，不得以idle TCS或0默认替代；SLPI认证/启动/传感器API仍待独立实现。首次声音、外屏/触控/自动切屏和现场权限GNSS保持边界，无commit/push或OS/Data推广。

## 首轮实际格式拒绝（09:32）

实际保留区两份copy一致、65536个word读取完成，但生产parser返回Corrupt(-2)，资源0/FAILsnapshot，未导出payload，不能认真实资源计划通过。196秒返回、1393100字节新鲜deadline/ADSP13/电池/无fatal音频/Android完成通过；零票/启动。稳定不等于合法表，原失败保留。下一单一变更是在稳定但parser失败时也允许私有raw导出，保持FAIL与零计划，取得实际字节后修具体格式差异；不改变硬件读取/资源执行范围。

## 稳定拒绝原表留证候选（v2）

唯一变更允许two-copy稳定但parserInvalid时也输出私有raw1024记录，终态仍FAIL/resources0，Changed/Deadline不导出。平台invalid分支检查完整1024记录且失败语义不变，host/日用/新双kernel通过。v2包911B已备，fresh前置下独立轮监督中；目标只为还原实际格式，不能认合法资源计划或启动资格。

## 日志边界修复（v3）

v2实机稳定读取但128B长记录被日志层截断，完整重建断言失败，原始日志和失败记录保留，不生成假完整表。v3只把每记录载荷缩为64B/总2048条并加whole-table FNV64，host以256B日志buffer回放成功/Invalid路径均完整重建；日用和新双kernel通过。实际parser仍FAIL，不认合法计划。新包911B/fresh单轮监督中，只有全部唯一offset与整表proof一致才保存actual128KiB供格式分析。

## 无损实际表与类型修复（09:55）

v3完整2048条/每64B及整表FNV64匹配，实际128KiB已私有还原并SHA-256留证；两份硬件copy稳定，资源表version1/magic正确。实际表含两个ARC分区及BCM/VRM分区，不能把先前固件magic当表。09:49:25临时启动、约196秒返回，1827449字节新鲜deadline/ADSP13/电池/无fatal音频/Android完成通过，09:55:56独立Android/zizhan/boot_completed1、34.9°C。

实际parser失败根因已定位为ACV mask资源aux width=0；普通带宽资源才需要非零width。修复只允许已知ACV mask类型的width0并完全跳过带宽缩放，普通5种BCM的zero-width仍逐项拒绝；unit/长度/VCD/地址/别名/边界检查保持。新增生产负控制与原资源/lease/capture控制通过，实际表调用修复后的生产parser得到9资源计划，私有记录携带snapshot/parser hash。此为真实表DEVICE_CAPTURED加HOST_VERIFIED计划，不等同于设备执行计划。

实际ARC corner索引由本机表选取，BCM单位/VCD也来自这份表；所有票值只是计划，`plan_executable=false/hardware_votes=0/baseline_verified=false/slpi_started=false`。旧设备轮终态的parserFAIL保留，不倒改为PASS；修复只在主机使用实际输入验证，新双kernel独立收据收口。没有为相同原表重复启动设备来“刷绿”。

传感器下一阻塞收窄为APSS继承资源票的可信owner交接，已不能再写“缺实际cmd-db”；未知baseline仍禁止lease/PAS12启动。本轮完成具体输入后转独立音频B2/易失trim/readback/relock候选，折叠静态输入已实机通过，机械事件现场。

最终ACV类型修复的生产资源/capture回归、完整日用host和新v4精简/完整内核均通过，33项runtime/build输入hash与receipt收口一致。无后续phone轮或待poll任务，旧设备parserFAIL与日志截断仍保留；真实输入host通过不能倒改旧设备结果。
