# CS35L41 保护固件 RAM 受限加载候选

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。接续[RAM传输计划](2026-11-15-amp-ram-transfer.md)，本批目标为原机保护 WMFW 在两颗芯片上的实际 RAM 写入与完整读回；不是保护程序执行或播放资格。当前结果以[STATUS](STATUS.md)和本文后续实机段落为准。

## 默认关闭的模式13

原模式1–12保留。只有 `zizhan_se15_prepare=13` 且实际board/audio冲突检查通过，才读取 `/chosen/zizhan-cs35l41-protection`；属性缺失或长度不是34068字节，在任何映射/时钟/引脚/复位写前跳过。属性复制为worker独占的内存，结束释放。默认包无该属性。

打包入口显式增加 `--cs35l41-protection "$PRIVATE_WMFW"`，只接受原机文件准确大小和SHA-256。文件在私有临时启动包ADT中传递，不进入源码、内核archive或已装OS/Data。内核用独立SHA256实现再次核对整个副本；该实现从仓库MIT SHA256实现转为设备层C形式，避免依赖缺失的corecrypto头文件，并与Python hashlib按257种长度对照。公开源码仅有文件hash和已核对的六段offset/长度/地址事实，不含payload。

同轮safe-off、owned snapshot、两颗35a40/B2/OTPID8、固定B2补丁、逐颗98字段trim和关闭ACK全部通过后才调用RAM入口。要求实际GENI独占配置、输出/boost/HALO禁止；每段前及最后核对原PWR1/PWR2/HALO状态，任一变化立即失败。只允许六段固定RAM目的、原文件4字节payload，无钥匙、系数、DSP或输出控制写。

每颗先写完六段，再按原地址顺序完整读回7469个32位值并逐一比较。总最多14938次RAM写与14938次RAM读；整轮45秒检查、固定循环上限和既有每事务20/2/2ms取消/中止上限。总线不确定后不派任何清理事务，平台最终共享reset；明确数据不符也不重试。记录尝试/ACK、完整段/读回计数、失败chip/segment/offset、耗时、禁止状态及最终reset。日志短行避免已有256字节限制。

## 主机与设备分别判定

生产GENI模型执行真实safe-off→owned snapshot→B2→trim→RAM完整链。合成payload仅用受控hash依赖模拟，不能当真实固件校验；另以仓外原机文件和生产SHA256及独立hashlib对照回放同一核心。覆盖哈希错误/缺失、截断、前置/配置/输出变化、首个读回错误、期限/冻结时钟、每97个事务及首末位置总线失败，禁止失败后继续传输。平台30项控制包括无属性/错长度零映射和mode13复位失败。实际ADT encoder覆盖默认不带payload、错误长度/hash保留原输出及真实输入逐字节传递。

```sh
python3 tools/zizhan/tests/cs35l41-ram.py
python3 tools/zizhan/tests/cs35l41-ram.py "$PRIVATE_WMFW"
python3 tools/zizhan/tests/cs35l41-ram-adt.py "$PRIVATE_WMFW"
python3 tools/zizhan/tests/cs35l41-setup.py
python3 tools/zizhan/mac/check-daily-use-host.py
```

设备前必须新reduced/full kernel、source/input/package hashes及fresh wholeOS/extent/备份/manifest/owner/后部USB-A；临时boot、自动/panic/power-key返回、pinned deadline及Android完成见证、30分钟监督保持。验收须两颗written6/verified6/readbacks7469/state39、总14938attempt=14938ACK、source_verified1、禁止状态不变、无buserror/uncertain/fatal、最终reset和返回通过。缺失/部分/超时不能认通过。

即便全部RAM字节正确，runtime算法ID/base、系数、DSP执行/保护状态与ADSP播放路由仍须后续独立验证；首次实际出声前停下叫用户在场。当前已装OS/UI/helper与回退链保留，无commit/push。

## 主机与候选收口

最终生产SHA256对257种长度与独立hashlib比较通过，实际原机文件完整链、平台30项、ADT真实输入传递和完整日用host通过；最终reduced/full kernel分别通过，源码hash与构建收据一致。早期full因corecrypto头文件缺失失败，改用独立实现；转换时C cast及输出字节截取顺序被主机控制发现并修正，早期日志/收据保留，旧构建不充当最终资格。已准备mode13临时包并逐字节核对ADT私有payload，尚未取得设备结果。

## 首轮实机失败保留（11:18）

最终mode13候选在fresh全量前置下临时启动。源文件SHA通过；第一颗6段7469次RAM写全部ACK，完整读回在首段offset4（第二个32位值）不符，reason14/readbacks2，立即停止，第二颗零写。没有buserror/uncertain，PWR1=0/PWR2=3300/HALO100保持，最终reset。写事务ACK不等于RAM内容正确，不能提升加载资格。此候选未记录expected/observed值，不能猜测为字节序或内存使能问题。

同轮owned snapshot/B2/trim各自通过；11:14:49临时启动、199秒返回，1380383字节新鲜deadline、ADSP13/电池/无fatal/Android完成通过，33.7°C。完整RAM成功review断言失败日志保留，独立失败记录不改成PASS。下一最小诊断为仅首72字节header写与完整packed读回/24位别名观察，记录实际原始值，不重复整固件写或启DSP。

## 独立mode14头部诊断候选

以失败点为依据缩小到chip0首段72字节/18次写，完整采集18个packed值及24个unpacked24别名值。全部仍需同轮初始化/hash/禁止状态，最终复位；零第二颗或剩余固件写。`PASS capture`只表示记录完整，mismatches保持实际数；mode13原完整读回碰错即失败的行为不变。实际核心/独立SHA、平台31项控制通过，新双kernel/日用结果收口后才上机。

## mode14实际头部诊断与新候选

第一颗18次写均ACK；18个packed标量读回中11个不符，非零匹配呈每12字节首32位精确，其余标量位置返回零。另一视图24个unpacked24值全部与同一原机文件逻辑字精确匹配，包括固件/算法头部。这确认首72字节写正确，不能据此推断整固件。原mode13的整固件读回失败记录保持。196秒返回、1485739字节新鲜deadline、ADSP13/无fatal/Android完成通过，35.4°C，最终reset。

下一独立mode15只改变完整packed分组读取：XM/YM12字节、PM20字节一次I²C读，逐原始32位比较；写行为保持。GENI另增限定12/20字节burst API，旧短读上限不变，最多5个FIFO词且完成前必须FIFO具备全部请求词，短数据直接错误。整固件应3618次读事务/14938个32位核对。45秒/状态守卫/失败不重试/最终reset及禁止DSP输出不变。

## mode15两颗完整RAM读回通过（11:40）

实际两颗各6段written/verified、7469个32位逐字精确核对，总14938次写尝试=ACK，3618次分组读，mismatch0，耗时21708655微秒。39次/颗PWR1/PWR2/HALO状态读保持0/3300/100，最终reset，无buserror/uncertain/coeff/DSPstart/output。只确认完整保护程序在RAM中的字节，不等于程序已运行或能出声。

本轮ready/OTP/B2/trim各PASS，11:36:19临时启动、200秒返回，1394874字节新鲜deadline、ADSP13/电池/无fatal/Android完成通过，36.3°C。旧scalar读回失败仍是有效负控制。日志审阅初版因userland前缀拼接漏识别OTP行失败；全部64行实际存在，后版只按准确tag移除前缀，逐字fullmatch/唯一index/独立解码校验保持，失败日志留存。下一先两颗runtime算法身份/内存base，之后准确原机分声道系数RAM写/读回，DSP和输出继续禁止。
