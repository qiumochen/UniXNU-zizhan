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
