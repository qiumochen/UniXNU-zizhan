# CS35L41 保护固件的离线内存计划

源码基点为 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。本批接续[原机输入清单](2026-11-15-amp-original-inputs.md)，交付可运行的主机地址计划工具；没有设备写入、DSP加载或输出入口。当前队列见[STATUS](STATUS.md)。

## 已落实的地址语义

从原机已留存模块的`cs35l41_dsp1_regions`静态表取得PM_PACKED、XM_PACKED、YM_PACKED、XM_UNPACK24与YM_UNPACK24五个窗口，和固定Linux v6.6表一致。实际模块表及哈希保留仓外，公开代码只保留寄存器事实。窗口分别为`0x03800000/0x02000000/0x02c00000/0x02800000/0x03400000`，packed PM按offset×5，packed XM/YM按offset×3后向下对齐4字节，unpacked XM/YM按offset×4。

HALO packed XM不能直接每3字节按大端切片：需先还原32位传输字节序，再组合连续24位字。已从实际保护镜像起始XM段解出固件ID`0x400a4`、版本`0x2b01`、vendor2及算法`0xcd/0xf20b`，与已留存的Android启动日志一致。这是静态镜像解码交叉核对，后续加载仍须读取芯片内的算法ID/base作运行时确认。

`WMDR`算法base先按目标窗口转换，再加系数字节offset，不能把offset再乘word stride。算法声明范围、固件声明范围与CS35L41物理寄存器窗口共同约束计划，拒绝未知算法/版本、不支持采样率策略、程序内存系数、未对齐及越界；packed/unpacked同一内存别名的覆盖也检查。固件metadata和场景整数不会成为寄存器写入。

## 可运行工具及实际结果

```sh
python3 tools/zizhan/mac/cs35l41-load-plan.py \
  --bundle "$PRIVATE_AMP_BUNDLE" --output "$PRIVATE_NEW_PLAN"
python3 tools/zizhan/tests/cs35l41-load-plan.py
```

工具先复用完整原机11文件集合的来源/角色/hash与framing检查，只选择保护WMFW及T/B保护系数。输出每段源文件hash、文件offset、长度与目标内存地址，不复制payload，不输出场景原文；计划只能保存在新仓外路径，不覆盖旧报告。没有执行器、设备传输或用户态固件读取入口。

实际原机输入得到`HOST_VERIFIED_PROTECTION_MEMORY_PLAN`：两声道固件各6个内存段，T系数1段、B系数2段，合计67284字节。T/B目标分别由各自参数文件解析，不能交换；系数算法base和版本均与静态镜像ID表匹配。最终报告固定两个checker源hash，已与当前工作树核对一致。旧v1计划保留，最终v2增加物理窗口/跨别名覆盖等检查。

9项合成测试与完整日用host通过，覆盖packed lane解码、firmware metadata不写、coeff字节offset、截断/错误core/count/重复ID、算法重叠、未知算法/版本/空范围/程序内存系数、packed/unpacked覆盖、物理边界/对齐，以及完整bundle/CLI输出保留和不执行语义。人工算法ID仅用于控制，不冒充实际芯片回读。

计划明确`plan_executable=false`、`hardware_writes=false`、`dsp_started=false`、`audio_output=false`；B2 errata、OTP易失展开、芯片运行时算法回读、固件内存回读、保护运行状态和本机ADSP路由均仍是待满足条件。本批只修改host工具/测试/文档，运行时代码与上一批双kernel receipt一致，不重复构建内核或启动身份轮。

## 新发现及下一实现

实际模块B2 errata表与通用Linux v6.6表均为10项，但**内容不同**：原机表为测试钥匙打开、6项补丁、钥匙关闭；通用表另含电源、增益及ASP输出相关配置。逐项比较报告与原始模块哈希留仓外，不能因相同芯片/项数而宣称相同初始化，也不将测试钥匙写入扩大为OTP/MTP编程授权。

下一单一增量是按原机模块核对B2初始化顺序与OTP数据到易失寄存器的展开，形成默认关闭、DSP停止/输出禁止条件明确的可测试初始化计划。当前纯内存计划不能代替这一步；未满足运行时算法base/readback前不能执行系数。场景payload、ADSP MI2S/TDM及首次实际声音仍分别保留，不套用Pong TFA或通用B2整段初始化。

本批无phone命令或新设备状态观察，无OS/固件安装、Data修改或commit/push；原私有输入与OS/UI/helper、OS370/回退链保持。实际数字身份仍以前一轮设备结果为准。
