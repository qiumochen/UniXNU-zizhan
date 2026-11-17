# SLPI 共用资源与固件入口增量

源码基点为 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。本批将现有 ADSP worker 中的有限 QMP load-state 事务与硬件互斥下的 SMEM 分配抽为可供 SLPI 使用的代码，并给 vnode 读取器增加精确的 SLPI 固件路径。没有新增 SLPI 启动开关或 PAS12 调用。当前任务与下一动作由 [STATUS](STATUS.md) 管理。

## 交付与失败边界

`ZizhanDspResources.h` 的 `LoadStateOwner` 由原 ADSP worker 持有；ADSP 与未来 SLPI 不能各建一个 mailbox owner。仅接受 ADSP/SLPI 的 on/off 命令，使用同一 `QcomQmp` 状态，检查发送返回并等待至多1000ms。超时会清理当前 mailbox 并将 owner 标为不可复用，直到重启；防止上一事务的迟到确认被下一事务当成成功。正常 ADSP 路径已改用这个实现，SLPI 命令仅在 host 回放中执行。

共用 SMEM 分配 helper 通过已有 TCSR 硬件互斥，在所选 remote partition 内分配和验证已有 item 的实际大小。已有但过小的 item 返回错误；不能因同名 item 存在就把它当完整 SMP2P/FIFO 缓冲区。未来 SLPI 保持 remote3 分区，ADSP 保持 remote2；这些 helper 不建立第二线程、不改变 Wi-Fi QRTR endpoint，也不证明不同线程调用是安全的。

`PongFirmware.c` 只允许 `/pong/firmware/adsp.mbn` 和 `/pong/firmware/slpi.mbn` 两个精确路径；错误输出清零，文件仍限非空且不超过64MiB，单次读取不超过1MiB并检查加法越界。路径允许不等于固件已安装或已认证；原已安装 OS 尚未加入 SLPI 文件。

## 本机资源核对与启动限制

保存的本机 live DT 指定 SLPI cx/mx 为 `lcx.lvl`/`lmx.lvl`，目标level均为384；XO 指向 `qcom,waipio-rpmh-clk` 的索引0；`crypto_ddr` 路径从 aggre2_noc 节点43到 mc_virt 节点512。`slave-kernel` 的fatal/handover/ready/stop-ack为bit0/2/1/3，`master-kernel` stop为bit0；SMP2P本地0/remote3，in481/out430，IPCC client4/signal2。

固定厂商版本 [7d10d738b2c4299f3b8549c61a01ece39f19c788](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/7d10d738b2c4299f3b8549c61a01ece39f19c788) 的 remoteproc 启动顺序要求带宽、QMP、XO及regulator再PAS认证/启动。现有 ADSP 启动不能代替 SLPI 的 XO/crypto_ddr 资源请求；本批未实现这些请求，故不启动 PAS12。已将同固定版本的 RPMh clock、waipio interconnect及binding源码按哈希保存于仓外，用于落实具体 owner 和请求语义。原始 DT/固件/日志保持私有。

同版本clock表把XO索引0映射为`xo.lvl`、offset0、on值3、divider4；普通clock同时涉及sleep/wake/active集合，不能将现有active TCS互斥当完整clock owner。带宽路径为CRYPTO→A2NOC/SNOC→GEM NOC→LLCC→EBI1，涉及CE0/SN3/SN0/SH0/MC0/ACV，并非只写CE0。下一实现须先保留GPU/WLAN既有请求，确定本次active-only启动lease的适用范围；不借此批改sleep/wake TCS，不把厂商声明当本机请求已生效。

## 验证与状态

`python3 tools/zizhan/tests/slpi-shared-resources.py` 编译实际 `QcomQmp.c`、`QcomSmem.c`、新 helper 及实际 vnode adapter，运行ASan/UBSan。覆盖ADSP→SLPI命令串行确认、无效processor、忙/未开邮箱、超时/迟到确认拒绝、remote2/3同号item隔离、已有过小item、锁失败、无空间/损坏，以及精确路径/文件大小/读取越界/短读/底层错误。模拟AOP/vnode只作host控制，不作硬件结果。

完整日用host runner、精简 `pong_ipados18_ci_defconfig` 与完整 `zizhan_ipados18_gpu_defconfig` 内核均通过；本机receipt记录本批运行时源哈希，旧IPCC构建不替代本批。精简kernel SHA-256为 `cc4e6848a86d175146a4013c4bc6977ef2072b502eabe25ea36fdf8bcda8791b`，完整为 `c7137167f6c7dcc3b0710e2b9c929481c9c7d440496f2a161e89f2ac185e465e`。

已完成一轮**变更后的ADSP兼容性回归** `slpi-resources-1115-v1`：新kernel、route1/ADSP2/audio0，保持既有OS/UI/helper；SLPI未启用。新鲜whole-OS/manifest/extent/backup preflight通过，02:43:05实际临时启动，196秒后返回bootloader；1542328B新鲜deadline klog，02:46:55 Android/zizhan/boot_completed=1，33.9°C，supervisor终结PASS。全程后部USB-A、210/150秒返回及30分钟监督。

实际日志ADSP阶段1至13，电池源发布后31条更新/18组不同电压电流温度值，没有ADSP failed/fatal、panic或PongAudio启动。既有有限统计仍见真实AOP/ADSP doorbell，unknown/budget/retries均0。主机回放中的SLPI命令及SLPI固件路径未在该轮使用，因此设备结论仅为`PASS_SCOPED_ADSP_SHARED_RESOURCE_REGRESSION`：共用helper的ADSP正常路径未见退步，不证明设备上的SLPI、超时注入或固件读取已通过。

新kernel只临时启动，SLPI固件未安装，OS/UI/helper及Data/OS370/回退链未推广或离线改写。完整raw log、独立Android witness、review、build receipt和source/prepared identity均留仓外。源码仍dirty main、无commit/push/云端CI；下一增量是共享资源owner的XO/带宽启动lease及其真实失败边界。
