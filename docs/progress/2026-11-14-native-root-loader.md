# 整夜迁移第二批：根卷链接、原生loader与闭包分析

日期：2026-11-14（UTC+8）。本批接[core与系统输入](2026-11-14-native-core-and-inputs.md)，继续[整夜目标](../plan/2026-11-14-native-overnight.md)，没有将host里程碑升级为原生设备资格。入口设备源码仍为`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`加本轮未提交修改。未boot、安装或写手机；实际安装v23、canonical OS370和旧Data保持。

## 已取得的结果

- 原生软件AES单独编译，NativeAES继承的181个PAC方法签名匹配，归档34,424字节、SHA-256 `183ce5510bb7700d85fcb6d0113acc6641e79d86a9be81f79cf248234eceb5df`。软件key范围保持，不提供硬件key或伪造安全服务；初始化仍等真实父类并执行原实现known-answer checks。
- `build-zizhan-native18-core.py`新增host root-link阶段，链接真实zizhan UFS与软件AES，打开root/Data/SystemOS相关hooks，保持Nothing电池/音频/Glyph/基带、Wi-Fi和GPU关闭；独立AES注册不再依赖PongPower。root-link-v3实际完成DEVELOPMENT/arm64e/size/chained链接，strip18,127,464字节、SHA-256 `514e272f91b689b2e02719c0b66c680660b74734433c894e5dd3624892d517ac`；unstripped21,915,784字节、SHA-256 `344a403a25197d3aec9b7772f3796e1d0cbecd478ad819530b2655cca681a657`。仍采用host-only UUID，无stock-kext集合，不是可启动内核。
- 新`native18-loader.py`从本机维护的loader复制生成实验source，仅移入固定上游的arm64e/fileset parser和真实PAC ISA检查。保留zizhan128MiB panel pool、extra RAM ownership、失败日志、MMU-off trampoline和UEFI自动返回。native-loader-v2完整EFI编译通过，34,816字节、SHA-256 `6a652d2572ddbacac81d1bf966d00f499292d362ec674cb59444f97333f538d9`。没有覆盖默认EFI或固件FD，没有加载/启动。
- `tools/zizhan/tests/native18-loader.py`对生成loader中的原始`ParseKernel`运行ASan/UBSan：legacy arm64、arm64e capability、有效合成fileset接受；缺kernel、entry越界、重复kernel、foreign subtype、segment溢出和短header拒绝，9项通过。合成fixture不执行、不当作boot candidate。
- 新`native18-userland.py`实际编译原生22G100 bootstrap/UI，strict codesign与4096页大小通过。init34,592字节/`efebe9a782d21b8055e898ab4eaa476db6eeabaf42372d5e828d0d9725e8bc24`；UI171,008字节/`8d95e2aa8f66465118fa5755e39a011c2e08afdd766360ca7d74fab179ff96f8`。用匹配只读OS/SystemOS准备服务目录、structural launchd task配置和cryptex链接overlay；没有把overlay写入OS或Data，未部署Metal/固件。

## 失败、修正与未完成

软件AES第一次因缺XNU external corecrypto头失败，第二次缺libkern私有头，补齐真实源头路径后第三次通过；未编造头或符号。root-link-v1将AES声明误置电源宏下，v2修正声明后发现真实UFS依赖`pong_dump_waiting_threads`。本机已有的0005诊断patch中提取首个有界waiters函数，新增`patches/xnu-ios18e/0001-zizhan-root-waiters.patch`；未引入后续周期sampler或noop，v3链接通过。

实验loader首次copy未包含`PongAutoReset.h`相邻的共享`BootResetCore.h`，编译拒绝；补齐本机原header后v2通过。所有首次失败目录与日志保留。生成代码不是另一份长期板级源码，变换边界和来源hash均在receipt，固定参考checkout保持干净。

原生stock-kext导入仍未闭合。`native18-closure.py`把本批原版kernel、实际source kernel及父类输入绑定到具体hash/构建receipt，执行固定原生指令/PAC匹配和imports-only计划；它不输出集合、不提供allow-missing/stub。

| 配对 | 缺少的唯一kernel import | 结果 |
| --- | ---: | --- |
| root-link-v3直接匹配、三个native父类 | 781 | INCOMPLETE，无候选。 |
| 另建真实RELEASE符号参考，加唯一PAC-shell/name匹配 | 613 | INCOMPLETE，无候选；不能把匹配数增加当作可启动证明。 |
| 当前带返回链的root-link-v5，加4KiB LTO命名 | 442 | INCOMPLETE，无候选。 |
| 增加16KiB非LTO、LTO两个仅命名参考 | 435 | INCOMPLETE；收益有限，不能继续不变输入重复配对。 |

默认闭包含APFS、IOStorage、IOHID、IOGPU及它们的实际依赖，没有删除硬件依赖伪装闭合。剩余import分布、原地址、source配对及矛盾拒绝记录在本机JSON。新增RELEASE/LTO命名参考仅帮助识别原始函数，运行时候选继续DEVELOPMENT；不能以reference地址填充最终内核。LTO首轮对未生成且随后不用的`all-kpi.exp`提前读取导致失败，适配层仅在该命名模式跳过这个无用读取，仍从真实reference符号导出集合。

## 正在继续的下一步

1. root-link-v4的缺失弱导入揭示最小阶段没有物理按钮驱动和诊断sampler；生成适配层明确省略这两个可选消费者，未提供假函数/硬件。root-link-v5返回链已完整链接：真实watchdog/deadline、klog、原生panic输出后的`pong_panic_recovery_hook`都存在。strip18,129,296字节/`b61b7ddccd50202e5e1501507e04994cc14603edfac6729fdf80c7c9f4d687d8`，unstripped21,931,416字节/`8215aaee4ead2d5f016421686b49d25f965c2e9c8e9d577762c33889500b4075`。仍未boot，不声明设备复位新资格。
2. LTO及16KiB命名参考已完成，运行目标保持4KiB DEVELOPMENT。接下来对435个缺口按代码/常量/数据区域拆分，用真实结构、消费者和独立源代码证据命名；仅扩大匹配覆盖不足以证明正确性。三项默认vnode操作已经过完整字节一致、无PC相对引用/调用、唯一命名与VOPFUNC PAC契约核对，生成本地anchor，供下一配对验证；不是以相似函数代换。
3. 原生bootstrap/UI、同版OS/SystemOS/AppOS的服务及namespace overlay已准备，71个boot/518个on-demand任务，无missing core labels，13个cryptex链接。旧共享报告中22H374文字标签已按真实输入修正为22G100，结构化task配置变化保留来源；不是沿用旧版本指令偏移。
4. 已在本机建立独立可写OS副本和空白Data副本：Data取自已只读确认根目录为空、既有Darwin24可用的模板，保护原模板不动。依据`apfs.util`文档选择该新镜像的实际APFS容器重新生成UUID，并在重新只读附加后确认不同；首次误选镜像内EFI分区无效、UUID未变的结果也保留，不能以utility返回码代替UUID观察。新Data仍为空，尚未seed/打包/写手机。三个原生只读挂载和Data临时附加均已卸载。手机约387GiB可用只是容量观察，没有分配新pinned文件或改旧root。

09:00前持续按整夜计划推进，遇具体缺口改假设或继续独立工程，不把本批结束当整夜目标完成。当前运行句柄、挂载、日志和唯一下一动作以私有handoff最新顶部为准。
