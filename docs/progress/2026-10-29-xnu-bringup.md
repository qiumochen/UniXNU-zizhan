# 2026-10-29 UEFI 与 XNU 首次上机

本文接着 [设备准备](2026-10-28-device-prep.md)。截至本轮，所有上机测试都只用 `fastboot boot` 临时引导，**没有写入任何分区**。Android 槽 b 保持可启动，每轮结束后都能回到 Android。

## 1. 进度总览

| 里程碑 | 结果 | 证据 |
|---|---|---|
| UEFI（Mu-Silicium + pong 的高通 DXE + zizhan 内存表） | ✅ 内屏显示 Silicium logo 和 UniXNU 菜单，颜色、方向正确 | 照片 2026-10-28-uefi-menu-sheet.jpg（未公开） |
| DarwinBoot24 交接 XNU | ✅ | 屏幕出现 XNU 文字控制台 |
| XNU 早期启动（k1，未预链接） | ✅ 打印 `Darwin Kernel Version 24.6.0 … DEVELOPMENT_ARM64_PONG`、`Pong RAM: 4 owned banks, 9728 MiB`，pmap 和 VM 启动完成 | 照片 2026-10-29-k1-ramoops-view.jpg（未公开） |
| 内存日志回读 | ✅ 强制重启后 `0x85200000` 的 ramoops 仍在，RamoopsView 能以大字分页显示，720p 摄像头可读 | 同上 |
| k1 panic | `kalloc.c:1798 assert(n_zones_sig + n_zones_type + 1 <= 32)`：未预链接时 kalloc_type 预算集中在少数大小档 | 同上 |
| k2（加 `kt_fixed_zones=60`） | panic：`remaining_zones >= min_sig`，预算过小。放弃手调这个参数 | 照片 2026-10-29-k2-panic.jpg（未公开） |
| k3（按 pong 正式流程预链接 22H374 kext，去掉 UFS 节点，1 核） | 走得更远：平台层发布 DT nub（pong-buttons 等）、GIC、显示、APFS kext 加载、CoreAnalyticsHub 启动。之后 panic：`Assertion failed: cpu < ml_get_max_cpu_number()` | 照片 2026-10-29-k3-panic.jpg（未公开） |

## 2. 下一步

1. **k3 panic**：最可能的原因是单核配置（`--cpus 1`）与预链接组件对 CPU 数量的假设不一致。pong 的正式打包用 `--cpus 8`，下一轮改用 8 核（或先用 2 核）。照片右侧被裁，需要用 RamoopsView 读完整 panic 行再确认。
2. **免按键闭环**：接入 sheng 的 `BootReset`。zizhan 的 SPMI 仲裁器地址（core `0xc400000`、chnls `0xc500000`、obsrvr `0xc440000`、cnfg `0xc42d000`）和重启原因单元（PMK8350 SDAM `0x7100` 的 `0x48` 字节，bits 7:1，bootloader=2）与 sheng 完全相同。目标是 panic 或超时后自动写入「bootloader」再 PSCI 复位，回到 fastboot 时 ramoops 仍在，主机自动启动 RamoopsView 读日志。
3. 之后：根文件系统（userdata 外的存放方案待定，先评估 RAM 盘）、ST FTS 触摸驱动、Adreno 730。

## 3. 构建链（Mac mini，`$ZIZHAN_SRC`）

源码目录用 `.noindex` 后缀，避免 Spotlight 索引拖高负载。脚本副本在本仓库 `tools/mac/`。

| 步骤 | 脚本 | 说明 |
|---|---|---|
| UEFI FD | `build_fw.sh` | 直接调用 EDK2 `build`。工具链用 Homebrew llvm 和 lld 合并后的目录。edk2 补丁只在构建期间应用，结束后回退 |
| UEFI 启动镜像 | `build_bootimg.py` | BootShim + FD + 实机 DTB → boot v4，产物 `out/zizhan-uefi.img` |
| 依赖准备 | `ci_prep.sh` | 复刻 UniXNU-pong 的 macOS CI：sheng、Apple OSS refer pins、DarwinBoot24（需要 llvm@18）、占位卷 UUID、22H374 kernelcache |
| 内核 | `ci_kernel.sh` | `build-pong-format2.py`，使用 `pong_ipados18_ci_defconfig`（不含 Wi-Fi、背光、Adreno） |
| 启动镜像 | `ci_boot_initial.sh`（未预链接）、`ci_boot_prelink.sh`（预链接，无 UFS） | 把 PONGFAT1 负载拼到 zizhan UEFI 镜像上 |
| 上机 | `trial.sh IMG RUN SECS` | 需要时执行 adb reboot bootloader，然后 `fastboot boot`，并按画面变化保存摄像头帧，最后拼成 contact sheet |
| 日志查看器 | `mkpayload.py` + `zizhanPkg/Applications/RamoopsView` | 强制重启进 fastboot 后启动 `out/ramoops-view.img` |

对 UniXNU-pong 的本地修改记录在 `device/unixnu-pong-local.patch`（基于 pong `db13296`）：
- `board.toml` 的 firmware 段改为指向 zizhanPkg。为了不改动 pong 脚本里的路径，`name` 仍保留为 `pong`。
- `dts/pong.dts` 替换为 zizhan 的 DTS（由 `tools/gen_reserved.py` 从实机 DT 生成，源文件为 `device/dts/zizhan.dts`）。
- pong 脚本原本只接受 Intel Mac，改为同时允许 arm64 主机（只是交叉编译，不影响产物）。
- Xcode 27 的 clang 对 XNU 头文件报错，加了 `-Wno-c99-designator`、`-Wno-reorder-init-list`、`-Wno-unnecessary-virtual-specifier`。

## 4. 踩过的坑

- Xcode 自带的 git 在克隆大仓库时会 Bus error。Mu_Basecore 改为按 SHA 浅拉取。
- 新版 LLVM（Homebrew 默认版本）链接 DarwinBoot24 时报 `relocation against symbol in discarded section: DarwinEnterEnd`，需要用 llvm@18。
- `lld-link` 在 Homebrew 的 `lld` formula 里，不在 `llvm/bin` 下。
- macOS 不允许 SSH 会话使用摄像头。所以摄像头由用户在 Mac 终端常驻运行 `imagesnap` 循环，输出到 `cam/latest.jpg`。Camo 最高只有 720p，因此需要 RamoopsView 的大字显示。
- Mac 负载很大一部分来自 Spotlight 索引新源码。
- 强制重启（电源+音量下）后，`0x85200000` 的 ramoops 会保留。但如果手机直接进了 Android，这块内存会被 Linux 覆盖，日志就没了。
