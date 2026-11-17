# SLPI 时钟与带宽启动请求核心

源码基点为 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。继[共用QMP/SMEM增量](2026-11-15-slpi-shared-resources.md)之后，本批交付可调用的纯C资源计划及启动lease执行核心。它已经纳入设备power archive编译和日用host runner；**尚未接入开机入口、RPMh硬件owner或PAS12**，因此不能称时钟、带宽或传感器已在设备上工作。当前队列仍见[STATUS](STATUS.md)。

## 计划解析与编码

`drivers/PongPower/ZizhanSlpiPower.c/.h` 解析完整128KiB cmd-db副本，要求真实资源名、ARC/BCM slave、地址、辅助数据边界和唯一身份。缺失、重复、地址别名、错误slave、aux与header重叠、非法电压档位和不足的384 corner均失败，输出计划清零。

资源来自本机保存的live DT与固定Xiaomi版本：XO为`xo.lvl`值3；`lcx.lvl`/`lmx.lvl`从实际ARC表选择至少384的首个索引，不用角标值冒充索引，也不把不足的最高档当成功。`crypto_ddr`经过CE0/SN3/SN0/SH0/MC0/ACV；BCM的8字节aux按unit/width/VCD解析，不当ARC电压表。

厂商依据固定在[7d10d738b2c4299f3b8549c61a01ece39f19c788的bcm-voter.c](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/7d10d738b2c4299f3b8549c61a01ece39f19c788/drivers/interconnect/qcom/bcm-voter.c)和[icc-rpmh.c](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/7d10d738b2c4299f3b8549c61a01ece39f19c788/drivers/interconnect/qcom/icc-rpmh.c)。本批保持PAS路径的average0、peak UINT_MAX kB/s，按waipio节点buswidth及真实BCM unit/width做两段“非零最小1、其余向下取整”的除法，再限制到14位。不能假定所有合法unit都必然饱和；负控制用高unit得到CE0请求124，证明没有无条件填最大值。ACV按enable mask8处理。

BCM按实际VCD稳定排序，只在每组最后一条设置commit。厂商实现明确不跨payload拆同一VCD，因此执行核心要求owner一次发送完整分组；在任何baseline查询或写入之前检查已观测的TCS命令容量。当前逐条`rawVote`接口不能直接作为该callback。

## 启动lease失败边界

调用者必须持有公共owner的同一把锁，先为全部9个资源提供可信的原有请求，再发送。空闲TCS或软件默认零值不代表AOP实际保留票为零。任何资源baseline未知，整个计划不发送一条命令。

有可信baseline时，ARC保留较大值；BCM的average/peak分量分别取较大值；ACV使用位或保留已有性能/使能位。原请求的commit不会被误当资源数值，实际commit由本次VCD组决定。

成功必须是所有分组取得新鲜硬件完成。任一发送失败后，记录已确认数量、最后尝试的地址及整组数量，状态为`Uncertain`；不认为失败命令必然没生效，不自动降票/回滚，也不重试该lease。该候选仅设计为临时启动期间保留至reset，不提供睡眠、唤醒或运行时降频策略。

## 验证与资格

`python3 tools/zizhan/tests/slpi-power.py` 使用实际生产C核心运行ASan/UBSan。人工cmd-db仅作测试夹具，覆盖解析/辅助数据错误、corner不足、带宽非饱和缩放、VCD排序及commit、容量不足、较高baseline和ACV性能位保留、9个baseline缺失位置、6个分组失败位置及拒绝重复执行。没有模拟硬件成功结果。

最终v2源码的sanitizer及完整日用host通过；精简 `pong_ipados18_ci_defconfig` 和完整 `zizhan_ipados18_gpu_defconfig` 内核均通过，源码哈希与最终receipt一致。加入TCS容量预检查后的精简kernel SHA-256为 `d469b2b02551f6642fcc3a6c91f28bef0b633c3c692c04bdb470c8f4631f24a3`，完整为 `a90629083330be888871c004e72be49ccaa515edc95d6d6b95dd3369a0c19b98`；此前v1构建保留但不能替代最终v2。

新增核心不改变原ADSP/Wi-Fi开机行为；本批未打包或启动手机，也不继承上一批ADSP实机结果作为本批lease资格。当前host测试使用人工cmd-db，尚无本机完整cmd-db副本的离线计划结果；实际资源请求、设备ACK和SLPI仍未验证。

具体剩余接口为：公共RPMh owner尚无保留同VCD payload的有界整组事务，也没有完整说明开机固件留下的APSS资源baseline。下一可实现前者；后者未知时仍禁止实际请求及SLPI启动，可独立推进CS35L41只读chip-ID路径。原固件、原始源码/哈希、构建receipt及日志留仓外；无新OS/固件安装或Data改写，无commit/push。
