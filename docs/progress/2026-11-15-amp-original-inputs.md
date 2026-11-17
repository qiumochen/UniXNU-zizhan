# 原机功放保护固件与参数离线前置

源码基点为 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。[上一批](2026-11-15-amp-boot-identity.md)已取得两颗CS35L41 B2真实数字身份。本批交付原机输入的仓外manifest和可运行的主机离线检查工具，不启动功放DSP或音频输出。当前队列仍由[STATUS](STATUS.md)管理。

## 实际输入留存

从本机Android的`/vendor/firmware/`只读取得11个文件：保护WMFW、T/B保护参数、校准WMFW与T/B校准参数，以及T-music/handset/handfree和B-music/handfree五个场景文件。每个文件均比较远端SHA-256、本机复制后的SHA-256和大小，全部一致；原始文件、角色、来源、时间及哈希只保存在仓外。没有向Android写文件、切换音频或执行校准。缺失odm路径带来的find退出码也保留，已取得的确切vendor文件由逐项读取和hash单独确认。

保护与校准的两个WMFW本机字节完全相同（各34068B），但系数/场景输入不同。文件名不能单独代表角色、算法或可播放资格；校准输入仅留证，不允许作为保护播放参数替代。T/B也必须分别保留：两个保护系数分别5060/5148B，不能混成一份。

## 可运行工具及检查范围

`tools/zizhan/mac/cs35l41-input-preflight.py`只接受显式仓外bundle与全新仓外report。要求固定11文件集合/来源/角色和实际远端/本机hash，拒绝漂移、重复、缺失、符号链接和越界文件。不会打开手机或加载固件。格式依据固定[Linux v6.6 wmfw.h](https://github.com/torvalds/linux/blob/v6.6/include/linux/firmware/cirrus/wmfw.h)及[cs_dsp.c](https://github.com/torvalds/linux/blob/v6.6/drivers/firmware/cirrus/cs_dsp.c)，原始参考源码哈希留仓外。

```sh
python3 tools/zizhan/mac/cs35l41-input-preflight.py \
  --bundle "$PRIVATE_AMP_BUNDLE" --output "$PRIVATE_NEW_REPORT"
python3 tools/zizhan/tests/cs35l41-inputs.py
```

WMFW只支持已审阅HALO v3/40字节header，检查每段type/offset/length、文件完整性、packed/unpacked跨度、声明内存范围及同类型重叠；绝对寄存器段不接受。WMDR检查header/core/revision、20字节块表、4字节padding、算法ID、长度及允许类型，拒绝绝对寄存器写块。场景文件只验证ASCII十进制有符号32位、首项计数包含全部token；它不是寄存器对列表，也不会生成写入。

报告始终明确`hardware_write_plan=false`、`firmware_loaded=false`、`dsp_started=false`、`tuning_applied=false`、`calibration_run=false`、`audio_output=false`及`playback_qualified=false`。这只是framing及部分内存边界检查，不校验固件签名/真实性，不解码算法metadata、运行时算法相对base或场景payload语义；源文件hash保障来源一致，不能代替完整加载验证。

## 本机结果与回归

真实bundle最终结果为`HOST_VERIFIED_INPUT_FRAMING`，报告记录最终checker源hash并重验一致。两份HALO WMFW各10段，声明XM1280/YM2000/PM4084 words；有PM/XM/YM packed payload和算法metadata。T保护WMDR3块、B保护4块，包含算法相对系数，base尚未解析。T三个场景各73个整数、B两个场景各22个整数，计数字段均吻合。所有原始值/路径文本留仓外，公开记录不包含固件或调音payload。

9项主机测试覆盖合法容器与明确scope、截断/错误header/core/type、超长/重叠/缺memory段、系数padding/算法/绝对写拒绝、场景计数/语法/范围、角色/远端hash/本机漂移、缺失/重复/symlink/bad manifest、CLI失败报告及不覆盖旧证据/拒绝公开路径。完整日用host runner通过。最终工具报告checker hash与当前源码一致；porting结构、相对链接/TOML和diff检查通过。

本批只改主机工具、测试和文档/测试触发，不改内核运行时，因此不重复上一批双kernel或身份轮次。手机只做有界只读文件采集，没有新temporary boot、安装或Data离线修改；最后实时观察本批只读采集时Android/zizhan/boot_completed=1，此后状态未新查询。

## 单一下一增量

进入CS35L41 B2的默认关闭初始化/保护加载候选：以实际模块和固定源核对B2 errata、OTP数据展开到易失寄存器、HALO内存与WMDR算法base的准确映射，再形成可测试且禁止输出的加载计划。场景整数语义未解码前禁止应用，不能直接把它们写成寄存器对。ADSP MI2S/TDM route还需本机配置输入；Pong的TFA profile不能代用。首次扬声器出声仍停在现场边界。

已有两颗ID/clock/pin/timing资格保留，不继续不变重复轮。原OS/UI/helper、OS370/Data及回退链保持；源码仍dirty main、无commit/push/云端CI，本批没有音频新设备资格。
