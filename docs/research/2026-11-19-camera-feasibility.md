# 相机可行性（2026-11-19，离线调研，无设备操作）

用户 11-18 定的优先级里相机排第 3（外屏/折叠/熄屏 → 天气 → **相机** → 温控调度 → 蓝牙）。本页只回答"能不能做、先做哪一步"。

## 硬件

- 后置：主摄 50 MP Sony IMX766（f/1.8、OIS），另有超广角与长焦；前置 20 MP（本机 CamX 模块确认为 IMX596，见下）。
- SoC 为 SM8475：Qualcomm Spectra ISP。数据通路 CSIPHY → CSID → IFE/IFE-lite（VFE）→ 内存；传感器经 CCI（I2C）配置，另需各自的供电、MCLK 和 reset GPIO。

## 本机 Android 只读核对（01:35）

- vendor CamX 模块：`zizhan_sunny_imx766_wide`（主摄）、`zizhan_ofilm_ov13b10_ultra`（超广角）、`zizhan_ofilm_imx663_tele`（长焦）、`zizhan_sunny_imx596_front`（前置）。前置确认为 **IMX596**。
- DT：CCI0（0xac15000）下 cam-sensor0/1/2（csiphy 0/3/1），CCI1（0xac16000）下 cam-sensor3（csiphy 2）；另有 actuator、OIS、eeprom 节点。CSID0–2、CSID-lite0/1、IFE0–2、IFE-lite0/1、CSIPHY0–5 均在 DT 中。
- 每个传感器节点有 vio/vana/vana1/vdig/vaf/custom 供电、clocks、gpio-reset 和 gpio 请求表；完整属性在仓库外 `work/camera-dt/camera-dt.txt`。I2C 从地址不在 DT，在 CamX 传感器模块 bin 中。

## iPadOS 侧的限制

- 系统相机链是 AVFoundation → cameracaptured/mediaserverd → Apple 相机内核驱动 + Apple ISP 固件。Qualcomm ISP 无法冒充 Apple ISP，**系统"相机"App 原样可用不现实**。
- 可行的接入点：iPadOS 17 起支持**外接 UVC 摄像头**（`AVCaptureDevice` external 类型，FaceTime 和第三方 App 可用）。如果内核把本机传感器做成一个"虚拟 UVC 设备"挂到 USB host 栈上，现成 App 就能拿到画面。系统相机 App 本身不支持外接摄像头。

## 建议路线

1. **只读调查（可无人值守）**：从 Android（root）导出 DT 中 CCI、各传感器节点（I2C 地址、供电、MCLK、reset、lane 数、C-PHY/D-PHY）、CAMSS 寄存器基址；记录 Android 相机打开时的时钟/供电投票。
2. **传感器握手**：在 iPadOS 内核里给一颗传感器（建议前置，分辨率小）上电，经 CCI 读 chip-id。需要先确认供电/时钟投票路径，失败只会读不到 ID，但上电时序错误有损坏风险，第一次建议有人在场。
3. **RAW 取帧**：CSID + IFE-lite RDI 直接把 RAW 写进内存，绕过 ISP；CPU/GPU 软件去马赛克（debayer）得到可看的图。参考 Linux 主线 `drivers/media/platform/qcom/camss`（已有 SM8250/SC8280XP 等，SM8450 系需核对）以及社区设备补丁（如 Xiaomi Nabu 的 camss 支持）。
4. **接入应用**：先做自用测试 App 读 IOSurface，再考虑虚拟 UVC 让 FaceTime/第三方 App 使用。

工作量判断：第 1 步半天；第 2 步 1–2 天；第 3 步一周量级；第 4 步（虚拟 UVC）是独立的大项目。

## 来源

- [GSMArena 评测](https://m.gsmarena.com/xiaomi_mix_fold_2-review-2475p5.php)，[Fudzilla](https://www.fudzilla.net/news/mobile/55311-xiaomi-mix-fold-2-is-an-impressive-foldable-smartphone)，[Kimovil](https://www.kimovil.com/en/where-to-buy-xiaomi-mix-fold-2-12gb-512gb)，[Wikipedia](https://en.wikipedia.org/wiki/Xiaomi_MIX_Fold_2)
- [Xiaomi Nabu camss 补丁](https://download.copr.fedorainfracloud.org/results/mcc45tr/nabu-linux/srpm-builds/10932122/rpmbuild/SOURCES/0023-media-qcom-add-Xiaomi-Nabu-camera-support.patch)

## 2026-11-19 中午：前置 IMX596 上电所需资源（离线，只读 DT）

| 资源 | DT | 实际来源 |
| --- | --- | --- |
| vio 1.8 V | `cam_vio-supply` | **PM8008J LDO5**（PM8008 是独立的 I2C 电源芯片，需要单独驱动） |
| vdig 1.056 V | `cam_vdig-supply` | **PM8008J LDO1** |
| vana 2.8 V | `cam_vana-supply` | `CAMERA_T_AVDD`：GPIO53 控制的 gpio-regulator |
| 时钟域 | `cam_clk-supply` | `cam_cc_titan_top_gdsc`（0xadf31dc） |
| MCLK | `CAMIF_MCLK3` 19.2 MHz | GPIO103，时钟来自 CAM_CC mclk3 |
| 复位 | `CAM_RESET10` | GPIO128 |
| 总线 | CCI1（0xac16000）master 0 | CSIPHY2 |

上电顺序（参照 Linux Sony 传感器驱动的通用顺序，待与 CamX 模块 bin 的 power-up 表核对）：复位拉低 → vio → vana → vdig → MCLK → 复位释放 → 等 ≥ 8 ms → CCI 读 chip-id。

前置条件：先实现 PM8008 的 I2C 稳压器控制和 CAM_CC（titan top GDSC + mclk3 RCG/branch），这两块在 iPadOS 内核里都还没有。第一次真正上电按计划放到有人在场时。

## 2026-11-19 下午：读芯片 ID 所需的三块逻辑已备好（宿主机测试，未接入内核）

- `drivers/PongPower/ZizhanPm8008.h`：PM8008 LDO 设电压/开关（每次写都读回），前置 vio L5 1800 mV → vdig L1 1056 mV，下电反序。
- `drivers/PongPower/ZizhanCamCc.h`：titan top GDSC → MCLK3 RCG 取 XO 19.2 MHz → MCLK3 分支；失败时按反序撤销。
- `drivers/PongPower/ZizhanCci.h`：CCI 需要的时钟全部可以只用 XO（slow AHB/CAMNOC AXI/CCI1 三个 RCG + 四个分支），CCI 复位、100 kHz 时序、16 位寄存器地址读（队列 0 写地址、队列 1 读，跳过回显字节），NACK/超时/长度都有错误码。

在场时的第一次上电（约 15 分钟，任何一步失败都按反序下电）：

1. GPIO128 复位拉低；PM8008J（I2C 0x0d，使能 GPIO11）L5 1.8 V 开 → GPIO53 拉高（vana 2.8 V）→ L1 1.056 V 开；
2. CAM_CC MCLK3 19.2 MHz，GPIO103 切到 MCLK 功能；
3. 等 1 ms 释放复位，再等 ≥ 8 ms；
4. CCI1 master 0 读 0x0016（Sony chip id，期望 0x0596 一类）。7 位地址先试 0x1a，再试 0x10（NACK 无害）；
5. 反序下电，记录全部读回。

补充：PM8008J 所在 I2C 是 **QUP wrap0 S5（0x994000）**，DT 时钟 GCC 96/134/135。GCC：投票寄存器 0x162008 bit 6/7（M/S AHB）、8/9（core/core2x）、15（S5）；S5 分支 0x127610，RCG 0x127618。同一总线上还有 eUSB2 中继（0x4f）和 FSA4480（0x42），操作时只碰 0x0c/0x0d，总线异常即停止。现有 `GeniI2c` 只支持 1 字节寄存器地址，PM8008 需要 16 位地址（先写 2 字节地址、不发 STOP，再读），接入内核时要补这一种传输。

## 2026-11-19 13:20：第一次和摄像头传感器通信成功（cam-1，静音，用户授权自行闭环）

`zizhan_cam_probe=2`（build-play35）：

- 只读盘点（self-1）：wrap0 S5 已由启动程序开好时钟（投票含 bit 15、DFS 开），GPIO206/207 已是 qup5 且 eGPIO 归 APPS，PM8008J 使能 GPIO11 为高；GCC camera AHB/XO 开；CAM_CC titan GDSC 收起，MCLK3/CCI1 时钟关。
- 上电：PM8008J（I2C 0x0d）L1 读回 1200 mV（默认）→ 设 1056 mV；L5 1.8 V → GPIO53 → L1，均读回一致；CAM_CC GDSC → MCLK3 19.2 MHz（XO）→ GPIO103 MCLK；复位 GPIO128 释放；CCI 时钟（XO）、CCI1 pads GPIO114/115（cci_i2c）。
- **CCI1 master 0、7 位地址 0x1a、寄存器 0x0016 → 0x0663**：这是 **Sony IMX663（长焦）**，不是 IMX596。更正：CCI1 / cam-sensor3 这一路是长焦（前文按编号推断为前置有误）。
- 结束：CCI 分支 → MCLK3 → GDSC 收起，LDO 反序关闭，GPIO 全部恢复；无 panic，返回 Android。

下一步：分别对 CCI0 上的三路（cam-sensor0/1/2）做同样的只读 ID 读取，确认主摄 IMX766（0x0766）、超广角 OV13B10、前置 IMX596 各在哪一路；之后才是 CSIPHY/CSID/IFE 取 RAW 帧。

## 2026-11-19 下午：按表探测四路中的三路（cam-front / cam-ultra）

- **超广角 OV13B10**（CCI0 master 1，0x36，寄存器 0x300a）→ **0x560d42**，与 OV13B10 一致。
- 前置（CCI0 master 1）：供电（PM8008J L2 1.2 → 1.152 V、GPIO97、L5）、MCLK5、复位均正常，但 0x1a NACK，之后两个地址超时——NACK 后 CCI 队列停住，探针没有复位就试下一个地址。已改为 NACK 后先复位，地址顺序改为 0x10、0x1a、0x37 再测。
- 已确认：长焦 IMX663（CCI1 m0 0x1a）、超广角 OV13B10（CCI0 m1 0x36）。主摄 IMX766（CCI0 m0）需要额外的 custom/vaf/vana1 供电，暂未做。

## 2026-11-19 14:30：四颗传感器全部在 iPadOS 下应答（cam-front2 / cam-main）

| 模块 | 传感器 | 总线 / 地址 | 芯片 ID | 供电（vio 均为 PM8008J L5 1.8 V） | MCLK / 复位 |
| --- | --- | --- | --- | --- | --- |
| 主摄 | IMX766 | CCI0 m0 / 0x1a | **0x0766** | vdig GPIO68 1.104 V，vana GPIO42 2.8 V，avdd1 GPIO70 1.8 V | MCLK2 GPIO102 / GPIO126 |
| 前置 | IMX596 | CCI0 m1 / **0x10** | **0x0596** | vdig PM8008J L2 1.152 V，vana GPIO97 2.9 V | MCLK5 GPIO105 / GPIO127 |
| 超广角 | OV13B10 | CCI0 m1 / 0x36 | **0x560d42** | vdig GPIO92 1.2 V，vana GPIO6 2.8 V | MCLK1 GPIO101 / GPIO24 |
| 长焦 | IMX663 | CCI1 m0 / 0x1a | **0x0663** | vdig PM8008J L1 1.056 V，vana GPIO53 2.8 V | MCLK3 GPIO103 / GPIO128 |

`zizhan_cam_probe=2..5`（默认关闭）。每轮都按反序下电、恢复引脚，无 panic。CCI 时钟、MCLK 都只用 XO 19.2 MHz。

下一步：CSIPHY + CSID + IFE-lite RDI 取一帧 RAW（先用超广角或前置，分辨率小），再做软件去马赛克。
