# 原生framebuffer与维护中的scanout owner配对

日期：2026-11-14（UTC+8）。继续[导入资格审查](2026-11-14-native-import-contracts.md)期间，对完整板级候选发现并修正一项独立的显示消费者缺口。源码入口仍为`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5`加dirty设备层实现，未commit/push/CI；无原生XNU boot、系统/Data或pinned写入。

## 问题与实际修正

维护中的zizhan GPU默认使用其已有的panel-pool双缓冲。原生MiniFramebuffer只调用`pong_scanout_frame`，但GPU帧之后回到CPU合成时，没有调用维护配方已有的`pong_scanout_cpu_frame`；DPU可能仍扫描另一个缓冲，CPU却写入原panel缓冲。该函数已有真实settle/flip及owner gate，不能用一个新的noop代替。

新增native专用`0002-zizhan-native-cpu-frame.patch`和`build-zizhan-native18-core.py --display-handoff`：确定可CPU回落且未隔离之后、任何CPU像素合成之前调用真实owner回切；已完成GPU帧不切换。此为维护设备0009交接语义对原生返回类型/控制流的适配，不修改默认22H374源码。

当前原生消费者尚没有维护异步latch的完整tag/SwapWait契约。因此对返回2、3或未知状态，明确返回NotReady，不报告已呈现，也不进入CPU写入。默认同步路径保持；这不是异步呈现、panel off/sleep或物理latch验证。已有quarantine检查和真正fatal错误处理保持。

## 实际控制流回放与构建

`native18-display-handoff.py`截取真实生成/编译的pre-composition控制块，只用host owner调用记录和surface形状适配。ASan/UBSan十项场景覆盖：允许CPU回落、真实完成GPU路径、结果2/3/未知、调用前/调用后quarantine、无可用surface及GPU-only。核对完整调用顺序及是否进入后续CPU写入点；原版控制因缺少回切失败。测试没有把DPU/DSI/GPU完成模拟为设备成功。

首轮harness因未用常量在Werror下拒绝，删除该无关harness常量后通过；实际产品块未因此放宽。带显式handoff的新root-candidate-display-v1已完成DEVELOPMENT/arm64e/4KiB原生完整链接，并再次通过placement validator。实际编译tree的控制块再做同样回放，通过后才称本次生成patch确实进入source candidate。

OS/SystemOS/独立空白Data仍使用既有冻结raw卷和真实UUID，不改手机安装。新kernel地址/身份已重新派生149项类表、vnode及各组直接契约，未拿上一kernel的qualified map冒充本次结果。`display-closure-v2-qualified`为711项实际导入未获资格、研究图523缺口；stock-kext闭包仍未完成，不生成/启动collection。

新kernel strip19,206,592字节，SHA-256 `4d810a1bb4cb13598249cad1dfac85eca6baddb97494f01832310ba42257472e`；unstripped23,022,792字节，SHA-256 `4e49133ee5a5d217a7019b3856f88ec1c249b8a8739a05d7da2c13968848fc93`。真实卷UUID与raw输入保持，各新鲜证据仍按这组kernel hash核对。

本机证据：`native-display-handoff-v1`、`native-display-handoff-test-v{1,2}`、`root-candidate-display-v1/host-core.json`和`layout-only-v1.json`、`native-display-handoff-compiled-source-test-v1`。硬件观察仍为未执行。
