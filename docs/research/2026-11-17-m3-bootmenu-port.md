# M3 开机菜单移植评估：data-a-live `bootmgr` → zizhan（2026-11-17）

状态：**只读评估 + 电脑端解析**。没有写手机、没有 `fastboot`、没有构建内核/镜像。
本文回答一个问题：能不能把 data-a-live 的 `bootmgr`（一次写入 boot 分区，开机倒数按音量上进 iOS）移植到 zizhan；如果能，改哪里、怎么验证、风险是什么、需要用户决定什么。

引用基线：

- data-a-live：`https://github.com/yechenan/data-a-live`，提交 `7c779bd99b918c1820235442a41fec0c000afb60`（2026-11-11）。下文行号均指该提交的 `bootmgr/`。
- zizhan 备份：`$ZIZHAN_SRC/../backup/2026-10-28-V14.0.15.0/boot_a.img.gz`、`boot_b.img.gz`（各 201,326,592 B，即 192 MiB 分区整镜像）。
- zizhan 设备树：`$ZIZHAN_SRC/work-dt/live.dts`（2026-11-01 从手机导出的 live FDT）与本次只读 `/proc/device-tree` 查询。
- 现有 iPadOS 临时启动镜像：`out/statusbar-inner-1116-v3/pong-xnu-initial.img`（102,572,032 B）。

## 1. 结论

**可以移植，而且分区结构、ABL 交接协议、AVB 布局都兼容；但不能原样移植。** 有四处 zizhan 专属差异必须先改，其中“音量上”的读取方式是新写代码量最大、风险最高的一处。

| 结论 | 依据 |
| --- | --- |
| boot v4 + AVB footer + `AVB0` vbmeta 的结构与 data-a-live 相同 | 见 §2 |
| ABL 的 `/chosen` 提供 8 字节 `linux,initrd-start/end`，满足 `layout_ok()` 的前提 | 设备只读：两属性各 8 B；`menu.c:287-298` |
| 原装 boot 有 ramdisk；data-a-live 的原装 boot 没有 | 见 §2；`prep.py:22` 断言 `rsize == 0` |
| zizhan 的 iPadOS 负载不是裸 arm64 Image，而是 `gzip(UEFI 固件) + PONGFAT 96 MiB ramdisk` | 见 §2.3 |
| 帧缓冲可以直接用 `splash_region`（0xb8000000，2160×1916） | `live.dts:422-426`、dmesg `hactive=2160 vactive=1916` |
| 音量上是 **PM8350 GPIO6（SPMI）**，不是 TLMM GPIO | `live.dts:32346-32353`、`pinctrl@8800` |
| ABL bootargs 里没有 data-a-live 用来判断“正常开机”的三个词 | 设备只读 `/proc/device-tree/chosen/bootargs` |

推荐路线（按风险从低到高）：

1. **先做不写入的 `fastboot boot` 探针**，一次证明/否证三个未知：ABL 入口 EL、`cntvct_el0` 是否可用、PM8350 GPIO6 是否能在裸机菜单里读到。
2. 探针通过 → 按 §6 的 B 方案移植 `bootmgr`，只写一次 boot 分区。
3. 探针失败（PMIC 读不到，或 EL/计数器不可用）→ 转 A 方案：在现有 uefi-zizhan 菜单里加“启动安卓”项，复用已经工作的按键和显示。

## 2. zizhan 原装 boot 镜像结构（电脑端实测）

### 2.1 boot header（v4）

`boot_a.img` / `boot_b.img` 头部一致，只有 ramdisk 大小不同：

| 字段 | boot_a | boot_b |
| --- | --- | --- |
| magic / header_version | `ANDROID!` / 4 | 同 |
| header_size | 1584 | 1584 |
| kernel_size | 46,698,068 B（44.53 MiB）@0x1000 | 同 |
| ramdisk_size | 1,380,092 B（1.32 MiB） | 1,943,230 B（1.85 MiB） |
| os_version | 0x18000173 | 同 |
| cmdline | 空（0 字节） | 空 |
| v4 signature_size | 4096 B | 4096 B |
| kernel 头 | `4d5a`(MZ, EFI stub)，偏移 56 为 `ARMd` | 同 |
| Image `text_offset` / `image_size` / `flags` | 0 / 0x2a70000 / 0xa | 同 |

结论：kernel 是合法的裸 arm64 Image（EFI stub 不影响 `prep.py:24` 的 `kernel[56:60] == "ARM\\x64"` 断言），但 **boot 带 ramdisk**，不能照抄 data-a-live 的 `rsize == 0` 假设。

### 2.2 AVB 与分区尾部

以 boot_a 为例（boot_b 只差 ramdisk 大小）：

| 区域 | 偏移 | 大小 |
| --- | --- | --- |
| header page | 0x0 | 4 KiB |
| kernel | 0x1000 | 46,698,068 B |
| ramdisk | 0x2c8a000 | 1,380,092 B |
| boot signature（`AVB0`） | 0x2ddb000 | 4 KiB |
| vbmeta（`AVB0`） | 0x2ddc000 | 896 B |
| AVB footer（`AVBf`） | 最后 64 B（0xbffffc0） | `original_image_size=0x2ddc000` |

data-a-live 的 `mkboot.py:55-92` 只保留“stock vbmeta + 新 footer”；zizhan 还要额外保留 **4 KiB boot signature**，否则分区布局会和原装 boot 不一致（签名本身在解锁状态不验，但保留可以少一个未知）。

### 2.3 现有 iPadOS 镜像的真实形态（关键差异）

`out/statusbar-inner-1116-v3/pong-xnu-initial.img` 解析：

| 字段 | 值 |
| --- | --- |
| kernel_size | 1,893,400 B；前 994,805 B 是 gzip 流，后 898,595 B 是以 `d00dfeed` 开头的 **DTB** |
| gzip 解开后 | 3,145,840 B（3.00 MiB）：BootShim + UEFI 固件 |
| ramdisk_size | 100,667,392 B（96.0 MiB）；头部是 `PONGFAT1`，里面是 FAT16 + `kernel.macho`(78.6 MB) + ADT |

近期 5 个 `out/*/pong-xnu-initial.img`（含 `statusbar-inner-1116-v3`、`weather-location-v25`）的 `kernel_size`/`ramdisk_size` 完全一致（1,893,400 / 100,667,392），所以下面的结构结论不是单个构建的偶然值。

这和 data-a-live 的“iOS 格子 = 一个裸 arm64 Image”不是同一种东西：

- zizhan 的“内核”是 **gzip 压缩的 UEFI 固件**（`uefi-zizhan/build_uefi.py:274-301` 的 `kernel_compression = "gzip"`，并 `append_dtb`）；
- zizhan 的 XNU 负载在 **ramdisk** 里（`PONGFAT1`），由 UEFI 的 initrd 驱动读取（`uefi-zizhan` 的 `PongBootMemoryDxe` 从 `/chosen` 取 initrd）；
- data-a-live 的 `slot.py:92-125` 要求负载是裸 `SWEETXNU` Image，`menu.c:468` 直接 `jump_linux(slot + SLOT_HDR, fdt)`。

所以移植时“格子”里要放的是：**解压后的 BootShim+UEFI（3.0 MiB）+ 原样 DTB（0.86 MiB）+ 原样 PONGFAT（96.0 MiB）**，并且菜单在跳转前把 `/chosen/linux,initrd-start/end` 改成指向格子里的 PONGFAT。这样 UEFI 仍然按今天的方式找到 XNU 负载，现有 iOS 链路一行不用改。

### 2.4 与设备当前 boot 分区核对（只读，2026-11-17 20:00）

| 分区 | 设备 `su -c sha256sum /dev/block/by-name/...` | 10-28 备份解压后 SHA-256 | 结果 |
| --- | --- | --- | --- |
| `boot_a` | `f0f85a1bacc3ebae743633db390241032d34d1fe43930a14078832a2739505d0` | 同左 | **一致** |
| `boot_b` | `754e6bd04ab9f7a24cd26b4b9aa5c974ff6333156c98aa30317d61a2c1f1a7c3` | 同左 | **一致** |

当前活动槽 `ro.boot.slot_suffix=_b`，即安卓正在用 boot_b。两条命令都是只读 `sha256sum`，没有写任何分区。

## 3. data-a-live `bootmgr` 是什么形态

### 3.1 形态

不是 UEFI，也不是 Linux 内核；是 **ABL 直接当 arm64 Image 加载的裸机程序**（`start.S:1-19`：标准 arm64 Image 头，`text_offset=0`，`flags=0xa`；入口 `_start` 设 `VBAR_EL1/EL2`、清 BSS、调 `menu_main`）。MMU 关闭，所有访问都是 Device memory（`-mstrict-align`，`menu.c:18-19`）。

### 3.2 布局与交接

- `mkboot.py:54-92`：kernel field = 菜单代码 | 设置页 | 2 MiB 处的原装安卓内核 | iOS 格子 | 原装 vbmeta + 新 footer。
- 菜单不解析 boot header；它靠自己的 Image 头 `_image_end`（`start.S:14`）和设置页里的 `slot_off/slot_max/save_off` 找格子。
- 跳安卓：`start_android()`（`menu.c:416-434`）检查 `ARMd`，按 `text_offset` 做 2 MiB 对齐搬移，然后 `jump_linux(dst, fdt)`。
- 跳 iOS：`menu()` 返回 1 后 `jump_linux(slot + SLOT_HDR, fdt)`（`menu.c:465-469`），和 ABL 进内核完全一样（x0=FDT，x1-x3=0）。

### 3.3 按键与显示

- 音量上：**TLMM GPIO 101**，寄存器 `0x0f165000`（`menu.c:34-36`、`346-356`）。先 `safe_rd32` 检查 config 的 FUNC/OE 位，再读 +4 的 bit0。读总线异常由 `sync_fault` 捕获并回退，不会直接复位（`start.S:85-124`）。
- 显示：硬编码 `FB=0xfc800000`，1220×2656，stride 1220×4（`menu.c:28-31`）；只画开机 logo 下方一条带，先保存再恢复（`band_copy()`，`menu.c:188-198`）。
- 计时：`cntvct_el0`/`cntfrq_el0`（`menu.c:78-97`）。

### 3.4 正常开机判定

`normal_mode()`（`menu.c:269-278`）只认三个 bootargs 词：`bootmonitor.bootmode=normal`、`mca_log.charge_boot_mode=0`、`silent_boot.mode=nonsilent`。zizhan 的 `/proc/device-tree/chosen/bootargs`（1099 B）里**这三个词一个都没有**；zizhan 的 ABL 也不提供等价物（只有 `bootinfo.pureason=0x80001`、`bootinfo.pdreason=0x2`，且这两项位于 bootconfig 段，不是 ABL bootargs）。移植时这一段必须重写。

### 3.5 换 iOS 与看门

- `update_ios.sh`：先不带 `--yes` 检查，再带 `--yes` 写。手机端 `phone/update_helper.sh` 的顺序是“清头 → 写内容 → 写头 → 读回”；格子以外区域写前写后 hash 必须一致。写坏只会让格子为空，安卓仍能启动。
- 格子头 `bm_slot`（`menu.c:52-61`）：`armed`、`size`、`sum`（自定义 8 字节字校验）、三个 pinned 文件 extents 的 SHA-256、`why[]`。
- 看门：Magisk 模块 `guard/`，开机时和之后每分钟用 `pinfile extents` 比对三个文件；不符就把 `armed` 清 0 并写原因（`guard/guard.sh` 的 `disarm()`）。菜单随后显示 `iOS off: <原因>` 并进安卓。
- zizhan 的对应物：根镜像和 klog 已经都是 `/data` 上 pinned 的文件，cmdline 里带物理块号；这与 data-a-live 的威胁模型完全一致，看门模块可以照搬，只需把三个文件名换成 zizhan 的 root/klog（以及将来可能有的 adspfw）。

## 4. zizhan 差异清单（移植必须改的部分）

| # | 项目 | data-a-live | zizhan | 处理 |
| --- | --- | --- | --- | --- |
| 1 | 原装 boot ramdisk | 无（`prep.py:22`） | 有，1.32/1.85 MiB | `mkboot.py` 必须保留 ramdisk 并把 field 缩小到给它留位置 |
| 2 | boot signature | 无 | 4 KiB `AVB0` @0x2ddb000 | 原样保留，放在 ramdisk 之后 |
| 3 | 内核压缩 | 裸 Image | iPadOS 侧是 gzip(UEFI)+DTB；安卓侧是裸 Image | 格子里存解压后的 UEFI+DTB；安卓侧不改 |
| 4 | iOS 负载位置 | 格子里的裸 Image | PONGFAT ramdisk（96 MiB） | 菜单跳转前把 `/chosen` initrd 指到格子里的 PONGFAT |
| 5 | 帧缓冲 | 0xfc800000，1220×2656 | `splash_region` 0xb8000000，2160×1916，stride 8640 | 改常量；仍用 `band_copy` 只改 logo 下方一条带 |
| 6 | 音量上 | TLMM GPIO101，`0x0f165000` | **PM8350 GPIO6**（SPMI，`pinctrl@8800`） | 必须新写 SPMI/PMIC-arbiter 只读路径；见 §5 |
| 7 | 正常开机判定 | 3 个 bootargs 词 | 不存在 | 首版去掉判定（总是显示），或改成“任何模式都显示 5 秒” |
| 8 | 入口 EL | 只允许 EL1（`menu.c:465`） | 未实测；`kvm-arm.mode=protected` 暗示 EL2 | 首版去掉 `el == 1` 限制，EL1/EL2 都显示；探针里打印实际 EL |
| 9 | 计数器 | `cntvct_el0` | 未实测 EL2 是否 trap | 探针里读一次；失败则改用 `pmk8350` RTC 或简单循环 |
| 10 | 分区大小 | 96 MiB | 192 MiB | 空间充足，见 §6 |

## 5. 音量上：为什么是最大风险

zizhan 的 `gpio_keys/vol_up` 是：

```
gpios = <0x57b 0x06 0x01>;   /* PM8350 GPIO6, active low */
linux,code = <0x73>;          /* KEY_VOLUMEUP */
```

`0x57b` 是 `pinctrl@8800`（`compatible = "qcom,pm8350-gpio"`），挂在 SPMI PMIC arbiter `qcom,spmi@c42d000` 下。也就是说，按键状态不在 AP 的 TLMM 寄存器里，不能像 data-a-live 那样一条 `ldr` 就读到。

可选实现：

1. **SPMI 只读**（B1）：在菜单里实现 PMIC arbiter 的观察寄存器读（`spmi-pmic-arb.c` 的 v2/v7 命令序列），读 PM8350 GPIO6 的 status。风险：pre-kernel 阶段读 SPMI 若时序不对，可能挂总线；必须用 `safe_rd32` 同级的异常回退 + 有界重试，任何失败都回安卓，不 reset。
2. **复用 UEFI 输入**（A）：`uefi-zizhan` 的 UniXNU 菜单已经用 `gST->ConIn->ReadKeyStroke` 工作（`Platforms/Nothing/UniXNUPkg/Library/UniXNULib/Menu.c:166-205`），音量/电源键可用。把选择器放在 UEFI 里就不需要碰 SPMI。
3. **无按键版本**（B0，最小可用）：开机固定 5 秒 → 安卓；在安卓里用 root 写一个“下次进 iOS”的标志（例如 boot 分区尾部预留页），菜单读标志决定。代价是每次切系统要先进安卓，不是“开机时按键选择”。

本文建议：先做 §7 的探针；若 SPMI 读在探针里稳定，选 B1；否则选 A（复用 UEFI 菜单）。B0 只作为 A/B 都受阻时的退路。

## 6. 空间预算（192 MiB 分区，按 boot_b 计算）

可用 kernel field：

```
201,326,592
  -   4,096   header page
  - 1,945,600 stock ramdisk（1,943,230 向上取整到 4 KiB）
  -   4,096   boot signature
  -   4,096   vbmeta page（896 B）
  -   4,096   AVB footer page
= 199,364,608 B = 190.13 MiB
```

需要的 field：

| 项 | 大小 |
| --- | --- |
| 菜单（裸机，含字体/设置页/1 MiB logo 保存区） | ≤ 2 MiB |
| 原装安卓内核（2 MiB 对齐放入） | 44.53 MiB |
| iOS 格子：UEFI 3.00 MiB + DTB 0.86 MiB + PONGFAT 96.00 MiB | 99.86 MiB，按 64 KiB 对齐 |
| **合计** | **约 146.4 MiB** |

余量约 **43.7 MiB**。即使将来 iOS 负载长到 120 MiB 仍能放下；但 `kernel_size` 必须一次写死为 field 大小（`mkboot.py:82-88` 的做法），换 iOS 时只改格子。

注意：`fastboot boot` 要把这 146.4 MiB 一次性下载进内存。10-28 的设备准备记录已存 `max-download-size: 805306368`（768 MiB，见 `docs/progress/2026-10-28-device-prep.md:10`），远大于本方案；本次没有再次运行 fastboot 查询，测试前可用该记录作预算依据。

## 7. 验证阶梯（全部先不写入）

### 7.1 T0：主机解析（已完成）

- boot header/AVB/ramdisk 偏移：本文 §2。
- 现有 iPadOS 镜像的 gzip/DTB/PONGFAT 结构：本文 §2.3。
- data-a-live 源码与 zizhan DT 对照：本文 §3–§5。

### 7.2 T1：裸机探针（`fastboot boot`，不写分区）

镜像内容：菜单 + 设置页 + 原装内核；**不装 iOS 格子**；`--any-mode`。屏幕顶部打印：

```
LOADED / IMGSIZE / FDT / FDTSIZE / INITRD / INITRDEND
EL / CNTFRQ / CNTVCT（读两次）
PMIC: PM8350 GPIO6 status（读 5 次，显示 fault 位）
VOLUP: 0/1
```

验收：

- 屏幕出现且 9 秒后自动进安卓（说明 ABL 加载了 145 MiB field、入口 EL 可用、计数器可用）；
- 按住音量上时 `VOLUP: 1`（说明 SPMI 读路径可用）；
- 不按音量上时 9 秒后原装安卓正常启动（说明 `jump_linux` + ABL FDT + ramdisk 交接正确）；
- 任何一项失败都必须回到安卓，不允许 panic/halt/死循环。

这是 B 方案能不能成立的分水岭；也是唯一一次需要用户授权 `fastboot boot` 的测试（不写 flash）。

### 7.3 T2：iOS 路径（`fastboot boot`，不写分区）

在 T1 通过后，把 iOS 格子（UEFI+DTB+PONGFAT）装进测试镜像，`--any-mode` + 按住音量上：

- 菜单校验格子头/大小/自定义 sum；
- 菜单把 `/chosen` initrd 改到格子里的 PONGFAT；
- 跳进 UEFI，UEFI 按今天的方式加载 XNU；
- 进入 iPadOS 桌面（与现有 `fastboot boot pong-xnu-initial.img` 的画面对比）。

如果 iOS 起来了，说明“格子 + initrd 改写 + UEFI 链路”全部成立，剩下的只是把同一个镜像写进 boot 分区。

### 7.4 T3：写入（唯一一次分区写，需用户书面授权）

前置：

1. 10-28 全分区备份在位；写入前用 `su -c sha256sum /dev/block/by-name/boot_b` 与备份解压后的 SHA-256 逐字节核对；
2. 当前活动槽是 `_b`；写 `boot_b`（当前槽）还是 `boot_a`（不活动槽）+ `set_active` 必须由用户决定。建议写 `boot_b`：不引入 system/vendor 换槽这个额外故障域，撤销就是刷回原装 boot_b；
3. 返回路径：音量下 + 电源进 fastboot（ABL 不动，这条路一直在）；原装 boot_b 镜像放在电脑上，文件名/哈希写进操作记录；
4. 命令行/超时/自动返回：本项不涉及内核 cmdline，但任何设备轮都必须有 `pong_autoreset` 或人工返回路径；本次建议有人在场，写入后立刻重启验证安卓，再重启验证 iOS。

## 8. 撤销与风险

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| 菜单代码在 EL2 读 `cntvct_el0` 被 trap | 进菜单即复位，安卓也起不来 | T1 探针先验证；失败改用 RTC/循环 |
| SPMI 读 PM8350 挂总线 | 开机卡死 | 有界重试 + 异常回退；任何 fault 直接进安卓；T1 先验证 |
| ABL 对 145 MiB kernel field 有限制 | `fastboot boot` 失败 | T1 先验证；若失败，把 iOS 格子压缩或减少 FAT |
| 写入后安卓起不来 | 手机只能进 fastboot | 备份 + 音量下+电源；刷回原装 boot_b |
| 格子里的 PONGFAT 被移动/改写 | iOS 读写错块，损坏安卓数据 | 照搬 `guard/`：pinned 文件 + 每分钟核对 extents；不符就 `armed=0` |
| boot 分区被 OTA 换槽/覆盖 | 菜单消失 | data-a-live 已记录：系统更新会换槽并写新 boot，菜单自然消失，安卓照常 |
| `fastboot boot` 下载超限 | 测试镜像被截断 | 已存记录最大 768 MiB，146.4 MiB 余量充足；若 ABL 另有 kernel field 上限，由 T1 探针暴露 |

撤销：`fastboot flash boot_b <原装 boot_b.img>`。ABL、vbmeta、vendor_boot、dtbo、init_boot 都不动。

## 9. A 方案（备选）：在现有 UEFI 菜单里加“启动安卓”

如果 T1 的 SPMI/EL 探针失败，A 方案不需要裸机输入/显示：

- 现有链路：ABL 加载 gzip(UEFI) + PONGFAT ramdisk → BootShim 把固件复制到 FD_BASE → UniXNU 菜单（`Menu.c`）用 ConIn 选条目 → DarwinBoot 启 XNU。
- 要加的是一条 “Android” 条目：把原装内核放到 boot 镜像的已知偏移（例如 field 内 2 MiB 处），UEFI 在 ExitBootServices 后关 MMU、按 arm64 协议跳过去（x0=ABL FDT，x1-x3=0），initrd 保持 ABL 的 stock ramdisk。
- 需要新增：BootShim 把“原始 field 基址”传给 UEFI（现在 `BootShim.S:19-34` 只保留 x0=FDT，没有保存自己的加载地址），以及一个 `UniXNUActionLinux` 的跳转例程。
- 代价：UEFI 固件 3 MiB + EDK2 启动时间；好处：按键/显示/菜单全部现成，且不需要 SPMI。

## 10. 需要用户决定

1. 是否授权在 T3 写一次 boot 分区；写 `boot_b`（当前槽，推荐）还是 `boot_a`（不活动槽）+ 切槽。
2. 默认进哪个系统：安卓（推荐，iOS 需按键）还是 iPadOS。
3. 是否接受“先做 T1 裸机探针”的一次 `fastboot boot`（不写 flash）。
4. 如果 SPMI 读不通，是否接受 A 方案（改 UEFI 菜单）或 B0（安卓里设标志，开机不按键）。
5. iOS 格子的更新方式：沿用 `update_ios.sh` 的“清头→写内容→写头→读回”，还是先只支持整镜像重刷。

## 11. 下一步（最小可执行）

1. 在 `$ZIZHAN_SRC/work/codex-1117/` 里做 T1 探针镜像（复用 data-a-live `menu.c`，改 FB/EL/normal_mode，加 PMIC 读取和调试行）；只构建，不写手机。
2. 用户授权后，后部 USB-A `fastboot boot` 跑一次 T1，记录屏幕照片、EL、CNTVCT、PMIC 读数、安卓是否正常返回。
3. 按 T1 结果在 B1 / A / B0 中定一条，再进入 T2。

