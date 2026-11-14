# 原生最小板级完整链接：Wi-Fi、双电池与背光

日期：2026-11-14（UTC+8）。接[Metal/根卷](2026-11-14-native-os-gpu.md)和[应用种子](2026-11-14-native-app-seed.md)。目标仍是MIG-01完整候选与真机验证；本批没有phone boot/write，源码为`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`加dirty设备层适配，未commit/push，无新CI。

## 维护中的zizhan Wi-Fi原生化

`native18-wifi.py`在隔离source overlay改编译target，选择实际维护中的zizhan PHY、时钟、rail及PCIe profile、Linux7.0.14 ath11k/MHI和干净的共享Ath12kWifi station/shim `f6bf561f92cc42761bdd417fce1d53fe1db9477c`。不替换成Pong硬件表，不传凭据或测试自动关联配置；固件来自本机保留输入且只进入私有归档。

第一轮编译通过C对象但缺本机LLVM-nm入口，固定显式工具后完成归档；随后检查真实对象头发现纯固件汇编未加`-mkernel`，导致归档混入用户态arm64e ABI。第三轮修正真实汇编命令，44个对象全部`MH_OBJECT`、kernel arm64e subtype `0xc0000002`，不是通过改header伪装架构。真实归档结果：

| 产物 | 字节 | SHA-256 |
| --- | ---: | --- |
| ath11k/station/shim | 643,944 | `2e51a85368d39e2558b452ba23fb5f9e0decceb3ba1d3f031da6806facde7bb9` |
| zizhan板级/网络/实际固件合集 | 26,358,304 | `a3ab9d11ade079fdfaa0db2bef4930ed3ff2b09ce257a7b92337e39cb3953081` |

使用真实编译的`PongWifiNet.o`核对六个网络类的父类PAC槽：Pong80211UserClient/IONetworkStackUserClient各186，IO80211Controller/IONetworkInterface/IO80211Interface/IONetworkStack各168，全部匹配当前22G100父类。归档链接不证明radio、MSI、认证、DHCP或联网。

先将完整真实Wi-Fi归档和optional worker注册接入已有UFS/AES/GPU/FST2内核，保留其UFS readiness、radio/DMA失败门槛及真实RPMh/SMMU owner。独立root-gpu-input-wifi-v1链接通过：strip44,703,368字节，SHA-256 `cd907bd9b529c3f591418e50b615aab22dff3fe7618b6d9d6cf7e678cb504904`；仍host-only UUID，无stock-kext集合，不boot。

## ADSP、双电池与背光

`native18-power.py`只选择维护中的zizhan SMEM/SMP2P/GLINK/MDT/QMP、实际固件reader、ADSP、双电池协议/服务、背光和power注册。`PongAdsp`保留本机0x9fd00000/0x3100000 carveout与RAM ownership拒绝；不使用Pong另一板的预留区。power注册只省略本批未选择的audio-start consumer，未补假音频函数/功放profile；音频原人工前置保持。

真实源码编译并保留原late-metaclass注册处理后，AppleSmartBattery的169个IOPMPowerSource继承PAC槽全部匹配。首轮validation调用参数类型错误保留，修正传真实父类路径后完成归档：118,048字节，SHA-256 `e1e86e150d424a4f9bdce0a7bf87166bc7b8c17aa80eba78b3b1293ee362f293`。未执行ADSP/PAS、背光DCS或电池读取。

`build-zizhan-native18-core.py --wifi ... --power ...`验证实际archive/source/profile身份，独立注册真实ADSP/电池worker，并选择原有MiniFramebuffer背光消费者；共享RPMh来自维护Wi-Fi owner，不拉入外机power/audio完整配方。root-full-minimal-v1已完整链接真实UFS/软件AES/GPU/SMMU/FST2/Wi-Fi/ADSP/电池/背光与有界panic/deadline返回。它仍是DEVELOPMENT/4KiB/arm64e、host-only UUID；下一步按该新kernel重新派生类表和导入配对，不能沿用旧kernel绝对地址。

完整最小板级内核strip44,765,120字节，SHA-256 `5cdb32577710bc88fca443d459045f3cd54a3b4f48d2cb4c8158129bef7fc7e5`；unstripped48,571,456字节，SHA-256 `abc06f5571ff2d322d474b272755b0feb3a26280b4cc504ef1195204d4480c1e`。对该实际新输入重新派生149/175个完整类表、62个vnode描述符和三默认操作，首版缺374项。

## 原生板级用户态及实际固件

从连接中的zizhan Android通过只读cat取得26个ADSP mdt/bNN文件，逐文件与手机端SHA-256一致。按真实ELF program header合并并重新核对所有有文件数据的段，得到22,802,856字节，SHA-256 `bbc091321d5752ef0b8aa0e14ce2b3391b064fe91d11ba3722a20edd33e2e791`，load跨度0x3100000。此为实际可重定位固件输入核对，不是固定原paddr能直接覆盖Darwin内存的许可；运行时原有carveout/RAM ownership/PAS拒绝保持。未写手机、未启动DSP。

`native18-board-runtime.py`只选择固定原生source的真实SC/MobileWiFi bootstrap及该固件，未选择上游另一板audio/modem/全局服务策略。实际helper为arm64e user ABI、strict4KiB签名；它只在真实en0/IO80211服务存在后配置native SC/请求原生manager，并保留超时。worker与固件已进入独立本机OS，结构化launchd cache仅增加对应job；新readonly重挂核对hash/mode/root owner/签名及既有bootstrap、Metal、SystemAppMigrator均通过，镜像已卸载。未宣告Wi-Fi或电池硬件可用。

## 导入身份补充

旧root-GPU输入的`PE_parse_boot_arg_str`猜测被排除后，进一步通过已配对`PE_init_platform`初始化的完整六指令字段写入，以及独立已配对`arm_vm_prot_init`对同一base的使用，确认实际`PE_state`。完整stock参数搬运/bootArgs accessor与native保存恢复/PAC return-check/调用目标核对后，只加入实际传FALSE的`PE_parse_boot_argn`包装。

分别把stock初始化中的bootArgs字段偏移改动，以及把FALSE改TRUE的两个本地负控制，均使对应检查拒绝并不输出anchors。`gpu-closure-v4-bootargs`缺口从352降到351，仍INCOMPLETE/no collection。新完整板级内核必须重新验证这些证据。

进一步把完全相同的local load/store leaf纳入严格完整字节核对：不删除读写，不改变字段偏移，不容许未解析的PC数据/外部分支。它们可能有不同源码名字，只有完整输入/指令/内存副作用相同才可作为实际导入实现；这不证明其源码名字就是原符号。改变gate类型初始化常量的负控制拒绝该项。

为避免等价实现的名字变成其他函数的错误身份线索，生成mapper只在全部图/数据推断结束后才加入这类最终导入绑定，不将其反馈到call graph。对新完整kernel重新核对得到46项完整操作等价实现、10项元类alias和1项bootargs包装；`full-closure-v2-exact-final`缺口降至317，仍无集合。PAC shell相近但字段偏移不同的独立review扫描得到0项，未忽略偏移或新增绑定。后续仍按真实函数/数据契约继续。

本批证据在私有`native-wifi-v{1,2,3-kernel-abi}`、`native-power-v{1,2}`、`root-gpu-input-wifi-v1`、`root-full-minimal-v1`、`bootargs-negative-*`及`gpu-closure-v4-bootargs`中。源文件/ABI/编译结果与设备资格严格分列；完整候选UUID、stock-kext闭包、独立Data安装与回退契约成立前不启动手机。
