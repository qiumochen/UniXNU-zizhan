# 原生 Pong 路线接近验证：设备前置与主机门槛

日期：2026-11-14（UTC+8）。用户连接手机并要求尝试验证、向 Pong 主线靠拢，本轮因此从纯调研进入迁移可行性验证。长期方向见 [上游研究](../research/2026-11-13-upstream-resume-survey.md)，动态状态只在 [STATUS.md](STATUS.md) 管理。

## 交付与结论

**00:17 源码结论更正**：用户要求再次搜索后，我通过匿名网页/API、登录网页、Git 远端和搜索确认，旧主仓库地址当前仍返回 404；但原生主线固定提交还留在公共 fork 网络。已从 `xz1c/UniXNU-pong` fetch 原提交 `0ee4243a101eac10a77cbc8b7b8ef77ae8ec93d7`，550 项递归 tree 与 11-13 API 快照逐项一致。构建脚本、patch series、原生说明与 board 均存在于恢复的干净 checkout。本次“旧地址 404”准确，但我之前据此推断“完整源码无法取得”是错的，现撤销。此次恢复的是精确原提交，不是旧 fork 默认分支；原地址是否迁址、转私有或删除仍未知。15 个驱动 submodule 已全部按 gitlink 获取并核对，工作树干净；额外公开源与22G100输入及完整构建仍待准备。

**手机、备份清单、当前安装身份和 arm64e 编译前置通过；原生系统构建及设备启动尚未进行。** 当前设备仍为 Android。本轮没有 arm、boot、安装或刷分区，没有改变系统镜像及 Data。不能将本轮记为原生 iOS/arm64e/PAC 设备启动通过。

入口源码为 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`；工作树仅有前轮研究/台账/进度索引文档改动。本轮没有改动产品运行时代码，候选编译和回放文件保存在仓库外本机工作目录。未提交、未推送，没有新 CI。

| 检查 | 结果 | 范围及限制 |
| --- | --- | --- |
| 设备身份/Android/root | PASS | 00:04:57 确认为 zizhan，boot_completed=1，root可达；入口只有一台ADB设备、无fastboot设备。 |
| 温度/电量 | PASS | 同时观察电池28.1°C、100%、USB供电。仅为当时读数，不是连续温控或充电资格。 |
| CPU指针认证能力 | PASS：Android报告 | 八个核心的 `/proc/cpuinfo` 均含 `paca pacg`。不证明XNU密钥管理、每核上下文恢复或EL0/EL1认证可用。 |
| 安装与回退前置 | PASS | 新鲜preflight整窗读取实机OS，匹配fairplay-off-v23；root/klog连续extent、描述符、UI/helper本地身份均匹配，后部USB-A可控路径确认。135项备份文件在位且既有错误文件为空；未重新全量hash所有备份。 |
| arm64e目标编译 | HOST_VERIFIED | Xcode的iPhoneOS27.2 SDK，以`arm64e-apple-ios18.6`编译最小指针签名/认证代码，生成subtype `0x80000002` Mach-O object，汇编含`pacia1716`和`autia`。不是完整内核/驱动链接，也不验证Rust arm64e runtime或设备执行。 |
| 当前生产loader格式门槛 | HOST_VERIFIED | 提取本仓库未改动`ParseKernel`，在ASan/UBSan下运行：实际v25 stage旧arm64内核解析成功；仅改header的arm64e、fileset及带ABI capability的arm64e/fileset均返回unsupported。修改header的文件只是负面解析输入，不是有效原生内核。 |
| 上游固定loader解析 | HOST_VERIFIED | 可获取`pong-darwin-boot24@ed894a40f4c166eea645ebac5b95c856eeec50fa`，提取其未改动`ParseKernel`在ASan/UBSan下回放：legacy、arm64e execute、合成arm64e fileset接受；缺kernel entry、越界header、重复kernel entry拒绝。6项符合预期。没有构建完整EFI或进入固件。 |
| 原生源码、输入与完整构建 | PARTIAL | 原native主仓库名/ID仍404；已从公开fork网络恢复精确提交`0ee4243a101eac10a77cbc8b7b8ef77ae8ec93d7`，550项tree与API快照一致，关键原生构建文件在位。15个驱动submodule已按gitlink全部获取；配置Apple输入只有22H374、没有22G100，完整构建仍BLOCKED。 |

## 实际兼容性差异

本机 `drivers/DarwinBoot24/.../DarwinBoot.c` 的生产header gate要求ARM64 subtype0、MH_EXECUTE。上游固定loader接受屏蔽capability后的subtype0/2和MH_FILESET，验证`com.apple.kernel` entry，合并root segment仅装载一次，并在handoff前观察PAC ISA。

不能仅放宽两个header常量就声称迁移完成。原生kernel集合还需要：完整闭包/重定位和PAC签名；配套页权限及内核窗口；本机panel pool、额外RAM、失败日志和自动返回；原生OS/SystemOS/AppOS输入；新的Metal/用户态ABI；隔离Data和可验证回退。上游loader里的Pong USB/低DMA所有权不能覆盖zizhan板级事实。

最初只取得独立驱动和部分研究文件，随后经用户要求复查已恢复完整主仓库tree及15个固定驱动子模块。源码恢复不等于完整依赖、Apple输入或原生启动候选已准备。

## 实验身份与停止条件

- 假设：本机CPU及开发工具具备原生arm64e的基础条件，而现有启动链存在明确格式缺口。
- 控制：当前已安装v23整窗OS身份；当前实际旧arm64内核作为现有loader正控制；合成fileset仅用于上游解析函数回放。
- 变量：主机解析输入的CPU subtype、文件类型和fileset entry；设备没有切换内核或OS。
- 接受标准：安装身份与安全前置吻合、工具链产生真实arm64e/PAC代码、生产解析行为可重现、上游正常/异常格式有明确结果；每一层不越级声明设备资格。
- 返回/回退：没有离开Android，不需要执行回退。已安装v23、canonical OS370和Data均保留。v25仍未发送，不借本轮执行旧天气cycle，也不执行POWER-02面板off/睡眠。
- 停止条件已触发：最初完整主线源码不可取得（已在00:17撤销）、22G100输入缺失、无可用原生构建产物，因此不派发原生启动，不以旧内核或格式fixture冒充迁移验证。

## 证据与接续

本机原始证据为`logs/native-align-20261114T000424-entry.json`、`native-align-android-20261114.json`、`native-align-baseline-20261114.preflight.json`、`native-align-source-availability-20261114.json`、`native-align-host-gates-20261114.json`、`native-align-upstream-parser-20261114.json`及`logs/pong-availability-recheck-20261114.json`、`pong-availability-related-recheck-20261114.json`、`pong-native-commit-network-recheck-20261114.json`、`pong-native-source-recovery-20261114.json`。恢复的完整主仓库tree在`work/native-align-20261114/pong-native-source`，精确原提交、干净detached HEAD；主仓库tree及15个子模块已按固定版本恢复，没有修改上游源码或推送。生产parser摘录、编译命令、Sanitizer输出、合成fixture和下载源码均留在仓库外；绝不将fixture作为boot包。00:10之前所有回放均已结束，无无人值守设备任务。

主仓库及固定驱动现已取得，最小下一交付是**补齐匹配22G100及额外构建输入，形成独立zizhan主机构建候选**。先验证闭包和loader板级差异，再在完整备份、隔离Data、deadline/返回与回退条件下设计临时启动。可继续利用已取得的loader固定版本做源码差异整理，但按恢复源码审查loader契约后再适配本机生产loader，不下载其他型号固件代用，不操作NV/EFS。

检查方法：文档相对链接、公开隐私内容与`git diff --check`；使用现有固定checker核对14个结构化areas。没有产品代码改动，因此本轮不需要双配置内核重建，检查成功也不提升硬件能力。
