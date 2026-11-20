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

## 2026-11-19 上午：第一次 HCI 握手成功（bt-r1..r3，静音、无固件下载、无射频）

内核探针 `zizhan_bt_probe`（`drivers/ArmDTPlatform/ZizhanBtProbe.h`）：

- 模式 1（只读，bt-r1）：wrap2 投票寄存器 0x162010 = 0、S5 分支关、RCG 停在 XO；GPIO80 WL_EN 高（Wi-Fi 在用），GPIO81 BT_EN 低，GPIO76-79 为下拉的普通 GPIO。
- 模式 2（bt-r3）：
  - 时钟：这颗 RCG 没开 DFS，按 Linux gcc-sm8450 写 7.3728 MHz（GPLL0_EVEN，M 384 / N 15625，CFG 0x2601）；投票加 bit 0/1/2/3/9（只加不减，与 SE15 相同）。S AHB 分支带硬件门控（0x80000002），HALT 位空闲时保持置位是正常的。
  - SE20：FW 0x204（协议 2 = UART），QUP 版本 0x30080000（≥2.5，过采样 16），分频 4 → 115200；FIFO 模式、8N1、CTS 流控。
  - BT_EN 拉高 200 ms 后：`HCI Reset` → `04 0e 04 01 03 0c 00`（Command Complete，成功）；
    QCA `EDL_PATCH_GETVER`（`01 00 fc 01 19`）→ `04 0e 12 01 00 fc 00 19 0c 13 00 00 00 e6 38 01 02 11 12 0c 40`，
    即 product id 0x13、ROM 0x0201、SoC id 0x400c1211（WCN6855/QCA6490 2.1），对应设备蓝牙固件分区里的 `hpbtfw21.tlv` / `hpnv21*`。
  - 结束：S 命令取消、BT_EN 拉低、引脚恢复；时钟投票保留。

下一步：rampatch（`hpbtfw21.tlv`）+ NVM 下载（参考 Linux btqca），之后 `Read BD_ADDR`、切换到 3 Mbps；再研究把它作为标准 H4 HCI 交给 bluetoothd。

## 2026-11-19 中午：rampatch + NVM 下载成功（bt-r4/r5）

- `EDL_GET_BID`（`01 00 fc 01 23`）→ board id 0；SoC id 0x400c1211 属 GF 工艺（`soc_id & 0xff00 == 0x1200`），按 Linux btqca 命名用 `hpnv21g.bin`。
- 固件通过打包脚本新增的 `--chosen-blob zizhan-bt-*=PATH` 放进 `/chosen`（仅 RAM 启动包，不写分区；固件文件只在本机 `work/bt-fw`，不入仓库）。
- `zizhan_bt_probe=3`（bt-r5）：rampatch `hpbtfw21.tlv` 125468 B / 517 段，下载模式 3（只确认最后一段）→ 确认；NVM 6390 B / 27 段逐段确认（改为 115200、关闭 in-band sleep 与 deep sleep，适配当前轮询主机）；之后 `HCI Reset` 成功。
- `Read Local Version`：HCI/LMP 0x0c（5.3），厂商 0x001d（Qualcomm），LMP subversion 0x4b61。
- `Read BD_ADDR` 为全 0：芯片里还没有地址，Android 由系统属性写入；接入 bluetoothd 前需要处理（不在日志里记录真实地址）。
- 全程未开扫描/射频，结束拉低 BT_EN。

下一步：切换到 3 Mbps、设置 BD 地址；然后调研 iPadOS bluetoothd 能否接受一个已初始化的 H4 控制器（BlueTool 的厂商初始化是最大未知）。

## 接入 iPadOS bluetoothd 需要什么（22H374 二进制只读分析）

当前系统日志：`bluetoothd` 启动 0.1 s 即退出，报 `Unable to match bluetooth service in device tree` → `Not a valid MAC address (all 00s)` → `No bluetooth on this device`。

从只读挂载的 22H374 系统卷拷出 `/usr/sbin/bluetoothd` 和 `/usr/sbin/BlueTool`（仅本机 `work/bt-ios`）看到：

- **bluetoothd**：在 IODeviceTree 找 `bluetooth` 节点读 `local-mac-address`；控制器走**串口**，依次尝试 `/dev/cu.bluetooth`、`/dev/uart.bluetooth`（内含 H4 收发状态机）。所以不需要苹果的 PCIe 蓝牙栈，传统 UART 路径仍在。
- **BlueTool**（launchd `com.apple.BlueTool`，`-R`）：读 `bluetooth` 节点的 `transport-encoding`、`manufacturer`、`bt-transport-override` 和各种校准属性；通过 `/dev/btpoweroff`、`/dev/btreset`、`/dev/btwake` 控制供电/复位/唤醒；按 `manufacturer`（Broadcom / Apple ACI）选内置的 BCM43xx 启动脚本，找不到时报 `No boot script for the platform`。

接入方案（按顺序，每步可单独验证）：

1. ADT 增加 `bluetooth` 节点：`local-mac-address`（设备自己的地址，从 Android 侧读取后只放进启动包，不入仓库）、`transport-encoding`、`manufacturer`。
2. 内核串口设备：把 SE20 GENI UART 做成 tty（`/dev/cu.bluetooth`），内核在开机时完成本页的 rampatch/NVM 下载，交出“已初始化的标准 H4 控制器”。
3. `/dev/btpoweroff|btreset|btwake` 三个设备：映射到 BT_EN（GPIO81）等，供 BlueTool 使用。
4. BlueTool：找出 `manufacturer` 取哪个值时它不跑博通脚本，或者用 `bt-transport-override` 跳过。这是最大未知，需要反汇编 BlueTool 的平台选择逻辑。

工作量：1–3 约 2 天；4 未知。

### 设备树节点试验（bt-n1，蓝牙芯片不上电）

打包加 `--bt-node 02:5a:69:7a:68:01`（本地管理的假地址，不是手机真实地址）：ADT 多出 `/arm-io/bluetooth`（`local-mac-address`、`transport-encoding`=1、`manufacturer`=29），平台驱动发布了它的 nub。

- `bluetooth: Got local-mac-address: 02:5a:69:7a:68:01`，不再报 `No bluetooth on this device`。
- bluetoothd 随后运行 0.05–1.4 s 后 **SIGSEGV** 崩溃并被 launchd 反复拉起；没有其它日志。最可能是缺 `/dev/cu.bluetooth` 串口或其后的传输服务。
- 另一路 `marconi-bt` 仍报 `Unable to match bluetooth service in device tree`（匹配条件不同，待查）。

结论：设备树节点这一步有效，下一个必需件是内核串口设备（第 2 步）。默认打包不加这个节点。

## 2026-11-19 深夜：bluetoothd 常驻，卡在 AppleBluetoothModule

- bluetoothd 的 SIGSEGV 根因：`IODeviceTree:/product` 无 `product-id`，MobileGestalt 产品类型未知，控制器平台对象为空。补丁 0025 只对 bluetoothd/BlueTool 给出 J171 的 product-id 后，bluetoothd 常驻。
- BlueTool 需要 IOKit 服务 `AppleBluetoothModule`（`marconi-bt: Unable to match bluetooth service in device tree`）；真机由同名驱动匹配 `bluetooth,n88` 节点发布。下一步是反推它的用户客户端方法并实现兼容服务。

## 2026-11-20：iOS 18 的串口蓝牙走内核控制；两守护进程未触及传输层

- BlueTool `device -D` 打开的是 PF_SYSTEM 内核控制 `com.apple.uart.bluetooth`（read/write 收发 H4，TIOCGETA/TIOCSETA 作为 socket 选项）；已在补丁 0023 中实现该控制，并在 UART 桥里对博通厂商命令（OGF 0x3f）直接应答。
- 系统调用跟踪（补丁 0026）显示 BlueTool 与 bluetoothd 从未调用 socket 或打开 /dev 节点：卡在平台/初始化路径选择，详见晚间记录。

## 2026-11-20 第三轮：根因是固件表的 Wi-Fi 厂商键

BlueTool 的 boot 脚本按 MobileGestalt `ProductHash` 加 `WifiVendor` 查内置固件表（J171 → BCM4355C1 RudderA，分 MUR/USI）。`WifiVendor` 来自博通 WLAN HAL 服务 AppleOLYHAL 的 `vendor-id`，本板没有，于是 BlueTool 卡在 “No WiFi” 循环直至自身看门狗退出。覆盖脚本、占位 HAL 服务和 os_log 通道三种绕法都无效，详见 [第三轮记录](../progress/2026-11-20-round3.md)。

