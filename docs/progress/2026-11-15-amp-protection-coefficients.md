# CS35L41 实际算法身份与分声道保护参数候选

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。接续[两颗完整保护RAM读回](2026-11-15-amp-ram-load.md)，本批最小交付为两颗实际算法身份/地址核对后，各自保护参数的受限RAM写入与完整读回。DSP执行与播放不在本批资格内，当前队列见[STATUS](STATUS.md)。

## 独立默认关闭模式16

同轮mode15完整RAM成功是强制前置：准确原机源hash、两颗6段/7469读回、14938写ACK/3618burst/零差异、禁止状态和确定总线。仍先完成safe-off、真实OTP、固定B2补丁和逐芯片易失trim。

在任何参数写之前，两颗都要从真实XM_UNPACK24读22个逻辑字，逐项核对core/block/vendor、firmware ID400a4/version2b01、算法数2、cd/version1d3f01及f20b/version100、各自XM/YM base与size。任一身份或地址不符，零参数写并失败，不以静态镜像代替芯片内身份。

只接受原机T5060字节和B5148字节保护参数文件，整个文件SHA256均通过后才访问参数RAM。两个文件通过显式私有临时包ADT传递，默认缺失；缺失/长度错误在MMIO前跳过。只写已核对的三段：T的YM_PACKED 3732字节；B的YM_PACKED 3792字节及YM_UNPACK24 8字节。地址由已核对算法base/offset得出并固定，只是RAM值，不执行场景文本、任意寄存器或校准程序。

共1883个32位参数写，随后逐声道完整读回（T933/B950）；packed按12字节组、unpacked按4字节。每段前后检查原禁止状态，10秒整体界限/固定段与字数/既有每事务取消中止限；错误不重试、总线不确定后不派清理，平台最终共享reset。全部成功也只证明参数在RAM中，复位后不保留，不等于保护固件已执行。

## 验证

```sh
python3 tools/zizhan/tests/cs35l41-coefficients.py
python3 tools/zizhan/tests/cs35l41-coefficients.py "$PRIVATE_WMFW" "$PRIVATE_TOP" "$PRIVATE_BOTTOM"
python3 tools/zizhan/tests/cs35l41-ram-adt.py "$PRIVATE_WMFW" "$PRIVATE_BUNDLE"
python3 tools/zizhan/tests/cs35l41-setup.py
python3 tools/zizhan/mac/check-daily-use-host.py
```

生产GENI模型执行完整初始化→RAM→算法身份→参数链；合成与真实原机文件均回放，测试第二颗身份错误时第一颗也不能先写、错误源hash/长度、原禁止状态变化、读回错、冻结时钟/总期限、每31事务及首末失败位置。平台34项包括参数缺失零映射及mode16复位失败，ADT encoder验证默认缺失/实际payload逐字节/错误参数hash拒绝。最终日用与reduced/full kernel按实际收口后才准备独立设备轮。

设备验收须同轮RAM/初始化通过、两颗identity_words22且具体ID/base吻合、sources2、1883attempt=ACK、T933/B950完整读回、段1/2、状态12/18及最终reset，零buserror/uncertain/DSPstart/output；fresh全量前置、后部USB-A、临时boot、30分钟监督、自动/panic/power-key返回、pinned deadline和Android完成见证仍必须满足。缺失或部分不提升通过。首次实际声音前停下叫用户现场，当前没有commit/push或OS/Data安装。

## 实际算法身份与参数读回通过（11:55）

独立mode16同轮完整RAM/OTP/B2/trim通过后，两颗各22个实际算法字匹配，firmware400a4/version2b01、算法cd/version1d3f01（XM94/50、YMe/7bc）及f20b/version100均正确。两份原机参数SHA通过，1883次写尝试=1883ACK，T933/B950完整读回、段1/2、状态读12/18均保持PWR1=0/PWR2=3300/HALO100，2945083微秒完成，最终共享reset。buserror/uncertain/DSPstart/output均0。

11:51:01临时启动、200秒返回，1367006字节新鲜deadline、ADSP13/电池/无fatal/Android完成通过，34.0°C。主机模型、静态输入与芯片实际身份/读回分别保留；全部易失RAM在最终复位后不作为已安装状态。

本轮结束后暂停设备轮，等用户在Android做尚未完成的原机双扬声器听测基线；只读路由不能替代实际声学结果。之后仍需默认关闭的HALO运行/MPU/状态/时钟资格及本机ASP/ADSP播放路由，首次iPadOS实际输出前再停下叫用户。没有把参数读回通过提升为DSP执行或音频可用。

## 用户现场反馈

用户确认Android上下两个扬声器均有声、听起来正常（“都有，都正常”）。未指定播放片段、音量和时长，未验证麦克风；只补Android声学基线，不提升iPadOS资格。用户允许继续，首次iPadOS实际输出前仍停下叫用户。
