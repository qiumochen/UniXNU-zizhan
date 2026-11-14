# 原生应用种子与目录准备

日期：2026-11-14（UTC+8）。接[本机OS/Metal/GPU批次](2026-11-14-native-os-gpu.md)，继续MIG-01整夜目标。源码入口仍是`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`加本轮dirty设备层脚本；未commit/push，无新CI或设备启动/写入。

## 发现和实现

原生`stage_native_staged_apps`生成的普通文件行引用`/pong/native-staged-apps/`，但同一固定源码的bootstrap `seed_manifest`只接受`/pong/var-seed/`。恢复原生可移除应用种子时，这些行会被实际解析器拒绝。固定Pong树后来将全部可移除应用改作builtins，绕过了这条路径；该策略标注为其他设备用户的特定选择，不自动继承到zizhan。

本机保留实际restore的原生可移除安装契约：`SystemAppMigrator`仍在原路径，初始化只复制真实种子，LaunchServices仍以有效bundle/proxy/原生安装状态判断成功。没有将所有应用塞进`/Applications`、构造LS数据库、写migration成功状态或退役占位记录。

新增`include/zizhan/Native18AppSeed.h`和`native18-userland.py --staged-app-seed`，仅在隔离source overlay接入额外reader策略：只有精确三位编号的`native-apps-NNN.skel`才能从`/pong/native-staged-apps/`复制到`/private/var/staged_system_apps/`的相同相对路径。已有owned manifest、Data路径限制及O_EXCL/O_NOFOLLOW保持。普通var seed不改变，默认用户态构建不启用本实验选项。实际新bootstrap/UI arm64e编译及strict4KiB签名通过。

## 实际解析器回放

`tools/zizhan/tests/native18-app-seed.py`截取该生成bootstrap的实际解析/复制函数，仅把syscall绝对路径映射到临时host目录，在ASan/UBSan下执行。七组manifest覆盖普通var seed、新应用种子、已有文件保留、其他manifest拒绝、源目标相对路径不一致、路径越界、错误编号和源symlink拒绝。保留的原版负控制在真正应用文件行失败，排除测试仅检查一个独立策略模型的解释。

随后用同一实际解析器回放完整22G100应用清单：35个分片、15,701项资源、853,669,631字节，复制到独立临时host目录后逐项核对字节hash、目录/file mode、root owner/group和symlink目标，全部通过。该结果证明host上的真实解析/复制路径；没有证明guest APFS挂载、DataMigration、SystemAppMigrator或应用安装已在手机执行。

## 原生目录和本机候选

`native18-app-catalog.py`从readonly、owners-enabled的匹配22G100 OS生成229项真实目录：188项原生builtins、41项原生可移除应用。每项要求实际Info、唯一identifier和现存真实可执行文件；保留种子资源原有mode。目录明确`EmbedRemovable=false`，不调用上游强制builtin策略。

原生服务构建入口编译的LS注册worker通过arm64e/strict签名，overlay仅选择该worker；同一次入口编译的其他板级适配器仍只是未选择的host输出。worker以mobile用户运行，最多四次有界重试，检查真实LS代理/安装状态；没有替原生服务宣告应用安装成功。结构化launchd cache仅在原有实际服务目录上加入该job。

上述overlay已应用于扩大后的独立本机OS：先前bootstrap/Metal实际文件匹配，被替换的本机候选bootstrap/cache保留，SystemAppMigrator保持原位；重新readonly挂载核对完整资源hash/mode/owner、更新bootstrap与LS helper签名及未改动的Metal/原Data种子均通过。

首次写入在原8.3GB固定容量sparseimage中遇到ENOSPC，中途结果记FAIL并卸载，部分写入镜像保留。直接image resize不被该SPRS/bare-APFS格式支持；独立UDTO块副本扩至17,179,869,184字节后，APFS fsck和实际container增长通过，再转回SPRS。扩容前后readonly检查卷UUID及bootstrap/cache/Metal/实际部分文件hash一致，余量8.9GB。一次恢复命令工作目录错误再次落在旧容量镜像，负记录同样保留；随后独立完成镜像选择并加上容量前置。

最终显式resume只接受原ENOSPC操作和未变的catalog/test输入，并核对原备份；保留实际部分文件身份后从同一源修复，完整写入及readonly读回通过。扩容/选择只改变本机候选，原始restore、原Data与phone/pinned root未动。闭包仍缺352项，真实UUID/独立Data安装方案、板级网络电源及设备资格未完成；不能因此生成或启动手机候选。

本机证据入口：`runtime-app-seed-v1/receipt.json`、`native-app-seed-parser-v1/results.json`、`native-app-seed-parser-v2-catalog/results.json`、`native-app-catalog-v1/receipt.json`及其`files.json`、`local-os-grow-v1/receipt.json`、`local-os-app-catalog-stage-v3-expanded/receipt.json`。Apple应用文件、原始日志和完整机器路径只留本机。
