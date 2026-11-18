# 双系统开机引导：开源方案调研（2026-11-17）

用途：M3“脱离电脑启动”（PERSIST-01）的方案输入。只做了调研和**只读**的手机检查，没有写任何分区。

## 1. 找到的两个可直接参考的实现

### A. data-a-live `bootmgr/`（小米 17 Pro / SM8850，最接近我们）

- 来源：<https://github.com/ShueiTsuki/data-a-live>（11-04 归档）→ 后续 <https://github.com/yechenan/data-a-live>（11-11 仍在推送），文件 `bootmgr/README.md`。
- 做法：**只写一次 `boot_b`**。分区内容依次是：菜单程序 + 设置页 → 2 MiB 处原样拷贝的**原装安卓内核** → “iOS 格子”（约 54 MiB：格子头带标记/允许位/大小/校验/三个 pinned 文件位置指纹，后面就是 `fastboot boot` 用的那个整镜像）→ 原装签名信息。
- 开机：小米开机画面下显示 `Volume up  iOS  5`，5 秒内按音量上进 iOS，否则擦掉提示进安卓；充电开机等模式直接进安卓；iOS 里重启默认回安卓。
- 换 iOS 版本：`update_ios.sh` 只改格子（先清头 → 写内容 → 写头 → 读回校验，格子外区域前后 hash 必须一致），中途断电只会让格子为空。
- 保护：iOS 直接读写 `/data` 上 pinned 文件的物理块，文件被挪动就会写坏安卓数据。所以：`chattr +i` 三个文件 + Magisk 看门模块每分钟核对位置指纹，不符就把格子“允许位”清 0，菜单显示 `iOS off: 原因`。
- 撤销：`fastboot flash boot_b <原装备份>`；bootloader（ABL）不动，音量下+电源始终能进 fastboot。
- 先不写入的测试：`fastboot boot boot-menu.img`（`--any-mode` 测试镜像）。

### B. HakiOS U-Boot（POCO X5 Pro / SM7325）

- 来源：<https://github.com/oosexgang/hackios-xnu-kernel>，`docs/dual-boot.md` 与 `docs/u-boot.md`（11-10 推送）。
- 做法：自带 U-Boot 写进 `boot_a`，安卓留在 slot b；每次开机出菜单（10 秒默认安卓、HakiOS、fastboot、shell）。选安卓时把 slot a 标记为不可启动并重启，由 ABL 切到 b；U-Boot 自己从不改 active slot（作者记录：手工改过一次 active slot，导致所有镜像报 “Device Error”）。
- 代价：系统放在从 `userdata` 末尾切出的 `darwin` 分区，**安装要清空安卓数据**（`fastboot -w`）。
- 还记录了：在 U-Boot 下启动时声音/振动会让手机挂住、不能充电。

### 其他

UniXNU 设备目录里（alioth、j716f、uke、fajita 等）多数做法是把 UniXNU 镜像直接刷进 `boot_a`/`boot_b` 替换安卓，不是菜单式双系统，不适合我们。

## 2. 对 zizhan 的适用性（只读核对，2026-11-17 16:00）

| 项目 | zizhan 实测 | 结论 |
| --- | --- | --- |
| A/B 槽 | 当前活动槽 `_b`（安卓在 b 上跑） | 方案 A 写 `boot_b` 就是写**当前**安卓的启动分区；也可以改写不活动的 `boot_a`，再用 `set_active`。需要在设计里明确选哪一边。 |
| `boot_a` / `boot_b` 大小 | 各 201326592 B（192 MiB） | 足够 |
| 现有 iPadOS 临时启动镜像 | `pong-xnu-initial.img` 约 102.6 MB（payload FAT 96 MiB） | 192 MiB 内能放下“菜单 + 原装 GKI 内核 + iPadOS 格子”，但余量不大，要先量原装 boot 的内核实际大小 |
| 根文件系统 | 已经是 `/data` 上 pinned 的 `root.img` + `klog.pin`，物理块号写在 cmdline 里 | 与方案 A 完全同构；方案 A 的“位置指纹 + 看门模块”正好对应我们最大的风险 |
| 现有菜单 | uefi-zizhan 已经有 UniXNU 菜单，音量/电源键可用 | 可以在 UEFI 菜单里加“启动安卓”项（chainload 原装内核），也可以照方案 A 在 UEFI 之前做一个更小的选择器 |

**倾向方案 A**：不清空安卓数据，不改分区表，ABL 不动，撤销只需刷回原装 boot，而且根镜像/日志都是 pinned 文件，和我们现在完全一致。方案 B 需要清空 userdata 并重分区，风险和代价都更大。

## 3. M3 进场前需要用户决定的

1. 是否允许写一次启动分区（与现行 AGENTS.md“不刷分区”规则冲突，需要书面授权），写 `boot_a`（不活动槽）还是 `boot_b`（当前槽）。
2. 原装 `boot_a`/`boot_b` 的备份已有（10-28 全分区备份），写入前需要重新读回核对 hash。
3. 默认进安卓、按键进 iPadOS，还是反过来。

在这之前可以做、不碰手机的事：量原装 boot 结构（header 版本、内核/ramdisk 大小）、读 data-a-live 的 `bootmgr` 源码评估移植量，以及 `fastboot boot` 方式的不写入菜单测试设计。

## 4. 补充（2026-11-18）：mi8-dualboot-lab，热重启交接安卓（用户指定优先方案）

- 来源：<https://github.com/Hercules-Zhaoziyi/mi8-dualboot-lab>（小米 8 `dipper` / SDM845，iOS 16.5；基于 [dipper-xnu](https://github.com/youzjuzi/dipper-xnu)、SimpleInit 与 edk2-sdm845）。只公开菜单 UI 与交接组件（`HerculesBootMenu.efi/.ffs`、`AndroidHandoff.efi/.ffs`、源码），不含完整 boot.img 与 Apple 系统文件；有给 AI agent 的移植文档（`AGENTS.md`、`docs/PORTING.md`）。
- 形态：**一个 boot 分区** = 双启动 BootShim + 带 SimpleInit/LVGL 菜单的 UEFI + **原样保留的 Android 内核/DTB/ramdisk**。开机菜单默认 Android、10 秒倒计时，任意按键/触控取消本次倒计时；音量上下选择、电源确认，高级页有 UEFI Shell/重启/关机。
- **安卓交接（与本仓库 §2 / bootmenu-port §9–§10 的关键差别）**：菜单里的 `Reboot2PayloadApp` 向**特定物理地址写 magic 后 warm reset**；ABL 重新加载同一 boot，匹配的 BootShim 看到 magic 后直接跳进保留的 Android 内核。安卓因此拿到 ABL 当次生成的 FDT/cmdline/initrd，和原装开机等价，不需要 bootmenu-port §10 的“EFI stub 启动安卓 + live FDT 复制”。作者注明该交接只适用于匹配的 BootShim，换机型必须替换实现。
- 安装与撤销：先 `fastboot boot` 完整测试镜像（倒计时、取消、两个系统、原数据恢复），再**只写 boot**，全长回读一致，GPT 与 recovery 前后校验；撤销 = 刷回哈希核对过的原 boot。作者已实机使用。
- 不适用部分：它的 iOS 可写数据是上游 RAMFS + 双槽存档（容量很小），我们是 `/data` 上 pinned `root.img`；滑动关机卡圈未修。

### 决定（用户 2026-11-18）：若三项核实可行，M3 优先用这个方案（记为 A'）

A' = uefi-zizhan 现有菜单加“Android”入口 → 写 magic + warm reset → BootShim 交接原装 Android 内核。需要先核实（只读/主机，不碰手机）：

1. **warm reset 后 magic 是否保留、BootShim 能否据此跳转**：SM8475 warm reset 后哪块 DRAM/IMEM 在 ABL 重新加载 boot 后仍保持；uefi-zizhan 的 `BootShim/` 现在的入口逻辑，能否加“检测 magic → 以 ABL 原 x0 跳 Android Image”的分支；现有 `PongBootReset.h` 的 warm reset 进 fastboot 路径可作对照。
2. **header v4 + vendor_boot 的 ramdisk**：zizhan 的 ABL 会把 vendor_ramdisk + boot ramdisk + bootconfig 拼成 initrd；我们 boot 的 ramdisk 字段是 iOS 的 PONGFAT（96 MiB）。走 Android 时内核拿到的 initrd 不是安卓的——需要确认能否让 boot ramdisk 同时满足两边（例如 PONGFAT 放到别处、boot ramdisk 改为原装 cpio），或 BootShim 改写 `/chosen` 的 initrd 范围。
3. **空间**：192 MiB boot 内放 BootShim + UEFI + PONGFAT + 原装 Android 内核 + 原装 boot ramdisk + AVB 尾部的预算。

三项都可行 → 按 A' 设计 T1（`fastboot boot`，需用户在场按键）；任一项不可行 → 回到 bootmenu-port 的 A（EFI stub）/ B（裸机菜单）比较。
