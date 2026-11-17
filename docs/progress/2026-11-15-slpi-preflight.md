# SLPI 固件与传感器通道前置核验

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。本批交付是可重复的主机离线 preflight 和一个实际原机固件检查结果，不是 DSP 启动或传感器验收；当前任务顺序仍由 [STATUS](STATUS.md) 管理。

## 本机输入与决策

保存的 live FDT 中，`qcom,msm-ssc-sensors` 通过 `qcom,rproc-handle` 选择 `qcom,waipio-slpi-pas`；固件名为 `slpi`。SLPI 的 MMIO 声明为 `0x02400000+0x10000`，保留内存为 `0x88000000+0x1900000`。其 SMEM/GLINK edge 使用 label=`slpi`、glink-label=`dsps`、remote-pid=3，子通道 `IPCRTR` 的 net-id=2。remote-pid、net-id 和 QRTR packet node 是不同含义的标识，不能把 DT 中的所有整数都按 phandle 解析。

这与现有 `PongAdsp.cpp` 的 ADSP carveout、PAS1、remote-pid2 和 LPASS doorbell 路径不同。通用 `QcomMdt`、`QcomSmem`、`QcomSmp2p`、`QcomGlink` 核心可作为实现材料，板级启动和共享 IRQ ownership 仍须独立适配。现有 Wi-Fi QRTR 实现是单远端全局状态，不能直接把 SLPI endpoint 塞进去覆盖 Wi-Fi endpoint。

一轮主源码核对：[Linux v6.6 qcom_q6v5_pas.c](https://github.com/torvalds/linux/blob/v6.6/drivers/remoteproc/qcom_q6v5_pas.c) 将 `qcom,sm8450-slpi-pas` 选到 SLPI 的 PAS12/crash-reason424、lcx/lmx、load-state=`slpi`、ssr=`dsps` 表。这是可继续核对的协议来源，不是本机 `qcom,waipio-slpi-pas` 已匹配/已运行的证据。尝试 Xiaomi `zizhan-s-oss/drivers/remoteproc/qcom_q6v5_pas.c` 直链返回404；未取得该版本的匹配表前，不把同SoC主线表自动作为设备启动授权或固件认证结果。

后续已补到 Xiaomi 固定源码：[zeus-s-oss 的 qcom_q6v5_pas.c，提交7d10d738b2c4299f3b8549c61a01ece39f19c788](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/7d10d738b2c4299f3b8549c61a01ece39f19c788/drivers/remoteproc/qcom_q6v5_pas.c)。其中**明确匹配**`qcom,waipio-slpi-pas`到`waipio_slpi_resource`，PAS12、crash424、auto_boot=false、has_aggre2_clk=false、ssr=`dsps`、qmp/sysmon=`slpi`、ssctl=0x16。此前“厂商compatible匹配表未找到”的缺口至此收敛；仍是同平台源码参考，不能代替本机动态供电/握手资格。原始源码与哈希留仓外。

该固定版本的启动函数顺序为q6v5 IRQ prepare、bus scaling、active/proxy域、QMP load-state、XO/aggre2 clock、regulators，再PAS auth/reset和5秒ready等待；失败路径撤销取得的资源。不能复制其将认证失败转为panic的可选驱动行为，应遵守本仓库失败留证/有限返回和真实内核fatal保留规则。本机live DT、固件和共享owner仍决定具体资源；下一可据此实现独立SLPI路径，而不是复制主线其他机型的power-domain表。

本机完整备份中的 modem 分区已离线解压到仓外，并按备份 manifest 核对 398458880 bytes 和 SHA-256 一致；原始备份不变。通过 mtools 仅提取 SLPI 的 MDT/bNN，再用维护中的 `mdt2mbn.py` 合并。原机固件包含 TCS3720、LSM6DSO 和 SEE protobuf 配置/字段标记，为经 SSC 获取数据的方向提供证据；它们不证明某个服务已运行或协议已接通。

## 可运行交付

`tools/zizhan/mac/slpi-preflight.py` 只接受显式的本地 DTB、合并固件及仓外新报告路径。它验证实际的 SSC→SLPI→保留内存引用、祖先启用状态、唯一 phandle、identity bus ranges，以及已审阅的 SMEM/GLINK route；再编译主机小工具，调用设备内核同一份 `QcomMdt.c` 的 parse、metadata 和 place 函数核对 ELF。无手机查询、MMIO、SMC、固件认证、DSP 启动或运行时修改。

```sh
python3 tools/zizhan/mac/slpi-preflight.py \
  --dtb "$DTB" --firmware "$SLPI_MBN" --output "$REPORT"
python3 tools/zizhan/tests/slpi-preflight.py
```

`DTB` 是本机已采集的 live FDT；`SLPI_MBN` 是从受保护原机备份提取并合并的文件；`REPORT` 必须是仓外的新路径。不复用其他机型固件或用文件名代替来源身份核验。

实际结果为 `HOST_VERIFIED_PLACEMENT`：ELF32/Hexagon、25 segments、23 load segments、relocatable、span 26214400 bytes、metadata 5324 bytes。已合并文件大小 9494904 bytes；原始 DT、固件、各文件哈希和完整报告均只保留在本机。

报告始终单独列出 `authenticated=false`、`started=false`、`transport_ready=false`、`sensor_api_ready=false`。放置验证不检验签名或段内容真实性，也不能发现已被填零但仍保持合法布局的任意替换镜像；原机备份 manifest 和提取证据必须与它一起保留。

## 回归与范围

8 项 host tests 覆盖：生产 MDT parser 的固定/可重定位放置；误用 ADSP 地址或49MiB跨度、截断加载段；缺 hash、错误 machine/headers；实际 FDT parser 的引用/数字域；错机/错固件/错误 carveout/ranges/route；禁用祖先和重复 phandle；公开路径/重复报告拒绝；完整 CLI 成功及失败报告保留。C helper 与内核 MDT 代码一起运行 ASan/UBSan。

完整日用host runner通过；随后新增的空hash拒绝控制也在最终8项SLPI回归中通过。实际原机preflight的最终报告记录生产parser/checker源码哈希，并已核对与当前工作树一致。porting-record结构16areas、相对文档链接和`git diff --check`通过。

本批只新增主机工具、测试与记录，未修改内核驱动，故不重复上一批已经完成的精简/完整内核构建。无新候选安装、设备启动、SLPI ready、GLINK handshake、QRTR服务、传感器值或 iPadOS API 成功证据。

下一次实现前仍需查清 SLPI 专属 PAS 及供电/clock/ready/stop 参数，并解决与 ADSP/Wi-Fi 的公共 SMEM/IPCC owner；不能只把 ADSP 常量替换成这组地址后启动。
