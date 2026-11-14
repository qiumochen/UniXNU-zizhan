# MIG-01无人值守晨间记录

记录日期：2026-11-14（UTC+8）。夜间批次已于09:00完成安全收口，实时后续仍以[STATUS](STATUS.md)和仓库外机器handoff为准。

**完整迁移目标未完成，没有进行原生XNU临时启动，也没有取得原生桌面或基础功能成功。** 今晚完成了真实zizhan板级arm64e内核、匹配用户态/独立raw卷和loader主机检查，同时发现并修复了会误导可启动性判断的导入身份问题。当前显示修正版还有710个实际stock-kext导入未取得独立资格，不能生成或启动完整collection。

源码入口为已发布`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`加本轮dirty设备层实现。所有新脚本、补丁、配置、记录尚未commit/push，无本批CI；不能把主机构建产物归到一个并不存在的干净发布SHA。

## 交付与边界

| 项目 | 已观察结果 | 仍缺什么 |
| --- | --- | --- |
| 原生输入与源码 | 恢复固定Pong原提交及15个驱动gitlink，取得匹配iPhone17,2/18.6.2/22G100输入；OS/SystemOS只读版本核对。 | 获取/解密不代表硬件适配或可启动。 |
| 完整最小板级source内核 | 真实维护中的UFS、软件AES、GPU/SMMU、FST2、ath11k网络、ADSP/双电池/背光及有界返回完成DEVELOPMENT/arm64e/4KiB链接；使用真实候选UUID。 | stock-kext导入、重定位后的实际运行，XNU EL1密钥/调度资格。 |
| 独立用户态与raw卷 | 原生bootstrap/UI、Metal/Turnip/维护presenter插件、229项应用目录及41个可移除app种子、native Wi-Fi worker/实际固件进入独立本机OS；新readonly读回通过。raw OS/SystemOS/空白Data各自hash及真实UUID已验证。 | 本机文件不是已安装系统；DataMigration、LS应用安装、原生桌面及持久化未运行。 |
| loader与布局 | 保留本机128MiB panel pool/RAM所有权/返回链的native EFI完整构建，实际parser15控制含真实kernelcache/当前kernel与损坏入口/范围拒绝；外部PWF2方案的placement通过。 | 格式/放置不等于完整导入或硬件执行，未发送boot包。 |
| 原生显示交接 | 修复CPU合成前缺失的真实panel owner回切；GPU结果2/3/未知或quarantine不允许CPU写入/虚报完成；实际编译块10场景及原版负控制通过。 | 真实GPU/DPU完成与画面/触控、异步latch、息屏/恢复均未新增资格。 |
| PAC实机控制 | Android EL0正确认证IA/IB/DA/DB、PACGA、认证分支/返回通过；错误modifier负控制稳定进程SIGILL且Android boot_id不变。getcpu见证CPU0–5实际执行，CPU6/7 affinity拒绝SKIP。 | 不能推成8核全部通过，更不能替代原生XNU EL1每CPU/线程密钥生命周期。 |

当前source内核strip19,206,592字节，SHA-256 `4d810a1bb4cb13598249cad1dfac85eca6baddb97494f01832310ba42257472e`；unstripped23,022,792字节，SHA-256 `4e49133ee5a5d217a7019b3856f88ec1c249b8a8739a05d7da2c13968848fc93`。这只是当前准备态身份，未生成可启动collection。

## 两个实际部署问题已经落实修正

内嵌完整Wi-Fi固件使collection需要约27MB只读header区，而实际最大空洞约2.9MB。布局规划拒绝后改用固定原生上游真实PWF2 vnode reader，按需读取实际root-owned包；没有改Pong或别板的PHY/rail/clock常量。213个固件输入与连接手机只读hash一致，reader真实代码的元数据/目录负控制、payload/cache边界通过；新kernel放置检查通过。

独立OS原始8.3GB裸APFS容量不足，应用seed复制中途ENOSPC。原部分写入镜像和失败记录保留，独立块副本扩容16GiB、APFS fsck/增长、扩前后UUID/关键文件比对后再恢复；最终完整readonly读回通过。一次恢复命令工作目录错误导致又落在旧容量，已保留该失败，并把容量检查移到写入前。没有改原始restore或手机root文件。

应用bootstrap另有真实路径契约错误：上游app清单引用`/pong/native-staged-apps/`，reader却只允许普通var seed路径。显式native实验选项增加严格编号manifest和相同相对目标约束，保留O_EXCL/O_NOFOLLOW；实际解析器在ASan/UBSan下复制15,701项/853,669,631字节并逐项核对，原版负控制失败。保留原生SystemAppMigrator及可移除应用契约，不合成迁移成功。

## 为什么旧312/309/299计数被撤回

实际审查发现三参数`vm_upl_map`被普通参考图猜测绑定到两参数`ubc_upl_map`，可能错误递归或参数错位；正确LTO代码身份因先后顺序没能覆盖先来的猜测。进一步发现普通字符串图把布尔语义的`thread_wakeup_prim`指向按数量唤醒的另一接口。旧map没有在手机使用，各历史结果加独立rejection记录。

闭包入口现在区分研究提议和独立资格。完整类/PAC、具体source/API/data契约及一致的exact-code身份作为证据；graph/string/order标签不能直接取得重定位资格。操作等价包装的callee/data依赖也必须在证据集合递归闭合，不能从研究map自举。对抗控制改错UPL目标或把错误wakeup标签伪装成reviewed，均不能进入资格表。

最新`display-closure-v4-meta-wrappers`仍`QUALIFIED_IMPORTS_INCOMPLETE`：**710个实际引用目标未获资格；研究图缺523个。** 两者含义不同，后者不代表另187项已经实现。所有实际依赖保留，没有allow-missing、通用成功stub或删required kext换取零计数。

本批已逐项积累的直接契约包括：真实UPL参数/global调用、NoCopy工厂/12个literal初始化槽位、1,042组stock/native/公开C parser计算、15个构造链/240组对象输入、页几何直接双store、64组fatal ABI边界捕获、两个byte比较API各65,792组、232组strlen对齐/guard-page、512组安全类型转换，以及26组完整元类构造包装。负控制保留；host PAC抽象或字段fixture都不能冒充硬件/注册成功。

## 手机与回退

08:25及后续检查手机保持Android、boot_completed=1，28.2°C、约99%电量。没有原生XNU boot、flash、分区操作、旧iOS Data或pinned root改写；仅在新Android临时目录放置/运行有限PAC ELF和只读获取/核对固件。原已安装隔离v23、canonical OS370、原Data、完整备份与回退链保留。

原生桌面、显示/触控/GPU/Wi-Fi/亮度/电池与持久化没有新增native设备PASS。第一息屏/真睡眠、声音和折叠仍遵守原人工前置，不因迁移主机构建成功解锁。

## 接续动作

1. 从当前显示修正版实际输入的`qualified/imports.json`继续，优先补齐锁、分配器、日志、剩余global等高引用API的具体参数/对象/字段契约；研究map只提供待审查线索。
2. 独立闭包全部成立后，再对同一kernel/raw/loader身份生成并核验collection、页几何/重定位/权限和返回链。任意OS或kernel变化都需新raw/manifest/配对，不跨输入复制绝对地址。
3. 设备阶段须先完成新pinned文件/extent、实际备份、回退、温度/通道和有界返回前置，再仅temporary fastboot boot。当前没有这些步骤已执行的状态。

主线方向适合继续吸收Pong的原生arm64e结构、同版用户态和通用组件契约，zizhan硬件资源、owner和回退仍由设备层负责。本次遇到的导入身份错误说明现有配对工具本身也要验收；当前不足以把原生路线推广为默认主线或日用基线。

公开Python编译、TOML/文档链接/隐私路径扫描、完整changed-source凭据模式扫描、14area记录检查及diff检查已通过。native专用完整内核、实际loader/parser和相关host回放结果分列；无本轮push/CI，也未宣称默认22H374能力随迁移改变。

## 09:00实际收口

09:00:00的最终只读观察确认zizhan/boot_completed=1，Android boot_id与夜间控制前相同，USB供电、99%、28.2°C；没有device cycle/installer/waiter或迁移镜像挂载。夜间收口进程正常结束，Paseo本轮心跳已删除、对应schedule文件不在，owned launchd保活已bootout且后续print明确找不到任务；没有重启手机或写镜像来“恢复”一个本来就未改变的安装状态。

目标保持未完成，不标DONE/complete。已按计划停止新增夜间设备试验，源代码和证据完整留待接续；后续开发不因夜间监督结束自动取得安装/启动资格。

## 09:00后主机增量

继续进行独立导入证明和同版Apple KDK参考调查，详见[本批记录](2026-11-14-native-kdk-and-proof-search.md)。布尔唤醒直接契约、实际完整函数体检索及vnode_mount契约使当前独立缺口降到705项；研究图缺529项。KDK的154项名称命中仍为待审线索，不能直接绑定。kernel/raw/loader未改，无collection/native boot，夜间监督保持撤销，09:00后没有手机操作。上文710项是09:00收口时的实际状态。
