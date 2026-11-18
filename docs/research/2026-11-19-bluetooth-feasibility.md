# 蓝牙可行性（2026-11-19，离线 + Android 只读，无设备写入）

优先级最低（外屏/折叠/熄屏 → 天气 → 相机 → 温控调度 → **蓝牙**）。本页只记录硬件事实和建议的第一步。

## 本机硬件（Android 只读核对）

- 组合芯片 QCA6490（WCN6855 系列，与已移植的 Wi-Fi 同一颗）；DT 根节点有 `bt_qca6490`。
- 主机接口：HCI over UART，`qcom,qup_uart@894000`（带 `qcom,wakeup-byte`，即 BT UART）；另一个 `qup_uart@99c000` 是调试串口一类。
- 固件在独立 vfat 分区 `/vendor/bt_firmware/image`：rampatch `hpbtfw20.tlv`/`hpbtfw21.tlv`，NVM `hpnv20.bin` 及按板号区分的 `hpnv20.b*`/`hpnv20g.b*`。
- Linux 主线 `btqca`/`hci_qca` 已支持这一系列（rampatch + NVM 下载、波特率切换、`HCI Reset`），可作参考实现。

## iPadOS 侧

- iPadOS 的 bluetoothd 只认苹果自己的 BT 传输与芯片初始化（BlueTool 脚本 + Apple/Broadcom 厂商命令）。高通芯片不能被它直接初始化。
- 可能的路线：内核里完成 QCA 上电、固件下载和波特率切换，把芯片做成"已初始化的标准 HCI 设备"，再以老款 iOS 设备的 UART H4 传输方式交给 bluetoothd。能否绕过 BlueTool 的厂商初始化需要单独调研，是最大的不确定点。

## 建议第一步（可无人值守、无射频动作）

内核探针：给 BT 上电（与 Wi-Fi 共用电源管理，需先确认不影响已工作的 Wi-Fi）→ UART 115200 → 发 `HCI Reset` 和 QCA 读版本命令 → 记录芯片 ROM 版本。只读握手，不下载固件、不开扫描。通过后再做 rampatch/NVM 下载和 `Read BD_ADDR`。

工作量判断：握手 1 天；固件下载 + 标准 HCI 1–2 天；接入 bluetoothd 是未知量，可能是最大的一块。
