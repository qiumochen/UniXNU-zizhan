# 原生PAC真机控制与collection布局

日期：2026-11-14（UTC+8）。接[板级链接](2026-11-14-native-board-link.md)。本批仍没有原生XNU临时boot、分区写入或旧OS/Data变更；增加了Android临时目录里的有限PAC探针进程，不能继续笼统写成“完全未执行手机代码”。源码入口为`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`加dirty设备层适配，未commit/push/CI，目标继续active。

## 布局失败与外部固件

对嵌入完整Wi-Fi固件的root-full-minimal-v1做实际布局规划，`__TEXT`在`__PRELINK_TEXT`需要`0x1a0e658`字节，最大实际空洞只有`0x2c6040`；在place_kernel拒绝，未生成collection。已有source链接通过不能覆盖这个部署阻塞。

采用固定原生上游的真实PWF2 vnode reader，在生成overlay中只替换维护Wi-Fi代码的固件目录查询，并在实际UFS readiness后增加有界root firmware前置；zizhan PHY/rail/clock、MSI、DMA和提交实现保持。该reader验证真实root owner/mode/type/size、PWF2目录/负载边界，按需缓存最多12个负载；固件缺失时optional worker停止正常启动流程继续。

`native18-wifi-v4-external`真实arm64e归档794,368字节，SHA-256 `11ba58fa03c50686c36564fb16be17aa71a46add0464aa7bf6586544d8230d7f`。213个固件输入逐项与连接中的zizhan Android只读SHA-256比较一致；一次exec-out shell引号错误未得到hash，保留其原始输出后以有效命令重做，未将空结果记PASS。PWF2为25,566,934字节、SHA-256 `a78e109499e780521429d74f18ca878a9a0088be488e91a0dcc2b7684d9f6552`。

`native18-wifi-firmware.py`在ASan/UBSan下编译实际reader，只用host vnode/metadata适配：无rootvnode、错误owner/可写mode/缺失属性/非普通文件、magic/count/非对齐/越界目录均被拒绝；真实12项负载内容、重复读取缓存地址和13项缓存上限通过。首轮host harness漏声明caddr_t编译失败，补声明后通过；未执行固件或radio。

root-full-external-v1真实完整内核重新链接：strip19,190,208字节，SHA-256 `0279c73b6746589229fc2ce7ef55ca24cc2f403e4265600f2f966ce458272572`；unstripped22,998,512字节，SHA-256 `07aa1993ad2afcd69e62a555529351f3c2eae56b689d755c702fbb892dd31067`。`native18-layout-only.py`以同一固定实际依赖闭包执行原生layout validator，结果`PLACEMENT_VERIFIED_IMPORTS_UNCHECKED`，未重写分支或输出collection。原生UUID/导入和设备资格仍未通过。

PWF2已加入独立本机OS `/pong/firmware/qca6490.pwf`，写前核对reader测试、213文件设备hash证据及既有catalog/Metal/board runtime；重新readonly挂载检查完整pack hash、root owner0644及原有bootstrap/launchd/Metal/migrator不变，通过并卸载。手机OS/Data仍不变。

## Android EL0 PAC控制

新增有限freestanding AArch64汇编和静态ELF构造脚本。探针只使用stdout write和exit，不写keys/MMIO/系统文件，不解引用错误认证指针或分支到错误目标；每轮device timeout5秒、host15秒，真实ELF hash在传输后核对。temporary文件仅放在新建的`/data/local/tmp`目录，未影响pinned root或备份。

首次含错误modifier认证的探针退出132（SIGILL），无PASS输出；手机boot_id/boot_completed保持。加入阶段标记、去掉错误modifier AUT的控制版通过全部七阶段：IA/IB/DA/DB各16次签名及正确认证、签名确实改变、PACGA输入变化、BLRAA/BLRAB及RETAA/RETAB真实分支返回。恢复错误modifier的探针在第一阶段稳定进程级SIGILL，boot_id仍不变。记录为预期负控制，不把它抹成普通PASS或内核故障。

随后同一correct-only ELF尝试单CPU affinity：CPU0/1/2/3/4/6通过，CPU5/7的taskset在执行probe前以EINVAL拒绝；root进程控制相同。两项为`SKIP_AFFINITY`，不是PAC执行失败或通过；未修改CPU online或调度配置。Android报告online0-7仍不足以解释该亲和性拒绝，保留限制。

08:25补充观察到Android仍online0-7，而shell当前affinity仅0x3f。为避免把taskset派发成功等同于某个实际CPU执行，correct-only探针增加Linux getcpu并在PAC阶段前打印真实CPU ID。新ELF传后hash核对、5秒device/15秒host限制、有限8组；CPU0–5各返回对应实际ID并通过全部PAC阶段，CPU6/7在执行前被affinity拒绝，SKIP。没有CPU online/sysfs写入，Android boot_id保持。先前CPU6的派发PASS没有getcpu见证，不能合并成“实际八核都通过”。

这些结果确认实际Android EL0上的PAC执行，超出了只读HWCAP报告；仍不能证明XNU EL1各CPU密钥初始化、调度/线程切换、错误指针策略、原生用户态启动或完整arm64e系统可用。

## 导入继续按实际契约推进

嵌入固件完整内核闭包为317项，外部固件kernel必须重新派生全部绝对地址；其149/175个类表和vnode证据已重建，首版仍374项。等价函数绑定只发生在图推断结束后，不以任意源码名传播身份。

额外两类有限审查已经完成，下一配对使用本次实际external kernel重新核对：

- `current_task`类包装要求完整stock MRS-x0/tail与真实三指令accessor、完整native保存恢复/PAC返回检查及独立已配对tail目标一致；改变MRS选择的系统寄存器负控制拒绝。
- 两个read-only PAC getter按完整具体寄存器表达式比较，保留所有读取宽度、字段偏移、读取次序和AUTDA discriminator。仅允许严格指令白名单；改变最终字段偏移4字节的负控制拒绝，不忽略结构布局。

另有两个明确自报名字的unsupported PPL接口：stock为fatal panic，source为实际非PPL NULL/no-op，两者行为不同，不能称完全等价。审查发现当前retained corecrypto仅有的两个branch消费者均先查实际恒false的PPL query，并走普通IOMallocData/IOFreeData分支；allocation还保留NULL拒绝。其普通GOT transport和完整fatal wrapper核对后只作为final import审查，不反馈图推断，也不提供PPL实现。把实际query改成true的负控制拒绝输出该组anchors；不存在PPL支持资格。

对external内核的所有新鲜证据重新配对后，`external-closure-v2-final-anchors`剩312个导入。完整readonly语义匹配本轮共14项，其中12项与完整字节leaf重合，增量2项；不把同一项重复计作进展。当前仍INCOMPLETE，不emit可启动集合。

## 独立raw卷与真实UUID

`native18-local-volumes.py`已将当前独立本机OS、匹配SystemOS、独立空白Data转成三个新的raw文件。重新readonly挂载核对系统版本、bootstrap/应用helper/Metal签名、board/firmware文件及原生migrator身份；Data卷根目录确为空白，未让新系统接触旧Data。三个实际卷UUID各不相同，所有raw文件按4KiB对齐并逐窗SHA-256，生成SHNGROOT目录和真实UUID布局。

窗口只描述这些本机候选文件，没有复制Pong userdata起止常量，也未推断phone extents/安装权限。`RAW_LOCAL_VOLUMES_VERIFIED`不是已经安装或可启动。`build-zizhan-native18-core.py`新增显式布局/收据参数，重新完整核对三份raw文件hash/UUID后才允许用实际UUID链接。

root-candidate-volumes-v1已链接通过，实际生成的ROOT/CACHE/DATA编译header与三卷UUID一致；strip19,206,592字节，SHA-256 `a3ee90db63a35689ddc235884ae6e302e3aa473915754d6f87f2c0cd6d5291f3`；unstripped23,022,792字节，SHA-256 `c56291b3a3bbb57a3979ebd47ad39b435d9df32fb6d53f91577496b1f713e950`。placement validator再次通过。

对该实际UUID内核重建149/175个类表和全部vnode/元类/包装/叶函数证据，`candidate-closure-v2-final-anchors`最终仍312项未解析。此处结果属于新的真实候选输入；数值等于上一external内核不能替代新鲜核查。完整本机review manifest标为`PREPARED_INCOMPLETE_NOT_INSTALLABLE`，主目标未完成，无stock-kext集合或设备安装。

本机证据：`full-layout-v1-embedded`、`root-full-external-v1/layout-only-v1.json`、`native-wifi-v4-external`、`native-wifi-pwf-reader-test-v2`、`local-os-pwf-stage-v1`、`android-pac-probe-v{1,2-correct,3-negative-marked}`、`android-pac-percpu*`及各negative目录。原日志、固件、ELF和机器标识只留本机。
