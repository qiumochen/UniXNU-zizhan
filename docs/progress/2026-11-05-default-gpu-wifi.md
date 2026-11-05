# 默认内核统一开启 GPU 与 Wi-Fi（2026-11-05）

按用户要求，将此前已完成 [生命周期验证](2026-11-05-wifi-lifecycle.md) 的 Wi-Fi 并入设备默认配置。默认入口现在同时编译和启动 Adreno 730 与 QCA6490，不再要求单独指定 Wi-Fi profile 或 `WIFI=1`。

## 配置和启动入口

- `board.toml` 原有默认名 `zizhan_ipados18_gpu_defconfig` 保留，内容同时开启 GPU、Wi-Fi 和既有 PongPower。背光、音频和 ADSP 的运行策略未在本轮改动。
- `zizhan_ipados18_wifi_defconfig` 改为指向上述文件的相对符号链接，只兼容旧命令；不再维护第二份配置。
- `ci_kernel.sh`、format2 构建脚本和底层 macOS 构建入口都使用同一个设备默认配置。
- `ci_boot_ufs.sh` 默认选择该设备产物；GPU 和 Wi-Fi 启动参数依据实际 kernel report。默认设备内核开启两者，明确选择精简 CI 内核则关闭两者。
- `WIFI=0` 或显式 `pong_wifi=0` 仍可用于关闭无线排障；不是正常启动的必需参数。打包器合并参数后每个键只保留一个值。

已有私有固件、22H374 输入和实际 pinned root 的本机环境，从工作根目录即可使用：

```sh
ZIZHAN_SRC="$PWD" bash ci_kernel.sh
ZIZHAN_SRC="$PWD" bash ci_boot_ufs.sh NAME <本机核实的-root-4K块号> pong_autoreset=300 pong_autoreset_sb=180
```

这只是构建和打包命令，不会自动写入系统或启动手机。固件必须来自本机合法准备的输入，缺失仍明确失败。无人值守试验继续使用后部 USB-A、临时 `fastboot boot`、自动返回和新 UFS 日志。

## 主机核验

默认 GPU＋Wi-Fi 内核和显式精简 CI 内核均构建至 `KERNEL_DONE`。系统 bash 实际执行未设置 `WIFI`/`PROFILE` 的默认打包；默认产物的 report 同时含两个已编译驱动，命令行各有一个 `pong_adreno_start=1` 和 `pong_wifi=1`。另外实际打包并核对 `WIFI=0` 只关闭无线，以及显式 CI profile 不启动未编译的驱动。固定 UniXNU 配置解析器确认兼容别名与默认配置完全相同。

已有 Wi-Fi 主机回放通过。云端 CI 继续明确选择精简配置，不把它的成功当作完整设备功能证据。

## 设备验收

g344 使用上述默认构建和打包入口，未指定 `DEFCONFIG`、`PROFILE` 或 `WIFI`；本机实际 extent 和干净 OS manifest 已核验。仅开启有界 HTTPS witness 和正常 Settings URL，不含内核关联测试或私有 UI fixture。g344 实际自动关联一次，密钥安装、DHCP 和 HTTPS 200 通过，零 link loss；无线保持 UP，三张完整 120/120 行设置页截图通过，没有画布映射错误。309 秒自动返回 Android，取得 2,021,761 字节新 UFS 日志。原生 UI probe 明确 SKIP no private fixture，未用测试文件驱动加入。脱敏结果见 [证据](../evidence/2026-11-05-default-gpu-wifi.txt)。

本次合并改变默认选择，不提升自然 T1、不同子网、长时间压力、企业/SAE/6 GHz 或真实 MSI 的资格状态；这些限制及 g342 的独立显示失败继续保留。
