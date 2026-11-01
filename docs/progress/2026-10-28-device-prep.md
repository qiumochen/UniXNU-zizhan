# 2026-10-28 设备准备与 UEFI 第一版

## 1. 设备实况（只读核对）

| 项 | 值 |
|---|---|
| 机型 | zizhan，22061218C，国行 |
| 系统 | MIUI 14 `V14.0.15.0.TLRCNXM`（Android 13），内核 5.10.136 GKI |
| 槽位 | 当前 b；a、b 均 `successful:yes` |
| bootloader | 已解锁（orange），`fastboot getvar unlocked: yes`，`max-download-size: 805306368`（768 MiB，`fastboot boot` 不受 K90 那种 2 MB 限制） |
| root | KernelSU（管理器包名已随机化），已给 Shell 授权，`su` 上下文 `u:r:ksu:s0` |
| 连接 | USB 接 Mac mini；所有 adb/fastboot 在 Mac 上执行（`ssh mac`） |
| GPU | `Adreno730v3`；固件 `a730_sqe.fw`、`gmu_gen70000.bin`、`a730_zap.*` |
| Wi-Fi | QCA6490（与 Nothing Phone 2 相同） |
| 触摸 | spi0.0 `fts-pri`（内屏），spi2.0 `fts-sec`（外屏）；外屏触摸电源 `6-0028 wl2866d`；固件 `st_fts_l18.ubin`、`st_fts_l18_sec.ftb` |
| 音频功放 | 2× CS35L41（I2C 2-0040/2-0042） |
| 显示 | DSI-1 内屏 `2160x1916` cmd（60/120），DSI-2 外屏 `1080x2520` cmd（60/90/120）；cmdline `msm_drm.dsi_display0=…prim_dsc_cmd`、`dsi_display1=…sec_dsc_cmd` |
| bootloader 画面 | FASTBOOT 画面**两块屏同时显示**（用户目测） |
| splash 显存 | `cont_splash_region` `0xb8000000` + `0x2b00000`（与 pong 的 `Display Reserved` 相同） |

## 2. 备份

- 位置：Mac `工作区 backup/2026-10-28-V14.0.15.0/`。135 个分区（除 userdata、super 外全部），全部核对了字节数和设备端 SHA-256，没有不一致；另有 6 个 LUN 的主/备 GPT（各 6 块和 5 块，块大小 4096）。gzip 后共 1.5 GB。清单为 `manifest.tsv`。
- 第二份（只含不可重建的校准和身份分区，共 33 个文件）：本机 `backup/critical-2026-10-28/`，已抽查 SHA-256。
- 脚本：`tools/backup_mac.sh`（Mac 侧）。

## 3. 原厂 UEFI 对照

- `uefi_b` 是 ELF，FV 加载在 `0xa7000000`，大小 `0x2a0000`。
- `uefiplat.cfg` 的内存表与 pong 的 MemoryMapLib 几乎逐行相同。差异有三处：PIL Reserved 大小 `0x1DC00000`（pong 为 `0x1C200000`，实机 DT 也是到 `0xA2E00000` 为止）；没有 QTEE 条目；Display Demura `0xA1400000` 与 PIL 区重叠。
- 原厂 FV 自带两块屏的面板配置：`Panel_L18_38_0c_0a_prim_dsc_cmd.xml`、`Panel_L18_38_0c_0a_sec_dsc_cmd.xml`。后续可以改用 zizhan 自己的 DisplayDxe 来点亮任意一块屏。

## 4. UEFI 第一版（V1）设计

- 基于 `Chengdong-Yu/uefi-pong`，新增 `Platforms/Xiaomi/zizhanPkg`（源码在本仓库 `uefi/`）。
- 高通 DXE 沿用 `Binaries/pong`（同为 SM8475），第一轮不替换。
- MemoryMap 按 zizhan 修改：PIL 大小改为 `0x1DC00000`，删去 Demura 和 QTEE 两项。
- 显示用 `SimpleFbDxe`（`USE_CUSTOM_DISPLAY_DRIVER=0`），framebuffer 设为 `0xb8000000`、2160×1916（假设主屏 DSI0 的显存从起始地址开始）。这样依赖 bootloader 保持面板自刷新：小米 12S、K60 在同一地址写屏都已实测可见。
- 设备树使用实机 `/sys/firmware/fdt`（`Resources/DTBs/zizhan.dtb`），同时用于 FV 内嵌和追加到启动镜像。
- 启动方式只用 `fastboot boot`，不写分区。
