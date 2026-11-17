# CS35L41 B2 / OTP 易失校准的离线计划

源码基点为 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。本批接续[保护内存计划](2026-11-15-amp-memory-plan.md)，交付原机模块表解析器与可测试的OTP位流解码器；没有设备执行器。当前任务入口见[STATUS](STATUS.md)。

## 原机输入与实际表结构

从此前私有留存、来源和SHA-256匹配的原机AArch64模块提取 `cs35l41_otp_map_map`，按ELF relocation的目标节和符号解析指针，不假定重定位节名称或本机文件偏移。只接受已核对的五项ABI、ABS64零addend、不可变rodata表、明确的易失trim目标寄存器及无重叠位域；文件、节、符号和位流均有上限与越界检查。

原机支持OTP_ID `1/2/3/6/8`，分别指向两种字段表，每种100项。从第2个零基word的bit16开始，消耗688位，结束于位768。第一种含2项保留字段，第二种含1项；保留字段仍消耗源位数，但不产生寄存器更新，零宽保留项也合法。跨32位word的字段按低位先行解码，传输端仍须将BE32线序转换成32个无符号主机word。

每颗芯片必须使用自己的真实OTP_ID和32个word，不能从Android启动日志、默认值、另一颗芯片或人工fixture推定。解码只产生指定易失寄存器的masked update，不允许OTP内存、测试钥匙、电源/AMP或HALO core enable成为trim目的地址。

## B2补丁与钥匙边界

原机B2补丁表严格匹配此前核对的10项：寄存器 `0x40` 写入 `0x5555/0xaaaa`，6个补丁目的寄存器，然后 `0xcccc/0x3333`。这与通用Linux表内容不同。

进一步核对原机probe反汇编，OTP trim阶段则在 `0x40` 使用单字节值 `0x55/0xaa` 打开、`0xcc/0x33` 关闭；不能把这两组数值混为同一个事务。该发现只是静态调用路径依据，尚无硬件钥匙/trim执行和关闭读回资格。原机probe在OTP后还会继续配置其他电源/接口寄存器，整段probe不能视为本候选的安全初始化。

## 可运行交付与验收

```sh
python3 tools/zizhan/mac/cs35l41-init-plan.py \
  --module "$PRIVATE_PHONE_MODULE" --manifest "$PRIVATE_MODULE_MANIFEST" \
  --output "$PRIVATE_NEW_SCHEMA_REPORT"
python3 tools/zizhan/tests/cs35l41-init-plan.py
python3 tools/zizhan/mac/check-daily-use-host.py
```

原机实际模块结果为 `HOST_VERIFIED_B2_OTP_SCHEMA`，五种表均为100项/688位。最终报告带当前checker hash，报告、模块、原始反汇编和来源manifest均留仓外；旧报告保留不覆盖。CLI仅生成符号字段/阶段/读取配方，不含本机逐颗校准值。

9项合成测试及加入本测试后的完整日用host通过，覆盖ELF架构/extent、任意重定位节名、错误relocation/表ABI/补丁漂移、跨word/保留项、禁止目的地址/位域重叠/源耗尽、严格32个无符号word、独立逐位解码对照、CLI来源hash和报告保留。fixture只验证算法，不作为真实芯片校准结果。

报告明确 `plan_executable=false`、`phone_operations=false`、`hardware_writes=false`、`otp_snapshot_read=false`、`otp_unpacked=false`、`otp_programmed=false`、`dsp_started=false`、`audio_output=false`。本批只有host工具、测试和文档变化，31个运行时/构建输入hash与上一轮双kernel receipt保持一致，因此未重复内核构建、打包或手机轮次。

## 下一单一增量

在已有SE15/身份路径上实现默认关闭、有界只读候选：逐颗读取OTP_ID、32个OTP word以及实际输出/boost禁止和HALO停止状态，失败时保留原因并重新assert共享reset。先完成实际生产路径host控制及新reduced/full kernel，再经fresh manifest/backup/owner/自动返回前置决定是否临时启动；不重复旧ID资格轮，不写测试钥匙或trim。

真实快照取得后才选择对应表做host解码及易失写入/readback/relock计划。当前仍缺实际快照和输出状态读回，尚不能执行B2补丁、trim或固件加载；场景语义、本机ADSP MI2S/TDM和保护运行状态也未通过。首次实际声音保持现场边界，默认音频关闭。

本批没有phone命令、新设备观察、安装或Data修改，无commit/push/CI；已安装OS/UI/helper与全部回退保护保持，当前设备状态不由旧观察推断。
