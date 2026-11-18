# 相机可行性（2026-11-19，离线调研，无设备操作）

用户 11-18 定的优先级里相机排第 3（外屏/折叠/熄屏 → 天气 → **相机** → 温控调度 → 蓝牙）。本页只回答"能不能做、先做哪一步"。

## 硬件

- 后置：主摄 50 MP Sony IMX766（f/1.8、OIS），另有超广角与长焦；前置 20 MP（IMX596 与 S5K3T2 两种说法，未用官方资料核实，需从本机 Android DT/驱动确认）。
- SoC 为 SM8475：Qualcomm Spectra ISP。数据通路 CSIPHY → CSID → IFE/IFE-lite（VFE）→ 内存；传感器经 CCI（I2C）配置，另需各自的供电、MCLK 和 reset GPIO。

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
