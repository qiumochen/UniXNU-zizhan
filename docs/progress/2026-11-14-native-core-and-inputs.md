# 原生迁移第一批：系统输入、arm64e core 与存储祖先

日期：2026-11-14（UTC+8）。接 [前置与源码恢复](2026-11-14-native-alignment-gates.md)。入口权威设备源码为 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`，本轮新增脚本/配置及文档尚未提交；原始源码快照、命令、dirty状态、产物hash与完整日志保存在仓库外。本批为 MIG-01 的主机交付，**没有派发boot、安装或写手机，不能登记原生系统设备启动通过**。

## 已交付

1. 实验性输入选择 `configs/zizhan-ios18e-inputs.toml` 固定原生源码、prelink kit 和 iPhone17,2/22G100 的 Apple restore identity；没有切换 `board.toml` 或当前默认22H374配置。
2. `tools/zizhan/mac/native18-inputs.py` 读取匹配的 Customer Erase Install identity，通过有界HTTP range准备选定成员，支持大stored ZIP成员的并行断点续取、严格206/Content-Range、大小/CRC校验及本地SHA-256收据。拒绝把Apple输入放进公开设备仓库。脚本没有ADB/fastboot入口。
3. `scripts/build-zizhan-native18-core.py` 在新目录生成隔离脚本overlay，保留固定上游源及现有构建树；针对当前Apple silicon主机修正两个Intel-only host guard，交叉目标仍为arm64e。用host-only随机卷UUID，关闭存储/GPU/Wi-Fi/音频，构建只作core兼容性控制，不输出可上机镜像。
4. `scripts/native18-storage-abi.py` 从同版stock kernel、公开IOStorageFamily-323和真实arm64e参考对象重新推导PAC父类，检查zizhan UFS子类继承签名并编译归档；`--parent-interfaces` 同时推导GPU/AES父类的host接口元数据。参考对象和绝对地址仅作本地构建输入，不公开、不执行、不代替真实设备服务。

## 固定输入

| 输入 | 本批身份 |
| --- | --- |
| Pong原生源码 | `0ee4243a101eac10a77cbc8b7b8ef77ae8ec93d7`；root tree `9beca44b35d20904ea493ea02b75316b7108f255`，15个驱动按gitlink获取；恢复checkout干净。 |
| XNU | `43a90889846e00bfb5cf1d255cdc0a701a1e05a4`，11417.140.69；libplatform/libdispatch/AvailabilityVersions/IOStorageFamily的原pin均与本机现有只读参考源相同。 |
| XnuPrelink | `440589b0c31625f17edf363e24b3c82c51c70bb9`，用于stock fileset读取与原始分支适配。 |
| Apple restore | iPhone17,2、18.6.2/22G100、d94ap、ApChipID0x8140、ApBoardID0x0e、Customer Erase Install (IPSW)。这是restore选择，不是SM8475硬件身份。 |
| 工具 | 当前Xcode的iPhoneOS27.2 SDK，Python3.15；目标arm64e-apple-ios18.0。主机架构/目标架构分别记录。 |

## 实际结果

### Apple输入

匹配identity后成功取得以下成员；OS/SystemOS/AppOS最终均按ZIP大小和CRC核对，并保存本地SHA-256：

| 成员 | 字节 | 状态 |
| --- | ---: | --- |
| KernelCache IM4P | 20,416,624 | PASS；解压为68,419,584字节、arm64e subtype2/MH_FILESET。 |
| DeviceTree | 63,740 | PASS；仅作原生restore元数据，不拿它覆盖zizhan硬件DT。 |
| StaticTrustCache | 78,673 | PASS。 |
| OS AEA | 7,180,648,448 | PASS；解密并以只读方式挂载，SystemVersion明确iPhone OS18.6.2/22G100。 |
| SystemOS AEA | 1,677,721,600 | PASS；解密并只读挂载，SystemVersion同为18.6.2/22G100，含arm64e分卷shared cache。 |
| AppOS DMG | 16,777,216 | PASS；已准备，未部署。 |

kernelcache SHA-256为`f9be85ea9e2e6d802e767067037b1504e15460aa0d0b777b932559f0be7c59fa`。AEA使用其公开FCS链解密，密钥没有打印或写入公开仓库。两个只读挂载检查结束后均已卸载。下载、解密和正确版本不代表系统已按zizhan组装。

### Core构建

上游原配方第一次在驱动host guard处拒绝Apple silicon，尚未尝试该驱动编译；负结果保留。新增wrapper仅在生成overlay允许Darwin arm64/x86_64主机，不伪造`platform.machine()`，保留真实报告与arm64e编译目标。

00:30:35–00:31:27实际完成DEVELOPMENT、`--chained --optimize-size --phase core`构建，返回0。strip产物18,040,840字节，SHA-256 `dc499ea529fab14cf2d8f34b7230ff75d028f8d1b18b1043f230c0cffc47b10a`；未strip产物21,840,488字节，SHA-256 `b9b2e6f506b66b7ca9353b7c1c98930d00285cdf02da2140881a53b0e22f320f`。Mach-O subtype包含arm64e capability，filetype为MH_EXECUTE；fileset闭包尚未生成。链接仍有原配方的segment-order/缺可选目录warning，返回0不隐去warning。

core含基础CPU/平台源，但**不是已完成zizhan板级适配的内核**。它没有存储、GPU、Wi-Fi、音频，没有stock kext闭包，配置中的UUID明确是host-only，不可将该文件交给fastboot或作为设备内核。

### 原生存储及其他父类

首次zizhan UFS编译因漏传XNU `EXTERNAL_HEADERS` 而找不到`corecrypto/ccsha2.h`，失败日志保留。补入同一core树的真实公开头路径后重新在新目录编译通过，未采用stub。

| 父类/子类 | 方法槽位 | 本批结果 |
| --- | ---: | --- |
| IOBlockStorageDevice | 190 | 与stock22G100的PAC key/diversity/address-diversity及真实类注册匹配，零签名差异。 |
| zizhan PongUfsBlock | 190个继承槽位 | 使用当前设备仓库真实UFS源码、arm64e目标编译；继承PAC签名匹配。保留zizhan自己的userdata边界与已有transport。 |
| IOAESAccelerator | 181 | host接口参考对象与stock父类签名匹配；不是AES驱动或设备密钥资格。 |
| IOAcceleratorES | 168 | host接口参考对象与stock父类签名匹配；不是A730驱动完整编译或渲染资格。 |

最终v3的UFS归档为107,568字节，SHA-256 `3bfcf9fd24cc37f110a316a8e3b881df08b1aa4aaa0a2343956efa6c153e35f2`；真实IOBlockStorageDevice原始方法以PAC安全branch/GOT适配，未复用22H374固定地址。尚未链接进完整内核、未挂载原生根卷、未验证读写/同步/持久化。方法签名匹配也不取代对象生命周期、整个内核ABI或硬件验收。

## 复现入口

源码恢复和15个子模块固定版本见前项记录；`NATIVE_SRC`指本机恢复的精确checkout，`PRELINK_KIT`指固定XnuPrelink checkout。输出目录每批使用新名；输入准备可按range收据接续。所有Apple/构建输出位于公开仓库外。

```sh
python3 tools/zizhan/mac/native18-inputs.py --unixnu ../UniXNU \
  --out "$ZIZHAN_SRC/work/native18-inputs-22G100" --decode-kernel
python3 tools/zizhan/mac/native18-inputs.py --unixnu ../UniXNU \
  --out "$ZIZHAN_SRC/work/native18-volumes-22G100" \
  --component OS --component Cryptex1,SystemOS --component Cryptex1,AppOS --jobs 8
python3 scripts/build-zizhan-native18-core.py --source "$NATIVE_SRC" \
  --work "$ZIZHAN_SRC/work/native18-core-new" --jobs 8
python3 scripts/native18-storage-abi.py --native-source "$NATIVE_SRC" \
  --tree "$ZIZHAN_SRC/work/native18-core-new/tree" \
  --input-root "$ZIZHAN_SRC/work/native18-inputs-22G100" \
  --prelink-kit "$PRELINK_KIT" --out "$ZIZHAN_SRC/work/native18-storage-new" \
  --parent-interfaces
```

三项本地HTTP测试覆盖断点续取和逐字节结果、损坏range的CRC拒绝、忽略range的200响应拒绝；四项CLI控制覆盖无效并发度、禁止Apple输出到公开仓库、禁止重用core输出目录、禁止覆盖ABI输出目录。均通过。另有真实Apple完整下载/CRC、两次只读版本核对、真实core链接、真实设备UFS源编译和190/181/168方法签名证据。只有host支持脚本新增，未修改默认驱动/内核源；没有新默认内核双配置或CI结果，不继承先前SHA绿灯。

## 下一实现边界

Pong新的电源构建把Nothing单电池、TFA9873功放、Glyph和基带整体编入；本机是Xiaomi双电池协议及CS35L41，不能为“凑齐full”直接启用该配方。下一最小交付是**以本机既有板级事实拆分原生存储/软件AES/平台资源的最小完整链接路径，保留原生父类PAC和真实失败传播，再接zizhan电源适配**。不得用Nothing固件、功放参数或主板内存常量代替本机输入。

完整source kernel/stock kext闭包、zizhan loader的fileset/PAC与panel/内存/返回契约、原生UI/Metal ABI、隔离Data与回退尚未闭合，因此本批停止在主机交付，不执行设备boot。当前安装仍v23，canonical仍OS370；APP-01 v25仍未派发，POWER-02前置不变。
