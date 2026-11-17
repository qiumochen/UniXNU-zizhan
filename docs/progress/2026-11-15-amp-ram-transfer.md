# CS35L41 保护固件 RAM 传输计划与读回校验

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。接续[实际易失校准读回](2026-11-15-amp-volatile-trim.md)，本批交付为主机工具，状态 **HOST_VERIFIED**。没有运行时执行器、设备传输或新手机轮次。当前队列见[STATUS](STATUS.md)。

`tools/zizhan/mac/cs35l41-ram-transfer.py` 复用原机输入 manifest/framing/hash 与[内存计划](2026-11-15-amp-memory-plan.md)的物理窗口、声明容量、对齐及跨别名覆盖检查。只选保护 WMFW 的内存段，排除系数、metadata、场景和校准程序。第二次读取的完整固件副本必须与已经独立检查的计划哈希一致，后续分块和期望读回都使用这一不可变副本。

每块4字节，写事务为4字节大端寄存器地址加原文件4字节，不再次反转payload；读回为同地址4字节。每段有源offset、字节数、目标窗口、段hash与块数，固件文件最多1MiB、每颗RAM最多128KiB、最多32段。实际原机输入生成两颗共12段、59752字节、14938次写及14938次读的计划。此处仅计数，不能证明设备速度或完整传输能在指定期限内完成；运行时仍须整轮deadline和实测预算。

可选的 `--readback` 只接受完整、有序的JSON记录，每条为 `{address, register, data_hex}`。缺块、多块、重复、顺序/地址错误、长度/编码错误及任意字节不匹配均失败。返回 `SUPPLIED_READBACK_BYTES_MATCH` 也只说明输入字节匹配；工具始终保留 `device_verified=false` 与 `provenance_verified=false`，实际来源必须由独立设备监督与新鲜日志证明。报告不含固件payload，要求仓外新路径，拒绝覆盖已有结果。

验证命令：

```sh
python3 tools/zizhan/tests/cs35l41-ram-transfer.py
python3 tools/zizhan/mac/cs35l41-ram-transfer.py \
  --bundle "$PRIVATE_BUNDLE" --output "$PRIVATE_NEW_REPORT"
python3 tools/zizhan/mac/check-daily-use-host.py
```

8项合成控制与实际原机bundle计划通过，已接入完整日用host并通过。控制覆盖逐段源字节/寄存器步进、错误读回、输入manifest漂移、检查后固件变化、大小/段数上限、私有报告与失败结果保留，以及合成匹配不能提升为设备资格。本批未改driver，沿用上一批已通过的reduced/full kernel，未重新构建、打包或安装。

下一单一动作是实现默认关闭的运行时有限RAM加载/完整回读候选，要求同轮fresh safe-off、身份、B2、逐芯片trim及独占总线；全程HALO/boost/输出禁止。实际RAM算法ID/base核对前不应用系数。首次声音、外屏与自动切屏仍留现场。两小时窗口剩余不足原监督完整回收预算，本批不启动设备轮；监督停止结果以台账与仓外交接为准。
