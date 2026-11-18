# M3 方案 A′：warm reset 交接三项核实（2026-11-18）

状态：**只读/主机核实**。没有碰手机、没有写分区、没有构建/刷 UEFI，也没有
启动设备轮。下文所有“可行”是源码/镜像证据的结论；需要设备才能确认的项已标出。

## 0. 结论表

| # | 核实项 | 结论 | 关键证据 | 仍需确认 |
| --- | --- | --- | --- | --- |
| 1 | warm reset 后 magic 保留、BootShim 据此跳转 | **需设备确认**（机制可行，地址待定） | mi8 `Reboot2PayloadApp` 写 `0x17FE00000`；BootShim 读 128×`0x5241` 后清 magic、保留 x0；zizhan `PongBootReset.h:112-120` 的 PSCI `SYSTEM_RESET2` 已证明至少 ramoops DRAM 跨暖复位保留；UEFI `ArmPsciResetSystemLib.c:66-94` 的 `EfiResetWarm` 走同一个 PSCI 调用；10-28 原厂 UEFI 明确保留 `0xA7400000–0xA7600000` | `0xA7500000` 是否真的跨 ABL 重载保留；提议用 T1a 写/读回 |
| 2 | header v4 + vendor_boot 的 initrd | **可行**：A（boot ramdisk 换原装 cpio、PONGFAT 放 kernel field、UEFI 另找）推荐；B（保留 PONGFAT、BootShim 改写 `/chosen`）可行但代码多、保真度低 | 实测 ABL initrd span `0x168c76c` = vendor_ramdisk `0x14b1c20` + boot ramdisk `0x1da8de` + `0x48e` bootconfig；与 live DT `/chosen` 完全对上 | A 的 PONGFAT 传递/扫描落点要在 T1b 验证；B 的 FDT 改写需实机 |
| 3 | 192 MiB boot 空间 | **可行** | A 约 **147.7 MiB**；B 约 **168.4 MiB**；都小于 `201,326,592 B` | 实际对齐/AVB 尾部按最终打包器复核 |

按用户的决策规则，第 1 项不是“不可行”，而是“先用 T1a 把地址钉死”：T1a 通过
就继续 A′；T1a 不通过再换地址或回退到 B/A。不能跳过 T1a 直接写 boot。

**必须先说清的边界**：A′ 的 magic 分支发生在 ABL **重新加载 boot 分区**之后。
`fastboot boot` 的 RAM 镜像在 warm reset 后不会被 ABL 重新加载，ABL 会读回
flash 里的 boot。因此：

- `fastboot boot` 能验证菜单、按键、iOS 路径、magic 写入和 host 侧 BootShim；
- **不能单独证明 A′ 的 warm-reset 交接**。要证明它，必须把带 magic 分支的
  BootShim 写进 boot 分区（或写到未激活槽并切换），这需要用户授权、全分区备份
  和回退路径；它不属于本轮“只读/主机”工作。

## 1. warm reset、magic 与 BootShim

### 1.1 mi8 的实际协议（源码）

参考仓库 `mi8-dualboot-lab` @ `9dfb6f9`：

| 文件:行 | 事实 |
| --- | --- |
| `source/edk2-integration/Reboot2PayloadApp.c:12` | `LINUX_BOOT_ACQUIRED_MAGIC = 0x5241` |
| 同文件 `:20` | magic 物理地址 `0x17FE00000` |
| 同文件 `:22-25` | 连续写 128 个 16-bit `0x5241`（256 B） |
| 同文件 `:26-27` | `WriteBackInvalidateDataCacheRange(ptr,256)`，然后 `gRT->ResetSystem(EfiResetWarm,…)` |
| `source/device-adapters/dipper/BootShim.Dualboot.S:39-52` | 从 `0x17FE00000` 每次读 16-bit，最多 128 次，匹配 `0x5241` 就跳 `_ClearFlags` |
| 同文件 `:112-123` | `_ClearFlags` 用 32 个 64-bit 0 清掉 256 B，然后 `b _KernelStart` |
| 同文件 `:5-36` | `_KernelStart` 是给 patcher 填的分支和 arm64 Image 头；`text_offset=0x80000`、`image_size=0x0BADFACE`、`flags=0x2`、`UEFI_BASE/UEFI_SIZE/_KernelSize` 占位 |

BootShim 的 `_Start` 只使用 x1/x4-x7；**从未写 x0**，所以进入 `_KernelStart`
时 x0 仍是 ABL 的 FDT 指针。`_ChargingDecision` 在 `:159-167` 保存/恢复 x0。
mi8 的 magic 分支没有显式清 x1-x3；充电分支在 `:65-68` 显式把 x1/x2/x3
置 0。A′ 应对 Android 分支同样置 x1=x2=x3=0，x0 保留。

### 1.2 zizhan 的 warm reset 证据

- `drivers/ArmDTPlatform/PongBootReset.h:112-120`：`resetNow()` 先查
  `PSCI_FEATURES(SYSTEM_RESET2)`，再用 `SYSTEM_RESET2` type 0（warm）；
  同文件 `:100-101` 明确写“DRAM ramoops survives the warm reset”。
- `PongBootReset.h:258-261` 在每次 iOS 启动打印
  `PSCI SYSTEM_RESET2 supported (0), warm reset on`；设备历史日志已多次走这条
  路径回到 fastboot，说明 SM8475 固件接受该调用。
- `uefi-zizhan/Mu_Basecore/ArmPkg/Library/ArmPsciResetSystemLib/ArmPsciResetSystemLib.c:66-94`：
  `EfiResetWarm` 先查 `ARM_SMC_ID_PSCI_FEATURES(SYSTEM_RESET2_AARCH64)`，成功
  后发 `SYSTEM_RESET2`，`Arg1=0`（type 0）；失败才退回 cold。这与 XNU 使用的
  是同一个 PSCI 语义。
- `uefi-zizhan/Silicon/Silicium/SiliciumPkg/SiliciumPkg.dsc.inc:211-212` 把
  `ResetSystemLib` 映射到上面的 `ArmPsciResetSystemLib`；
  `Platforms/Xiaomi/zizhanPkg/Include/DXE.inc:20` 和 `APRIORI.inc:38` 包含
  `ResetSystemRuntimeDxe`。因此菜单里的 `gRT->ResetSystem(EfiResetWarm)` 走
  的就是这条 PSCI SYSTEM_RESET2 type 0 路径，而不是普通的 cold reset。
- `uefi-zizhan/Platforms/Xiaomi/zizhanPkg/Applications/RamoopsDump/RamoopsDump.c`
  在复位后从 `0x85200000` 读 ramoops；该地址在 live DT 的 `PIL Reserved`
  carveout 内。它是“暖复位后某块 DRAM 仍可读”的现成设备证据，但不是任意
  地址都保留的证明。

### 1.3 原厂 UEFI 的内存图（10-28 备份，只读）

从 10-28 备份的 `uefi_b.img.gz` 解压后只读 `strings` 提取到（不提交镜像）：

```text
0xA7000000, 0x00400000, "UEFI FD",           ...
0xA7400000, 0x00200000, "UEFI FD Reserved",  ...
0xA7600000, 0x00001000, "CPU Vectors",       ...
0xA760D000, 0x00040000, "UEFI Stack",        ...
0x85200000, 0x1DC00000, "PIL Reserved",      ...
```

这与 `uefi-zizhan/Platforms/Xiaomi/zizhanPkg/Library/MemoryMapLib/MemoryMapLib.c:17-19`
的 `FD Reserved / UEFI FD / UEFI FD Reserved` 布局一致。当前 port 的 BootShim
把 UEFI FD 复制到 `FD_BASE=0xA7100000`、长度 `FD_SIZE=0x300000`（`zizhan.toml`），
即 `0xA7100000–0xA7400000`；`0xA7400000–0xA7600000` 是原厂和本 port 都保留的
2 MiB。`0xA760D000` 起是 UEFI stack。

**候选 magic 地址：`0xA7500000`**（`UEFI FD Reserved` 中间，256 B
`0xA7500000–0xA7500100`）。理由：

- 不在 BootShim 的 FD 复制范围（结束于 `0xA7400000`）；
- 不在 UEFI stack（`0xA760D000` 起）；
- 不在 live DT 的 `ramoops@0xa7000000`（`0xA7000000–0xA7400000`）；
- 与原厂 UEFI 的保留区一致，ABL 不应把它当普通可用 DRAM。

不能直接复用 mi8 的 `0x17FE00000`：zizhan live DT 的低内存范围到
`0xEA000000`，高内存从 `0x800000000` 起；`0x17FE00000` 落在两者之间，**推测**
没有可用的物理 DRAM。高内存的 `HYP Reserved`（`0x830000000` 起 256 MiB，
`MemoryMapLib.c` 最后一项）是备选，但 BootShim 运行时 ABL 的页表是否映射
高 32 GiB 未确认，第一轮不要用。

IMEM（`0x14680000` / `0x146AA000`）在当前 UEFI 内存图是 MMIO；没有暖复位
保留证据，第一轮也不要用。

**结论**：magic→BootShim 的机制可行；`0xA7500000` 是 **需设备确认** 的候选。
T1a 必须先只验证“写 magic → warm reset → BootShim 读回”，不要直接上完整
Android 镜像。

### 1.4 当前 BootShim 与改动量

`uefi-zizhan/BootShim/BootShim.S:1-42` 现在只有：`_Head` 的 arm64 头、复制
`_Payload` 的 `FD_SIZE` 字节到 `FD_BASE`、`_Entry: br x5`。它已经保留 x0，
但没有任何 magic 检查、Android 分支或清 magic。

建议改动：

| 文件 | 改动 | 量级 |
| --- | --- | --- |
| `BootShim/BootShim.S` | `_KernelStart`/分支占位、`0xA7500000` magic 读取、匹配后清 256 B、`x1=x2=x3=0`、保留 x0 跳 Android；无 magic 时走现有 FD 复制 | **35–50 行** |
| `build_uefi.py` 或新的 A′ 打包器 | kernel field 改为非 gzip 布局；追加 Android Image/PONGFAT；patch `_KernelStart` 分支和 `kernel_size`；保留 DTB | **40–80 行** |
| UEFI 菜单/配置 | 新增 Android 动作：写 `0xA7500000` magic + `gRT->ResetSystem(EfiResetWarm)`；菜单项/默认项 | **20–40 行**（可复用 mi8 的 `Reboot2PayloadApp.c` 30 行） |
| `PongBootMemoryDxe.c`（A 方案） | 读 BootShim 传来的 PONGFAT 地址，扫描该范围；FDT span 作为回退 | **10–20 行** |
| `MemoryMapLib.c`（可选） | 把 magic/scratch 区标成保留，避免 UEFI 分配器复用 | **1–2 行** |
| XNU / `PongBootReset.h` | **不需要改**；A′ 的 reset 由 UEFI 菜单发起 | **0 行** |

合计约 **110–190 行**，主要风险不在行数，而在 `0xA7500000` 的实际保留和
BootShim 分支的绝对/相对地址 patch。

## 2. header v4 + vendor_boot 的 initrd

### 2.1 现有镜像的实测事实

只读解析 10-28 备份里的 boot 镜像副本（SHA-256 与原厂备份一致）：

| 项 | boot_a | boot_b |
| --- | --- | --- |
| 文件/分区 | 201,326,592 B | 同 |
| header v4 | 1584 B，page 4096 | 同 |
| kernel_size | 46,698,068 B | 同 |
| ramdisk_size | 1,380,092 B | **1,943,230 B** |
| boot signature | 4,096 B @ `0x2ddb000`（`AVB0`） | 4,096 B @ `0x2e65000` |
| vbmeta | 896 B @ `0x2ddc000` | 896 B @ `0x2e66000` |
| AVB footer | `0xbffffc0`，`original_image_size=0x2ddc000` | `0xbffffc0`，`0x2e66000` |

vendor_boot_b（v4）：vendor_ramdisk **21,699,616 B**（`0x14b1c20`），DTB
5,102,291 B，vendor_bootconfig 108 B（文本 85 B）。当前 iOS boot 的
ramdisk 是 `PONGFAT1` **100,667,392 B**（96.0 MiB），kernel 是
1,893,400 B 的 gzip UEFI + DTB（解压 3,145,840 B = 3 MiB FD + 112 B
BootShim；见 `docs/research/2026-11-17-m3-bootmenu-port.md` §2.3）。

### 2.2 ABL 的 initrd 组合已对上

本机只读 live DT 的 `/chosen`（只读采集；不提交 DTB）：

```text
linux,initrd-start = 0xb6972000
linux,initrd-end   = 0xb7ffe76c
```

span = `0x168c76c` = **23,644,012 B**：

```text
vendor_ramdisk_b  21,699,616
boot_b ramdisk     1,943,230
ABL bootconfig         1,166
=                  23,644,012
```

因此 ABL 确实把 **vendor_ramdisk + boot ramdisk + 动态 bootconfig** 拼成
initrd 并写进 `/chosen`；`1,166 B` 是动态 `androidboot.*` 尾部（vendor_boot
里的静态 bootconfig 只有 108 B）。这也解释了 `PongBootMemoryDxe.c:77-101`
为什么可以只按 `/chosen` 的 span 扫描 `PONGFAT1`。

### 2.3 两种让 Android 拿到正确 initrd 的做法

#### A（推荐）：boot ramdisk 换回原装 cpio；PONGFAT 挪到 kernel field

- 新 boot 镜像的 ramdisk 字段 = 原厂 boot_b ramdisk（1,943,230 B）。
  ABL 重新组合时，Android 内核拿到的就是原厂 initrd；`/chosen` 不需要
  BootShim 改。
- PONGFAT（96.0 MiB）放到 kernel field 的 UEFI FD/DTB 之后；BootShim 在
  进入 UEFI 前把它的物理地址写到固定 scratch（例如 `0xA75FF000`，也在
  `UEFI FD Reserved`）或一个新的 HOB。`PongBootMemoryDxe` 先读 scratch，
  从该地址扫描 `PONGFAT1`；没有 scratch 时回退到原来的 `/chosen` span。
- iOS 路径不变：BootShim 仍只复制 `FD_SIZE` 的 UEFI FD；PONGFAT 留在
  boot 镜像加载区，由 UEFI 的 `MakeReadable`/`Publish` 读取。
- 代价：kernel field 变成非 gzip（BootShim 必须直接看到 Android Image 和
  PONGFAT），现有 `kernel_compression="gzip"` 要改成 `"none"`；UEFI FD 仍是
  3 MiB，PONGFAT 仍是 96 MiB，空间足够（见 §3）。

#### B（回退）：保留 PONGFAT 在 ramdisk；BootShim 改写 `/chosen` initrd

- 预拼一份 initrd = vendor_ramdisk + 原厂 boot ramdisk + bootconfig
  （23,644,012 B），放进 kernel field；BootShim 在 magic 分支里找到 ABL FDT
  的 `/chosen`，把 `linux,initrd-start/end` 改成预拼 initrd 的地址/结束，
  再以 x0=FDT、x1=x2=x3=0 跳 Android。
- 原厂 `/chosen` 已有这两个属性（live DT 是 8 字节值），所以只需原地改写，
  不需要扩展 FDT；但仍需 FDT 结构解析（找 `/chosen`、找属性），比 A 多
  **80–150 行**。
- 保真度较低：bootconfig 是打包时快照，OTA/槽位/`vbmeta.digest` 变化后
  可能过期；A 让 ABL 每次自己拼，等价性最好。

#### C（不推荐，仅对照）

保留一个原厂 Android boot 槽、另一个 iOS 槽，靠切换 slot 选择。它需要写
`misc`/slot 元数据或改 ABL 选择逻辑，且 AVB/槽位语义变化；不是 A′ 的
“一个 boot + magic 交接”，本页不展开。

**结论**：A 可行且最保真，推荐；B 可行但代码多、bootconfig 可能过期，作为
A 失败时的回退。两种都保留原厂 boot header v4 的 ramdisk 语义，不需要改
vendor_boot 分区。

## 3. 192 MiB 空间预算

分区 `201,326,592 B`。固定尾部按 boot_b：header 4,096 + boot signature
4,096 + vbmeta page 4,096 + footer page 4,096。Android 内核按 2 MiB 对齐，
其余按 4 KiB 对齐（保守）：

| 项 | 字节 | MiB |
| --- | ---: | ---: |
| BootShim.bin（实测） | 112 | 0.0001 |
| UEFI FD（`FD_SIZE=0x300000`） | 3,145,728 | 3.000 |
| 原厂 DTB（append） | 898,595 | 0.857 |
| UEFI+DTB 小计（4 KiB 对齐） | 4,046,848 | 3.859 |
| 原厂 Android Image | 46,698,068 | 44.535 |
| 同上，2 MiB 对齐 | 48,234,496 | 46.000 |
| PONGFAT（96 MiB） | 100,667,392 | 96.000 |
| boot_b ramdisk | 1,943,230 | 1.853 |
| 预拼 initrd（B 用，A 不用） | 23,644,012 | 22.548 |
| boot signature | 4,096 | 0.004 |
| vbmeta | 896（占 4,096 页） | 0.0009 |
| AVB footer | 64（末尾页） | 0.0001 |

### A：ramdisk=原厂 cpio，PONGFAT 在 kernel field

- kernel field 预算：`201,326,592 - 4,096 - 1,945,600 - 4,096 - 4,096 - 4,096`
  = **199,364,608 B（190.13 MiB）**。
- 已用：4,046,848 + 48,234,496 + 100,667,392 = **152,948,736 B（145.86 MiB）**。
- 余量：**46,415,872 B（44.27 MiB）**。
- 整镜像（含 header/ramdisk/signature/vbmeta）：**154,903,424 B（147.73 MiB）**，
  小于 192 MiB，也远小于 10-28 记录的 `max-download-size=805306368`。

### B：ramdisk=PONGFAT，预拼 initrd 在 kernel field

- kernel field 预算：`201,326,592 - 4,096 - 100,667,392 - 4,096 - 4,096 - 4,096`
  = **100,642,816 B（95.98 MiB）**。
- 已用：4,046,848 + 48,234,496 + 23,646,208 = **75,927,552 B（72.41 MiB）**。
- 余量：**24,715,264 B（23.57 MiB）**。
- 整镜像：**176,604,032 B（168.42 MiB）**，仍小于 192 MiB。

**结论**：A/B 都放得下；A 余量约 44 MiB，B 约 23.6 MiB。真正的限制不是
空间，而是“warm reset 后 BootShim 能否读到 magic”和“BootShim 能否按
ABL 的 x0/initrd 语义跳转”。

## 4. 若可行：T1 测试镜像怎么组装、用户在场做什么

### 4.1 先做 T1a：只验证 magic 跨 warm reset 保留

目的：在写 Android 镜像前，单独确认 `0xA7500000` 没有被 ABL 清掉。

1. 用 host 构建一个最小 UEFI test app（不刷机）：写 256 B 的
   `0xA7500000` magic（例如 64-bit `0x5A495A48414E4431`，重复 32 次），
   清 cache，`gRT->ResetSystem(EfiResetWarm,…)`。
2. BootShim 加最小分支：读到 magic 后不跳 Android，而是把 magic 清掉并把
   `0xA7500100` 的 `0x5A495A48414E4432` 写回，然后继续原 UEFI 路径；UEFI
   启动后由菜单/日志显示“magic survived”。如果 magic 不保留，BootShim 走
   原 UEFI，不产生副作用。
3. 需要用户在场：`fastboot boot` 这个 test image，确认菜单/日志。

**注意**：即使 `fastboot boot` 加载了 test image，warm reset 后 ABL 仍会
从 flash boot 重新加载。T1a 要真正跨 ABL，仍需要把 BootShim 写到 boot
分区（或未激活槽）。如果用户暂不授权写 boot，T1a 只能做 host 侧模拟，
**不能**把结论升级为“设备验证”。

### 4.2 T1b：完整 A′ 镜像（需要一次 boot 写入授权）

在 T1a 通过、全分区备份和回退路径确认后：

1. host 组装 boot v4：
   - kernel field（非 gzip）= BootShim（patch 好 magic/Android 分支）+
     UEFI FD + DTB + PONGFAT + 原厂 Android Image；或按 A 的顺序把
     PONGFAT 放在 UEFI FD/DTB 后、Android Image 前。`kernel_size` 写完整
     field；Android Image 的入口由 BootShim 分支 patch 指向。
   - ramdisk field = 原厂 boot_b ramdisk（1,943,230 B）。
   - boot signature/vbmeta/footer 按新 `original_image_size` 重算或保留
     原厂尾部；`fastboot boot` 不验 AVB，但将来写分区必须一致。
   - 菜单配置加入 Android 项；默认可以是 iOS，Android 项写 magic 后
     `EfiResetWarm`。
2. 先 `fastboot boot` 同一镜像：验证菜单、按键、iOS 启动、Android 项能
   写 magic；**不要**把这一轮当作 A′ 交接 PASS。
3. 用户书面授权后，把镜像写到未激活槽（当前 `_b`，建议 boot_a）并用
   `fastboot set_active a` 或等价方式让 ABL 选它；或备份后写 boot_b。
   保留原 boot_a/boot_b 哈希和回退命令。
4. 重启进入菜单，选 Android。菜单写 `0xA7500000` magic + `EfiResetWarm`；
   ABL 重新加载 boot，BootShim 读到 magic，清 256 B，置 x1=x2=x3=0，
   x0 保持 ABL FDT，跳原厂 Android Image。
5. 用户在场检查：
   - Android 能过 kernel/init，`/proc/cmdline` 与 stock 一致；
   - `/proc/bootconfig` 含 `androidboot.slot_suffix=_b`、`vbmeta.digest`、
     `verifiedbootstate` 等动态项；
   - initrd 来自 `vendor_ramdisk + boot ramdisk + bootconfig`，不是 PONGFAT；
   - 无 panic、无 PMIC/显示写；magic 已清，下一次正常启动回到 iOS/原路径；
   - 失败时保留本次日志，不改补丁硬绕，按备份回退。

### 4.3 验收与停止条件

- T1a 失败（magic 不保留）：换 `0x8_3xx` 高内存 `HYP Reserved` 或改走
  方案 B/A；不要把同一地址重试到无日志。
- T1b Android 不启动：先区分“magic 没读到”和“读到了但 x0/initrd 不对”；
  前者回 T1a，后者检查 BootShim 的 x0 保留和 A 的 PONGFAT/ramdisk 布局。
- 任何写 boot 前必须有全分区备份、原 boot 哈希和回退路径；本轮不执行。

## 5. 未完成与推测

- **需设备确认**：`0xA7500000` 跨 ABL 重载是否保留；高内存 `HYP Reserved`
  是否被 BootShim 的页表映射；ButtonsDxe 的音量/电源键在 zizhan 菜单里的
  实际映射。
- **推测**：ABL 不会把 boot image 加载到 `0xA7400000–0xA7600000` 的 UEFI
  保留区；这是基于原厂 UEFI 内存图和当前 initrd 地址（`0xB6972000`）的判断，
  不是设备实测。
- **未做**：内核/UEFI 构建、任何 fastboot/adb、任何分区写、Android 实机
  启动、`in[0]`/selector 12（那是 POWER-02，不属于本页）。
- 参考仓库只读克隆在 `$ZIZHAN_SRC/work/codex-therm/ref/`（不进 git）；
  10-28 备份、原厂 boot/vendor_boot/uefi 镜像只在主机读取，不进 git。
- 参考提交：<https://github.com/Hercules-Zhaoziyi/mi8-dualboot-lab> @
  `9dfb6f9492a6b129bdb6342ac1a2d89cbd4c21a5`（2026-11-15）；
  <https://github.com/youzjuzi/dipper-xnu> @
  `2854c1e22fc2d73bb7a890c2c84b80182ea5ad18`（只读克隆，用于交叉核对
  dipper 的启动/安装约束；本轮结论主要来自 mi8 源码和 zizhan 自己的镜像）。
