# 内屏状态栏与传感器电源交接前置

源码基于`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`的未提交main工作树。当前队列以[STATUS](STATUS.md)为准；本批按用户新授权的两小时无人值守推进，首次音频输出、外屏和需要用户动作的测试继续停在现场边界。

## UI-STATUS-01：原生内屏状态栏候选

用户报告电池在左半屏。既有截图中右侧图标组确实没有靠近整屏右边缘；日志记录实际native2160×1916、logical1080×958、scale/nativeScale2。截图缩小或错误布局都不能单靠一个图标猜坐标，现阶段记录为用户报告与待验布局异常。

原适配的UIKit/SystemStatusUI provider是Pong1080×2412/scale3专用的phone类；其中nil-screen默认发现路径可能继承这个候选，而实际zizhan内屏不匹配它的物理尺寸。原始22H374 SystemStatusUI元数据显示存在独立原生Pad provider、原生setup/sizeUpdated及左右region约束。候选仅在显式`zizhan_statusbar=1`、实际L18硬件服务匹配、真实屏幕尺寸与scale2匹配（或原生nil默认屏发现）时，选择单独分配的原生Pad子类，并提供referenceScale2；保留原生初始化、region布局、时钟与真实电池数据。不会直接移动电池、修改电量或把小fallback对象改isa。

对已经建立的已知phone/fallback provider，通过原生reset/prepare重新发现；UIKit与STUI对象分开。`zizhan_statusbar_diag=1`才遍历最多256个视图、深度16，记录真实状态栏/电池frame与bounds。外部或未知屏幕、错误scale、无硬件服务、无开关保持原路径；nil发现不重入mainScreen。

`python3 tools/zizhan/tests/statusbar-panel.py`的10项生产策略控制通过，实际iOS SDK编译/签名通过。旧prepared编译命令指向已不存在的Xcode目录，初次编译失败已保留；改用实际xcode-select与SDK后通过。v2还覆盖原phone provider早建后的原生重建。已准备独立UI-only镜像与原OS完整回退，仅改变UI库，Data未离线改写。主机通过与prepared不等于实际布局修复；设备实验需完整几何、可判读截图、无系统崩溃、正常返回和独立Android完成见证，随后恢复原OS并完整读回。

## SENS-BASELINE-01：实际完成请求的软件记录

SLPI的实际cmd-db已取得、生产资源计划和有界整组RPMh接口已主机验证。剩余阻塞是资源所有权及开机固件留下的APSS票值；不能从空闲TCS、内存零值或一条历史请求推断继承baseline。

新增`ZizhanRpmhReceipt.h`保存共享active owner在本次启动实际收到完成的最后请求。两个生产owner均在同一powerLock下记录成功单请求/完整batch；查询也在该锁下，固定64地址、整组原子预检、保留sequence与真实ACK0。缺失地址返回unknown且清空输出；容量或序号耗尽、失败/不确定记录拒绝后续观察直到reset，不静默逐出旧记录。查询没有MMIO写，也未接SLPI启动调用者。

接口明确命名completed request，**不是**继承APSS baseline、AOP聚合票、当前硬件值或启动许可。原机固件票未知的资源仍不得发SLPI lease/PAS12。此批仅补来源可追踪的交接输入，下一需要一个能说明所需全部资源的可信owner交接方案；已知完成记录只能作为该方案的证据之一。

`python3 tools/zizhan/tests/rpmh-receipt.py`覆盖真实ACK0与unknown、更新/组序号/边界、容量满/无逐出、整组不足不部分记录、非法输入及永久拒绝。`python3 tools/zizhan/tests/slpi-rpmh-batch.py`执行两个实际owner API分支，检查真实完成后记录、查询锁、不确定后的拒绝；MMIO由受控夹具提供，不称设备ACK。完整日用host及新精简/完整kernel均通过。没有SLPI认证/启动、传感器数据或API实机成功结论。

## 当前交付边界

音频首声候选仍封存，不随新kernel源码自动继承资格；夜间不派首声包。不提交、不推送。状态栏设备结果和系统恢复记录按完成后追加，不能用主机构建或一个已派发命令记PASS。


## 首轮设备结果：原生Pad选择已观测，图像验收FAIL

隔离UI候选安装前后均完成wholeOS/manifest/extent/备份和实际UI/helper身份核对。临时boot取得1,351,255B新鲜deadline，日志三次明确SystemStatusUI nativePad/scale2/defaultscreen0，实际UIScreen仍native2160×1916/logical1080×958/scale2。未见功放启动、panic或ADSP失败，独立Android完成见证通过。

没有PNG，不能认电池位置修复。具体原因是测试参数step4产生540×479=258660像素，超过截图工具既定130000像素预算，实际日志明确SKIP；不是截取成功，也不把它直接解释为画面未显示。UIApplication视图遍历只找到legibility外层容器，尚缺实际STUI子树几何。

已恢复原weather UI，重新完整读取wholeOS/manifest/extent确认PASS，Data无离线改写。下一有区别的诊断轮：截图step8/较早采样，并在真实STUI layout调用原生实现之后读取其自身子树（最多两次、128视图/深度12）。保持同一布局策略，不重复未改动失败。候选source实际SDK编译/签名通过，设备结论等待独立完整证据。


## 第二轮：整宽容器已观测，完整视觉验收仍FAIL

第二轮保持布局策略，只改step8/较早采样及真实STUI自身两次子树诊断，独立UI候选编译签名/有界安装与wholeOS读回通过。实际STUI初始化frame600×50，随后frame/bounds1080×20，与真实logical1080×958一致；三次原生Pad选择仍观测到。前两次布局尚未生成可记录的电池子树，不能把外层容器宽度提升为图标布局正确。

取得1,689,201B新鲜deadline并正常回Android。PNG存在，但解码明确只有224/270列（右侧log chunks缺失），没有完整右边缘，因此完整视觉验收FAIL；不推广该UI。下一先恢复原OS并完整读回，推进独立传感器资源查询。若继续UI，只能用较晚的实际STUI子树和预算内、能完整还原的截图补证，保留本轮负结果。不能把缺列图补齐为推测像素或称电池已在右上角。

## 新默认关闭的9资源查询候选

`zizhan_cmd_db_observe=2`只延迟一次10秒等待已有开机owner，再稳定读取原机cmd-db作为资源地址/类型输入，查询9个资源的本次软件完成记录。每条保留observed/request/sequence，汇总known/unknown，明确atomic_snapshot0、inherited_baseline0、votes0、slpi_start0。该模式不再次导出完整raw表，mode1原行为保留。实际平台已知/未知/混合与Invalid拒绝控制、日用host和新双kernel通过；实际9条观察尚待独立设备轮。此实验的目的是把未知交接缺口落实到资源，仍不允许据此启动SLPI。


## 第二轮补充：原始完整顶部图像的局部位置资格

第二轮原始日志逐行检查发现：缺块位于部分下方行，**顶部连续140行的每一块均完整**（270列，每块原始RGB字节长度严格核对）。全帧解码器为保留完整矩形而裁到224列；这不意味着顶部右边缘也不存在。仓外可复现工具只拼接实际完整的顶部140行，不补任何缺失像素，原始像素按最近邻放大供查看。

实际查看该完整宽度的顶部图像，绿色电池位于整屏最右上约97%处，左侧时钟/状态图标仍显示；结合真实STUI1080点容器，**原生Pad候选的电池靠右位置可记DEVICE_VERIFIED（限定已捕获顶部）**。此前整帧FAIL记录保留，补充review单独记录局部图像资格；“右边缘全不可验”的初步解释明确由逐行原始数据纠正，不声称整个主屏所有行完整或设备所有场景都修好。原UI已恢复并完整readback。

下一UI-STATUS-02候选保留同一布局，只让实际zizhan内屏默认使用原生Pad、显式`zizhan_statusbar=0`回退；无真实硬件服务或其他screen仍走原选择。诊断默认关闭，默认路径候选已实际SDK编译/签名并准备独立UI镜像，尚未上机。下一需不带enable参数的独立静音资格；较晚实际STUI子树与较低采样率完整截图分别验证。

## 9资源查询实机：3已知、6未知

独立原weather UI静音轮实际2copy/65536reads/parser0/9资源通过，无raw表重复导出。XO最后完成请求3/sequence29，LCX7/sequence1，LMX5/sequence2；MC0、SH0、SN3、SN0、CE0、ACV均unknown且输出value/sequence清零。9行唯一资源/地址、known3/unknown6与严格review一致。

完整新鲜deadline、ADSP13、无panic/音频/ADSP失败、正常自动返回及独立Android完成见证通过。本结果是本次软件owner已完成请求的实际观察，非继承baseline/AOP聚合票/硬件当前值，不把未知0当真值，也未启动SLPI/PAS12或提供传感器数据。下一资源交接缺口收窄到6个带宽资源的继承owner状态；已有3条也仍需与完整ownership方案一起验证，不能直接当启动授权。

## 默认路径第三轮：接手补齐已完成结果

下午接手时没有设备cycle/installer/waiter或锁；本机交接文字停在v3已安装、待启动，但实际工件还包含后续完整cycle和回退，故先协调记录，没有重复设备实验。`statusbar-inner-1116-v3`于02:04实际临时启动，196秒返回，取得1,411,486B新鲜deadline；独立Android完成见证PASS。启动参数没有`zizhan_statusbar=1`，仍有只读诊断和audio0。三次实际nativePad/scale2选择、STUI600×50→1080×20及晚期真实电池子树均已记录；子视图frame属于局部父坐标，不能直接当整屏绝对坐标。

`python3 tools/zizhan/mac/shot-integrity.py logs/statusbar-inner-1116-v3.txt`对完整135×120采样帧的16,200个像素和PNG三倍最近邻副本逐像素核对PASS，无缺失像素或补图。实际查看完整画面确认绿色电池及右侧状态组位于整屏右上，左侧时钟保留。无panic、ADSP失败或音频启动。该证据通过默认布局在本轮主屏上的资格，不证明高分辨率文字、所有App/方向或长期运行。

原UI已于02:09–02:10恢复，wholeOS/manifest/extent完整读回PASS；下午重新执行只读安装核验，原weather OS、真实manifest、root/klog extent、备份清单及Android完成均PASS。当前安装仍为原weather UI，已验证候选保留但未默认推广。旧v1预算SKIP、v2整帧FAIL及v2顶部局部PASS原样保留。本次修正的是漏记结果，不将新核验时间冒充昨夜执行时间，也没有新增安装、启动或声学测试。
