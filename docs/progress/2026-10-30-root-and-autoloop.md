# 2026-10-30 根文件系统与免按键闭环

本文接着 [XNU 首次上机](2026-10-29-xnu-bringup.md)。这一轮起，构建机换成 Mac Studio（`ssh mac`），摄像头换成局域网拍照页面。

## 1. 结果

| 项 | 结果 |
|---|---|
| k4（预链接，8 核，无 UFS） | ✅ 8 个 CPU 全部启动，k3 的 `cpu < ml_get_max_cpu_number()` 没再出现。APFS、CoreAnalyticsHub 起来后，停在 `Waiting on … IOMedia … Apple_HFS`，随后 panic `Failed to mount root device @IOKitBSDInit.cpp:996`（没有存储，属预期） |
| iPadOS 用户态镜像 | ✅ 在 Mac 上用 22H374 IPSW 做出 OS（6.4 GiB）、SystemOS（5 GiB，普通 APFS 副本）、Data（8 GiB） |
| 根文件系统放在安卓 /data 里 | ✅ u2：OS、SystemOS、可写 Data 三个卷全部挂载；原厂 22H374 launchd 启动，backboardd、SpringBoard 被拉起（见第 3 节） |
| 免按键闭环 | ✅ u2、u3 全程无人操作：看门狗复位进 fastboot，RamoopsDump 写 logfs，回到安卓后自动取回日志（见第 4 节） |
| u2：SpringBoard 崩溃 | 运行 130 s 后 SIGSEGV，之后反复重启。从安卓把 Data 窗口原样读回 Mac 挂载，崩溃报告显示状态栏 `STUIStatusBarVisualProvider_Fallback` 的 Auto Layout 无限递归，栈溢出 |
| **u3：iPadOS 主屏** | ✅ ADT 的 `display-scale`/`artwork-scale-factor` 从 3 改成 2（1916/3 不是整数，2x 为 1080×958 点）后，**内屏显示 iPadOS 主屏**：Dock、应用图标、天气和日历小组件、时间。SpringBoard（PID 7）连续运行 7 分钟不再崩溃，只有 SBRendererService 因没有 GPU 退出 |

## 2. 拍照回传

- 页面：Mac 局域网上的 8765 端口（`tools/photo_drop/server.py`，在 Mac 上用 nohup 常驻，照片存到 Mac 上的 photos/ 目录）。页面顶部的指示来自同目录的 `ask.txt`。
- 这台 Linux 到 Mac 的链路只有约 22 KB/s（中美之间），所以**不传原图**：
  - 在 Mac 上用 macOS Vision 做 OCR（`tools/photo_drop/ocr.swift`，编译后为 `tools/photo_drop/ocr`），一整屏 1.6 秒，只把文字传回来；
  - 需要看图时，用 `tools/photo_drop/pull.sh` 先在 Mac 上缩小、压缩再传。

## 3. 根文件系统：放在安卓 /data 的 pinned 文件里

用户选择保留安卓。pong 的原方案是把 SHNGROOT 目录写在 userdata 第 0 块，窗口从 1 MiB 开始，相当于清空 userdata，所以不能照搬。

做法：

1. 在 `/data/hackios/root.img` 建一个 f2fs pinned 文件（`F2FS_IOC_SET_PIN_FILE` + `fallocate`）。pinned 文件不会被 GC 搬动，`fallocate` 给它分配真实的物理块。工具是 `tools/phone/pinalloc.c`：无 libc 的 aarch64 静态程序，用 Mac 上的 Homebrew clang 编译。
2. FIEMAP 查出物理位置。/data 是 dm-default-key，对 sda36 是 1:1 映射、偏移 0，所以 f2fs 的物理地址就是 userdata 分区内的字节偏移。
3. f2fs 会先把前面的空洞填满：直接分配 1 GiB 会被切成 7 段。先建一个 40 GiB 的占位 pinned 文件（`/data/hackios-test/spacer.pin`），之后分配的文件就是连续一整段：19.4 GiB，起点在 userdata 内 59,198,406,656 字节处，即 4K 块 14452736。
4. `tools/mac/install_pinned.py` 绕过 f2fs 和 dm-default-key，直接写 `/dev/block/by-name/userdata`：
   - 每块 64 MiB，用 gzip 压缩后经 adb 传输，写完回读核对 SHA-256；
   - 写入范围限定在 pinned 文件以内；
   - 三个窗口都写完后，最后才写入 SHNGROOT 目录。
   - 安卓读这个文件只会得到乱码（解密的是明文），这没有关系；安卓本身和其他数据不受影响。
5. 驱动改动（`device/NativeUfs/`，已同步到 Mac 的 UniXNU-zizhan）：
   - 启动参数 `pong_shngroot_4k=<块号>` 指定目录表位置，默认 0，与 pong 原行为一致；
   - 每个窗口必须落在目录表之后、`imageBytes`（= 文件末尾）以内；
   - `kPongUserdataFirst4k/Last4k` 换成 zizhan 的 `0x2dfa00/0x76753fa`。已用备份的 LUN0 GPT 核对：驱动的 GPT 检查（CRC、表项、备份位置）全部能通过。

## 4. 免按键闭环

- 内核：`device/ArmDTPlatform/PongBootReset.h` 改编自 sheng 的 `BootReset.c`，在 `ArmDTPlatform::configure()` 一开始调用。
  - 带启动参数 `pong_autoreset=<秒>` 时，经 SPMI 把 PMK8350 SDAM `0x48` 写成 bootloader（2）并回读校验，然后打开 Gunyah 看门狗（60 s），由内核线程喂狗到期限为止。
  - 之后无论 panic、卡死还是到期，都会复位进 fastboot，内存里的日志仍在。
  - zizhan 的 DT 有 `qcom,gh-watchdog` 节点，SPMI 地址与 sheng 相同。
- 实测（u3）：`spmi v70000000 apids=768 apid=412 owner=0 cell 00 -> 04 rc=0`，`watchdog enable=0`，到期加 60 s 后复位进 fastboot，内存里的日志仍在（个别字符有位翻转，不影响阅读）。
- UEFI：`zizhanPkg/Applications/RamoopsDump` 按 GPT 分区名找到 `logfs`（原厂 UEFI 每次开机写 UefiLog 的 8 MiB FAT 卷，用户已同意写入），把 `0x85200000/0x85400000` 的日志写成 `XNULOG.TXT`，然后冷重启进安卓。镜像为 `out/ramoops-dump.img`。
- 主机：`tools/mac/cycle.sh IMG RUN` 依次做四步：启动测试镜像，等它自动回到 fastboot，启动 RamoopsDump，进安卓后读出 logfs，日志存到 Mac 的 `logs/RUN.txt`。

证据：`docs/evidence/2026-10-30-u3-ipados-homescreen.jpg`（用户拍照）。日志在 工作区 `logs/u2.txt`、`u3.txt`。

### 调试手段：从安卓读回 iPadOS 的 Data 卷

回到安卓后，用 `adb exec-out "su -c 'dd if=/dev/block/by-name/userdata bs=1048576 skip=<(base+Data 偏移)/1MiB> count=8192'"` 把 8 GiB 的 Data 窗口读到 Mac（约 12 MB/s），再用 `sudo hdiutil attach -readonly -imagekey diskimage-class=CRawDiskImage` 挂载。崩溃报告在 `mobile/Library/Logs/CrashReporter/*.ips`（JSON 格式）。

### 下一步

1. ST FTS 触摸驱动（内屏：SE4 SPI，IRQ GPIO21，RST GPIO20，供电 L7C/L2C）。没有触摸，界面无法操作。
2. 画面方向和比例：目前主屏在内屏上是横向布局，需要确认 iPadOS 的方向与面板物理方向是否一致。
3. GPU（Adreno 730，复用 PongAdreno 和本机固件）。
4. 外屏、背光、电池、Wi-Fi 以后再做。

## 5. 换到 Mac Studio 后的坑

- Xcode 在 `/Applications/Xcode.app`（26.6），不在 `/Volumes/Extend`。`ci-env.sh` 和 `local.toml` 已修正。
- 构建树里 `.tools/python3` 的符号链接指向旧的 Python 路径，需要删掉让脚本重建。
- 嵌套引号：`adb shell su -c "a; b"` 经 ssh 转发后，`b` 会以 shell 身份执行。要么推送脚本执行，要么在 Mac 上用 `adb shell -T "su -c '…'"`。

## 6. 内屏触摸（ST FST2）✅

| 轮次 | 结果 |
|---|---|
| t1 | 驱动退出：`clocks stay off: 0 0x80000002 0x4220`。QUP0 S-AHB 时钟带硬件门控（CBCR bit 1），空闲时状态显示关闭。改为对带 HW_CTL 的时钟不检查关闭位 |
| t2 | `GENI unavailable: the serial engine's FIFO interface is disabled`。zizhan 的 SE4 固件以 GSI（只走 DMA）方式加载，FIFO 接口被关掉，pong 的 FIFO 收发不能用 |
| t3 | 改用 GPIO 模拟 SPI（模式 0，同样是 GPIO16 MISO、17 MOSI、18 CLK、19 CS）。读到芯片 ID 0x3652、固件 0xe455、分辨率 21590×19130、35×40 节点，触摸事件正常上报。用户点左上角、右下角得到原始坐标 18345,485 和 675,20565：x 正确，y 上下颠倒 |
| t4 | 默认 `touch_flip=2`（y 镜像），**用触摸打开「设置」**（`docs/evidence/2026-10-30-t4-settings-by-touch.jpg`） |

实现：
- `device/pong_fts.c` 由 Mac 上的 `tools/mac/mk0002.sh` 生成 `patches/xnu-ipados18/0002-zizhan-fts.patch`。这个补丁只新增 `bsd/dev/pong_fts.c`、`bsd/conf/files` 末尾一行，以及在 `bsd_init()` 结尾调用 `pong_fts_init()`，因此 0001 的「已应用」检查仍能通过。
- 板级部分（RPMh、GCC、TLMM、GENI、GPIO SPI）取自 pong 的 `pong_goodix.c`。芯片层按 zizhan 的 fst2 6.0.3 驱动编写：
  - 读写帧：`FA/FB` 硬件寄存器（32 位地址），`B2/B1` 固件寄存器（16 位地址），`B7` HDM；每次读先返回 1 个 dummy 字节。
  - 初始化：`configure_spi4`（`FA 20000034=02`、`2000003E=07`、`2000002D=02`）→ 读芯片 ID → GPIO 复位 → 等 `03` 就绪事件 → 系统信息 → `B2 00 10 01` 开扫描。
  - 事件：每次从 FIFO（`B1 00 60`）读一个 8 字节事件，直到读到 00。
  - 不刷固件。
- ADT 的 `pong-touch` 节点改为 `st,fts-zizhan-pri`、max 19140×21600。pong 的 Goodix 驱动会跳过这个节点。
- 启动参数 `pong_touch=1`；`touch_flip` 的 bit0 镜像 x，bit1 镜像 y，bit2 不交换轴，默认值为 2。

已知问题：用户反映「有点卡」。可能的原因有两个：没有 GPU，全靠 CPU 软件渲染；以及 GPIO 模拟 SPI 较慢。下一步再定位。

## 7. GPU（Adreno 730）

pong 的 GPU 路径里写死的硬件常量，已和 zizhan 实机 DT 逐项核对，**全部相同**：

- 寄存器窗口：GPU、GMU、RSCC、GPUCC、SMMU、GDSC；
- RPMh 的 DRV2/TCS；
- SMMU 流 ID 0/1/2/5 和 ACTLR；
- 中断 332/336/337；
- zap 保留区 `0x8b91a000`、PAS 13；
- 芯片 ID `0x07030002`；
- 13 档频率、ACD 和 DDR 表。

固件使用本机的 `gmu_gen70000.bin`、`a730_sqe.fw`、`a730_zap.mdt/.b01/.b02`，放在 Mac `work/firmware/adreno730`，构建时由 `ci-env.sh` 的 `PONG_ADRENO_FIRMWARE` 指过去。zap 由手机自己的 TrustZone 验签，必须用本机这份。

| 轮次 | 结果 |
|---|---|
| g1 | 新 defconfig `zizhan_ipados18_gpu_defconfig`：CI 配置基础上只打开 `CONFIG_XNU_PONG_ADRENO_PLATFORM`；打包时用 `PROFILE=zizhan_ipados18_gpu ci_boot_ufs.sh`。先关掉 Metal：`pong_metal=0 pong_gpu_scanout=0 pong_ui_software=1 pong_software_ui=1`。**GPU 启动的每一步都 `result=0`**：ARC 投票、CX 时钟、GPU PLL、SMMU、GMU 固件/HFI、上电握手、GBIF、CP_ME_INIT、**PAS13 zap 认证**、CP fence、IRQ332、CP/user-client 启动。SpringBoard 和触摸不受影响 |

下一步：用户态 Turnip、Metal 桥接层和插件（由后台任务在 Mac `work/gpu/` 构建）→ `sudo build-ipados18.py refresh_metal` 写入 OS 镜像 → 重新安装 OS 窗口 → 打开 `pong_metal=1 pong_gpu_scanout=1`。
| 用户态 | 后台任务在 Mac `work/gpu/` 构建（不需要 root）。补齐 rustup（stable 1.99.1 + `aarch64-apple-ios`）、brew bison 3.8.2/glslang、venv 中的 meson/mako/pyyaml/packaging/ninja。Mesa 用 `85fda3b3`（freedesktop gitlab）加 9 个 UniXNU 补丁；metal2vulkan 用 `720ef1ce` 加 bridge 补丁 0001-0007。pong 的 mesa 0003 中有一段已包含在 pin 补丁里，手工去掉了这一段（见 `work/gpu/logs/0003-hunk2-already-present.rej`）。产物：`work/gpu/plugin/`（PongAdrenoMetal.bundle：Turnip 18 MB + Metal 桥接 33.5 MB） |
| 写入 OS | `cp -c` 备份为 `pong-ipados18-system.pre-metal.cdr`，执行 `sudo build-ipados18.py refresh_metal --out …/prepared --runtime work/gpu/plugin`，再用 `prepare-ipados18-ufs-layout.py` 重新生成 `ufs-metal/ufs-layout.json`（只有 OS 的哈希变化），`install_pinned.py --only OS` 只重写 OS 窗口和目录表 |
| g2（完整 GPU 参数） | panic `Failed to mount root device`：APFS 容器已挂上，但 System 卷的 IOMedia 一直没有发布。g3（新 OS 镜像 + g1 参数）根卷和 SpringBoard 都正常，说明问题出在 `pong_metal=1`：IOAcceleratorES 提供者在挂根之前发布，会卡住卷的发布 |
| **g4** | `drivers/PongAdreno/PongAdrenoNativeGlue.cpp`：改为由内核线程等 `rootvnode` 出现后再发布 Metal 提供者。根卷正常挂载，`PONG METAL: … publication status=0x0 (after root mount)`；`Pong Metal: real GPU scanout worker ready`、`actual GPU scanout copy completed`；backboardd 的 Metal 计数为 942 个命令缓冲、6479 次绘制、477 帧，**界面合成走 GPU**。SpringBoard[7]、backboardd[41] 全程没有重启，没有 GPU 故障。画面效果还需要用户拍照确认 |

GPU 启动镜像：`PROFILE=zizhan_ipados18_gpu ci_boot_ufs.sh NAME 14452736 pong_autoreset=… pong_touch=1`，使用 pong 默认的 GPU 参数：`pong_metal=1 pong_gpu_scanout=1 pong_ui_software=0 pong_software_ui=0 pong_gpu_pool_mib=256 pong_metal_global=1`。

## 8. 画面验收与显示性能（2026-10-30 上午）

| 轮次 | 结果 |
|---|---|
| g5 | 同一个 g4 镜像再启动，又出现 `Failed to mount root device`。g4 的成功是时序碰巧 |
| g6 | `PongAdrenoPlatform.cpp` 的 GPU 启动线程改为等 `rootvnode` 后再等 2 s 才开始。正常进系统。**用户照片确认 GPU 渲染正确**（主屏、小组件毛玻璃、Dock 半透明、设置页都对），没有花屏，点击正常；动画仍卡，比 CPU 版略好 |
| g7 | 内核侧计时（`PONG SCANOUT TIMING`）：prepare/映射/释放合计 < 0.1 ms，每帧 18–75 ms 都在用户态呈现器（导入 + blit + 等待）。`MiniIOMobileFramebuffer` 约 10 帧/秒；upstream 文档也记录了约 10 fps |
| g8 | 每个合成表面的映射和 GPU 导入都缓存（内核 `kept[4]` + 令牌/淘汰协议，用户态 `GpuScanoutPresenter` 按令牌缓存 MTLBuffer；`pong_scanout_cache=0` 可关）。导入降到 4 µs，64/64 帧复用；剩下 encode 9–25 ms、GPU 等待 9–14 ms |
| g9 | 关掉缓存做对照，结果没进系统：又是根卷挂载超时（60 s），而且这次 GPU 还没启动。所以根卷问题与 GPU 无关，g6 的改动只是让概率变小 |

**根卷间歇性挂载失败**：失败的轮次里 OS、SystemOS、Data 三个容器并发探测，OS 容器完成 checkpoint search 后不再推进，60 s 后 `IOFindBSDRoot` panic。UFS 驱动是同步轮询、单锁串行，没有报 I/O 错误。g10–g13 加了 `PONG_UFS IO` 每 5 s 统计（每个窗口的请求数、KiB、当前请求已等多久、最长请求），用来区分是传输卡住还是上层（APFS/IOKit）卡住。

**卡顿的主因**：GMU 启动时投的唯一一票是 DT 初始档 index 2，也就是 **GPU 285 MHz、DDR 约 1.76 GB/s**（最高 900 MHz、13 GB/s），而且没有 GPU DCVS，不会再升。一帧 16 MiB 的拷贝要读写 32 MiB，这个带宽下约 19 ms，和测到的 GPU 等待一致。改动：`pong_adreno_start_level`（boot-arg `pong_gpu_level`，默认 9，即 710 MHz、DDR 档 10，约 12.5 GB/s）。g14/g15 验证。

| 轮次 | 结果 |
|---|---|
| g10–g13 | 加了 `PONG_UFS IO` 统计，4 轮全部正常进系统。正常时 OS、SystemOS、Data 在 10 s 内都挂好；之后约 40 s 里 SystemOS 顺序读了约 3.8 GB（应是 dyld 共享缓存，约 100 MB/s），这不影响挂根 |
| g14 | `pong_gpu_level=9` 生效（`GMU start level a=0x9`）。这一轮又碰到挂根失败，统计给出了答案：三个窗口各发了几十个请求（OS 85、SystemOS 73、Data 95）后同时停住，`busy=0`，之后 60 s 里没有任何新请求。**传输层是空闲的，卡在上层**（APFS/IOKit） |
| g15 | 正常。710 MHz 下 GPU 等待只从 9–14 ms 降到 8–11 ms，encode 仍是 14–22 ms：带宽不是主要瓶颈，主要开销在 metal2vulkan 的 commit（每次提交都做驻留同步、live buffer 列表、预处理），合成器本身也被它拖慢 |
| g16 | 怀疑原因：`doAsyncReadWrite` 在调用者自己的线程里、返回之前就调用完成回调，真实硬件驱动不会这样。改成 4 个传输工作线程处理请求并回调（`ufsQueue`/`ufsWorker`，`pong_ufs_async=0` 可关）。**这一轮手机没有自动回到 fastboot**，Mac 上 adb 和 fastboot 都看不到设备，后面 5 轮已停。等用户按电源+音量下进 FASTBOOT 后读 ramoops 再查 |

当前状态（13:05）：
- Mac `UniXNU-zizhan` 里改过但未验证的：`drivers/NativeUfs/PongUfsTransport.cpp`（I/O 统计 + 异步工作线程；改动前备份 `/tmp/PongUfsTransport.cpp.pre-iostat`、`.pre-async`）。
- 已验证可用：GPU 启动等根卷（g6）、扫描输出映射/导入缓存（g8，`pong_scanout_cache`）、GPU 启动档位 boot-arg（g14，`pong_gpu_level`，默认 9）。
- 手机上 OS 窗口装的是带缓存呈现器的插件（`install-g8.json`）；g6 版 OS 镜像备份为 `prepared/pong-ipados18-system.g6.cdr`，旧插件为 `work/gpu/plugin.g6`。
- 显示性能下一步：在 metal2vulkan commit 里加计时，找出单次 blit 10–20 ms CPU 开销在哪；或者让扫描输出与下一帧渲染流水线化。

## 9. g16 panic、提速、挂根失败的根因（2026-10-30 下午）

### g16 的 panic：qsort 把启动栈用爆

用户拍的照片里 `Invalid kernel stack pointer`，sp 落在 `intstack_low_guard`。照片上 0/6/8 难分，逐个候选反汇编，只有 pc=`qsort+0x20`（`stp x2,x3,[sp,#0x80]`，正好 far=sp+0x80）、lr=qsort 自递归返回点能对上，x3=`kalloc_type_cmp_fixed`。XNU `bsd/kern/qsort.c` 总是对左半边递归，每层 0x600 字节，启动时 kalloc_type 排序跑在 16 KB 中断栈上；g16 的 `IOMallocType(UfsRequest)` 多了两个 kalloc_type 视图就越界了。panic 发生在 watchdog 武装之前，所以手机停在 panic 画面。
修复 `patches/xnu-ipados18/0003-qsort-bounded-recursion.patch`：对较小的一半递归、较大的一半循环。g22 起不再出现。

### 每轮提速

| 改动 | 位置 |
|---|---|
| UEFI 在交给内核前写 SDAM "bootloader" 并武装 Gunyah watchdog（60 s），早期 panic/卡死也会回 fastboot | `drivers/DarwinBoot24/.../PongAutoReset.h`（复用 `ArmDTPlatform/BootResetCore.h`，加 `BR_NO_STDINT`）；日志行 `DARWINBOOT24: pong_autoreset: reboot-reason cell` |
| panic 日志写完后 2 s（`pong_panic_hold`）复位，不再停在画面上 | `0004-pong-panic-reset-hook.patch`（`pong_panic_reset_hook`），`PongBootReset.h::panicReset` |
| 截止时间到或 SpringBoard 起来 N 秒后（`pong_autoreset_sb=N`）立即复位 | `PongBootReset.h::pinger`，每秒 ping 一次并查进程名 |
| 复位方式：把 watchdog 改成 1 s 后咬 | g23 用 PSCI SYSTEM_RESET 也能进 fastboot，但 ramoops 控制台读不出、DIAG 区大量位翻转（不是热复位），所以改回 watchdog |
| Mac 端 `round.sh RUN args…`：编内核、打包、跑一轮，各阶段计时 | `tools/mac/round.sh` |

效果：正常一轮从约 5 分钟降到约 2 分钟（g26：开机后 97 s 回到 fastboot），挂根失败一轮约 2.5 分钟（124 s 回 fastboot），都不需要人按键。

### 挂根失败的根因

| 轮次 | 结果 |
|---|---|
| g22 | g16 + qsort 修复：异步 UFS 完成回调没有用，仍是 OS 85、SystemOS 73、Data 94 个请求后停住 |
| g24–g27 | SystemOS/Data 推迟到根卷挂好后才发布（`pong_ufs_defer`）：失败时 SystemOS/Data 请求数为 0，OS 仍停在 85。**与并发无关**。4 轮 3 失败 |
| g28 | 加 `0005-pong-dump-waiting-threads.patch`：根卷 25 s/45 s 还没挂上就打印所有等待线程的等待事件和栈（`tools/mac/symwait.py` 符号化，kext 地址用 `prelink.json` 的 extents 定位，`tools/mac/kdis.py` 反汇编并解析字符串）。第一轮就抓到 |

g28 的栈：
- IOKit 配置线程在 **APFS** 某服务的 `start()` 里 `waitForMatchingService(resourceMatching("IOBSD"), 永不超时)`，同时持有 APFS 的一个锁；
- 另一个配置线程在 APFS 的另一个 `start()` 里等这把锁（OS 容器的探测，所以停在第 85 个请求）；
- **AppleImage4** 的 `start()` 在 `waitForMatchingService(serviceMatching("IOAESAccelerator"), 永不超时)`。Pong 没有 IOAESAccelerator（`XNU_NATIVE_AES` 关），所以每次启动它都永远卡着；拿到后它只是 release，超时返回空则 panic "failed to match crypto accelerator"。

因果链（15:40 更正：最初以为是 AppleImage4 堵住了 IOResources；查 kernelcache `__PRELINK_INFO` 后发现 AppleImage4 的 arm64 personality 是 `IOPlatformDevice` + `IONameMatch=defaults`，它卡住的是 `/defaults` 节点，与挂根无关）：
- APFS 主服务 `com_apple_filesystems_apfs` 的 personality 是 `IOResources` + `IOResourceMatch=IOBSD`，它在 IOResources 的配置里启动；
- OS 盘的 `AppleAPFSContainerScheme::start()`（provider 是 IOMedia）若在 IOBSD 发布之前运行，就拿着 APFS 的锁去 `waitForMatchingService(resourceMatching("IOBSD"))`（g28 tid 204）；
- IOBSD 发布后 IOResources 开始配置，启动 APFS 主服务，它要这把锁（tid 132）；
- tid 204 要等 IOResources 的 matched 通知，而通知在 IOResources 配置结束（即 tid 132 的 `start()` 返回）后才发：**死锁环**。
- 容器探测若在 IOBSD 发布之后才跑，`copyExistingServices` 直接返回，所以是概率性的。0006 第一版的 panic 也说明 IOBSD 发布时 IOResources 每次都在配置中。

修复 `0006-pong-resource-notify-during-config.patch`：`publishResource` 发现 IOResources 仍在配置中时，立即投递 matched 通知（`copyNotifiers` + `invokeNotifiers`，正常情况下重新匹配本来就会再投递一次）。

| 轮次 | 结果 |
|---|---|
| g32–g35 | 第一版用了 `deliverNotification()`，这个版本里它是 `panic("deliverNotification")` 的空壳，4 轮都在 `bsd_autoconf` 时 panic。反过来证明 IOBSD 发布时 IOResources **每次**都还在配置中。顺带验证了 panic 快速复位：开机到回 fastboot 33 s |
| g36–g39 | 改用 `copyNotifiers` + `invokeNotifiers`：**4/4 正常进系统**（修复前 g24–g28 是 5 轮 4 失败），每轮开机到回 fastboot 96–97 s，日志 0.73–0.78 MB |

当前状态（15:15）：
- 镜像 `out/boot-g36`（Mac）：0003 qsort、0004 panic 复位、0005 等待线程转储（仅挂根卡住时触发）、0006 IOResources 通知；DarwinBoot24 提前武装 watchdog；`pong_ufs_async=0`，`pong_ufs_defer` 默认开（SystemOS/Data 挂根后发布，未发现副作用，可保留也可关）。
- 一轮：`round.sh RUN pong_autoreset=180 pong_autoreset_sb=20 pong_ufs_async=0`，约 2 分 20 秒。
- 遗留：AppleImage4 在 `/defaults` 上永远等 IOAESAccelerator。其他项目（picasso、sagit）记录过它会导致 watchdogd `busy timeout 'defaults'` 和 launchd 卡在 img4 ignition；我们能进 SpringBoard，暂未见影响。根治办法是提供软件 IOAESAccelerator（UniXNU 的 `native-aes-personality.patch`、`UniXNU/SoftwareAES`、picasso 的 NativeAES），我们的树里有这个补丁但 `XNU_NATIVE_AES=0`。

## 10. 显示性能：每次提交的缓存维护（2026-10-30 傍晚）

调研（见对话；主要来源 UniXNU/Adreno740、UniXNU-sheng、ShueiTsuki/data-a-live、steelbrain/metal2vulkan）后逐条核对 pong 已有的上游优化。`device/ref/` 下克隆了 Adreno740 和 UniXNU-sheng 做对照（`git apply --check` 正/反向判断是否已合入）。

- 已有：128-IB 整帧提交（pong 自己的 mesa 0003）、metal2vulkan 的 data-a-live 补丁 0012–0019、`IOMFBDisplayRefresh`（MiniIOMobileFramebuffer 已发布）、b686b90 的代码。
- **b686b90 实际没生效**：它跳过导入内存每次提交的 clean/invalidate，前提是 `native_adreno_io_coherent=1`。sheng 在开机时用 CPU/GPU 双向自检（`package/GpuPowerProbe.cpp::coherenceProbe`）证明 IO 一致后才置 1；pong 没有移植这个自检，变量一直是 0。
- 缺 5c8c145 的两部分：每次提交都做一致性采样（两个临时非缓存映射）、每 8 次提交做约 2 ms 的 IB 审计。TLBIALL 那部分在 pong 里本来就只是屏障，未改。

| 轮次 | 改动 | 每帧扫描输出（内核 `user`）/ CPU encode / GPU wait |
|---|---|---|
| g40 | 基线（一致性关） | 24–35 ms / 10–22 ms / 8–16 ms |
| g41 | `pong_gpu_coherent=1` 强制打开 | 4.9–6.3 ms / 3.3–4.5 ms / 1.5 ms，skips 计数持续增长 |
| g43 | 移植 sheng 自检（`PongAdrenoNativeGlue.cpp::coherenceProbe`，`pong_gpu_coherent` 2=自检默认/1=强制/0=关）+ 5c8c145 的采样/审计减量 | 自检通过 `d64/64 r1024/0 w1024/1024 k6`；4.4–6.0 ms / 2.3–4.2 ms |
| g42 | 再加小核占位（`ArmDTPlatform/PongCpuPark.h`，`pong_park_cpus` 默认 0xf：cpu0–3 的 A510 各一个优先级 1 的绑定空转线程，内核把 8 核当成同一簇，否则先派活给小核；参考 sheng 19960fa） | **2.9–3.4 ms / 1.3–1.8 ms / 1.4 ms** |

结论：SM8475 的 GPU 是 IO 一致的；扫描输出这一步从 24–35 ms 降到约 3 ms（8–10 倍）。

### 真实帧率与瓶颈

只看扫描输出不够：主屏静止时合成器很少出帧（MiniIOMobileFramebuffer 的统计里大部分是 >90 ms 的"停顿"），g44/g45 的 10 fps 只是静止画面的更新频率。为此：
- `pong_touch_synth=N`（`device/pong_fts.c`，patch 0002）：SpringBoard 起来 20 s 后每 N 秒左右滑一次，主屏翻页，作为可重复的动画负载；
- `PONG SCANOUT TIMING` 增加 64 帧窗口时长、帧间隔直方图（<17/<25/<34/<50/<90 ms/停顿）和只算"在动的帧"的 fps，也写进 DIAG 区（控制台环形缓冲会被 applog 冲掉）。

| 轮次（每 2 s 一次滑动） | 在动的帧 | 间隔分布 |
|---|---|---|
| g49 一致性关、不占位 | 30–32 fps | 主要 25–50 ms |
| g48 一致性开 + 占位，GPU 档 9（710 MHz） | 31–32 fps | 主要 25–34 ms |
| g51 GPU 档 4 | 31–37 fps | 主要 25–50 ms |
| **g50 GPU 档 13（900 MHz）** | **49–73 fps** | **主要 <17 ms** |

结论：扫描输出变快后，瓶颈是合成器渲染一帧的 GPU 时间；710→900 MHz（DDR 档几乎一样，12.5 vs 13.1 GB/s）帧率翻倍，说明帧时间刚好跨过节拍阈值。默认 `pong_adreno_start_level` 改为 13（`PongAdrenoPower.c`），代价是没有 DCVS 时一直最高频，耗电发热。metal2vulkan 上游的 ca8340b/d485925 与我们锁定的 720ef1c 冲突约 30 个文件，暂不升级。

待办：
- **画面正确性需要用户看一眼**（自检通过，理论上不会有残影）：晚上跑一轮停在主屏的镜像，拍照/划几下。
- 小核占位会让 4 个小核一直空转，耗电发热；以后可以换成真正的 AMP 调度（ADT 里的 cluster 拓扑）。
- 真实帧率：g44 起 `PONG SCANOUT TIMING` 带 64 帧窗口的墙钟时间和 fps（只在界面持续动画时有意义）。
- NativeAES（`XNU_NATIVE_AES=1`）需要 `XNU_SOURCE_CORECRYPTO14=1`，改动整个 crypto 启动路径，暂缓。

### 发热与日志位翻转（17:45）

g54–g67 的帧率统计几乎都读不出来。原因有两层：
1. 用户态 logd 起来后 IOLog 不再进 ramoops 控制台，只有 `pong_ramoops_diagnostic_write` 写的 DIAG 区能留下；
2. 位翻转越来越多，DIAG 头的签名被翻一位后 RamoopsDump 整段丢弃（`RamoopsDump.c` 按 `sig` 校验）。

按"完整的 launchd 行占比"统计：g15 22%，g26–g39（连续跑）2–3%，**g40（歇了约 50 分钟后）84%**，g41 69%，g42 47%，g44 20%，g46 13%，g48 2.5%，g58 以后约 0%。翻转双向（0→1、1→0），与连续运行时间强相关：是发热导致复位期间 DRAM 保持变差。g66 试了 PSCI SYSTEM_RESET2（热复位，`pong_reset_warm`，默认开，固件不支持则退回看门狗），在已经发热的状态下看不出改善。g67 结束回安卓约 1 分钟时电池 40.8°C、CPU 46–49°C。

本移植**没有任何温控**。处理：
- 小核占位默认关（`pong_park_cpus=0`），GPU 档默认回到 9（710 MHz），900 MHz 的收益只有 g50 一轮，需要在不热的状态下复测；
- `cycle.sh` 开机前在安卓里等电池降到 35.0°C 以下（`COOL`，最多 20 分钟），结束后打印回到安卓时的电池温度；
- `PONG FPS SUMMARY`：整轮累计的帧间隔统计，每 64 帧一份、结束前 8 份，Mac 上 `tools/mac/fpsvote.py` 逐字符投票还原。
- g68–g71：降温后交替跑 GPU 档 9/13/9/13（`tools/mac/ab-gpu-level.sh`）。

### GPU 调频（DCVS，18:00–18:50）

调研（源码一手）：Linux 上游 msm 用 devfreq `simple_ondemand`（50 ms，upthreshold 50 / downdifferential 10）+ boost + 空闲降频；本机原厂 KGSL 用 `msm-adreno-tz`，算法在 TrustZone（`qcom_scm_dcvs_update`），落档同样是 HFI `GX_BW_PERF_VOTE`（id 30）；UniXNU-sheng（`package/GpuPowerProbe.cpp::gpuDcvsThread`）在 simple_ondemand 基础上加了触摸/唤醒升频；data-a-live 0197 照搬 simple_ondemand。XNU 没有 devfreq，这些移植都自己写调度线程，但都照着 Linux 移植。

实现（`device/PongAdreno/`）：
- `PongAdrenoGmu.c`：开忙碌计数器（busy mask `0xff000000`、countable 5、enable，与 Linux/KGSL gen7_start 相同）、64 位读计数、`PongAdrenoGmuSetLevel`（HFI id 30，阻塞等 ack）。
- `PongAdrenoPlatform.cpp::dcvs`：**移植 sheng 的调度器**，档位表换成 A730 的 13 档；16 ms 窗口、50% 目标 ×1.1、>85% 或连续忙 50 ms 到顶、连续 4（升档后 5）个窗口才降一档、空闲 80 ms 回底；唤醒升频（≥578 MHz，50 ms）、触摸升频（`pong_touch_activity`，`device/pong_fts.c`，1 s）。计数器第一次动之前保持起始档。boot-arg：`pong_gpu_dcvs`（默认 1）、`pong_gpu_dcvs_min`（默认 3 = 324 MHz）、`pong_gpu_dcvs_boost_ms`、`pong_gpu_max_level`（默认 13，也可作发热上限）。
- 中间我自己写的版本（g72–g77）有两个 bug：开机 GPU 空闲时误判计数器坏而放弃；降档计数被同一轮清零，只升不降。已被 sheng 版本替换。

| 轮次（降温到 35°C 后，每 2 s 滑动） | 滑动帧率 | 结束时电池 |
|---|---|---|
| g78 固定 710 MHz | 29.1 fps | 35.8°C |
| g80 固定 710 MHz | 33.4 fps | 36.1°C |
| g79 sheng 调度器 | 34.3 fps | 36.1°C |
| g81 sheng 调度器 | 35.2 fps | 36.0°C |

调度器每 10 s 换档一两百次，0 次失败；滑动负载下 57–71% 时间在 900 MHz（触摸升频）。**GPU 平均只忙 3–10%，不是瓶颈**；30–35 fps 的限制在 CPU 端（backboardd / CoreAnimation / metal2vulkan 提交 / 呈现器）。下一步先调研类似项目怎么定位和解决 CPU 端瓶颈、怎么快速迭代用户态，再定方案。

### 方案 A1：帧间隔按刷新节拍分析（19:30–19:45）

按 `docs/plan/2026-10-30-cpu-side.md`。`PONG FPS SUMMARY` 增加按节拍（`fb_hz`，默认 120 Hz = 8.33 ms）的分档和"偏离整拍 >2 ms"计数（参考 annibale `docs/analysis/2026-10-29-frame-budget.md` §6，他们 97% 落在整拍上，结论是客户端多等整拍）。

| 轮次 | 刷新 | 在动的帧 | 偏离整拍 | 均匀分布时的预期 |
|---|---|---|---|---|
| g82 | 120 Hz | 35.8 fps | 62% | 约 52% |
| g83 | 120 Hz | 约 25 fps（投票不全） | 约 56% | 约 52% |
| g84 | 60 Hz | 33.0 fps | 77% | 约 76% |

帧间隔连续分布、没有节拍量化，且 60/120 Hz 帧率不变：**是渲染/提交本身慢，不是等节拍**。B2（SwapWait/交换节奏）降级；先做 B1（大核）和 A2（采样）。`tools/mac/tlsplit.py` 已写好（解析 `touch_lat=1` 的 `tl:` 时间线，按 annibale 的 B/C/D 分段），出现节拍量化时再用。

### 方案 B1 第一步：小核占位单独对比（19:45–20:00）

| 轮次（降温后，每 2 s 滑动，sheng 调度器） | 在动的帧 |
|---|---|
| g85 不占位 | 33.5 fps |
| g86 只占 cpu0（sheng 19960fa 的做法） | 日志损坏，只有开头一份（不可用） |
| g87 占 cpu0–3 | 32.2 fps |

四个小核全占住、界面线程只能在大核上跑，帧率不变：**瓶颈不是线程落在慢核上**。结合 A1（帧间隔连续、不按节拍量化）和 GPU 只忙 3–10%：一帧的时间主要花在流水线里的等待上。B1 第二步（移植 sheng `zzyf-sheng-amp.patch`）暂缓：还需要给 ADT 补 `cluster-type`/`cluster-id`（我们生成的 ADT 里 0 处，sm8475.dtsi 里有 `unixnu,cluster-type`，UniXNU `scripts/unixnu/devicetree.py` 有转换），以及 PONG.h 的 `__ARM_AMP__`/`MAX_CPU_CLUSTERS 2`（现在 `NO_ECORE 1`）。

### 方案 A2：backboardd 线程状态采样（20:05 起）

`patches/xnu-ipados18/0007-pong-thread-sampler.patch`（osfmk/kern/thread.c，接在 0005 后）：内核线程每 1 ms 遍历目标进程（`pong_tsample_proc`，默认 backboardd）的所有线程，分类计数：运行（哪个核）、排队、等待（block hint + thread_invoke/thread_block_reason 之后的 4 个内核返回地址，无栈等待记 continuation），512 项哈希表聚合；结束时按样本数排前 48 项，打印两份（控制台 + DIAG），线程名里的空格换成 `_`。`PongBootReset.h`：`pong_tsample=<秒>` 时在 SpringBoard 起来 20 s 后开始（与合成滑动同步）。Mac 端 `tools/mac/tsample.py LOG KERNEL`：两份投票、atos 符号化、按线程汇总"跑在小核/大核/排队/等待"占比。g88/g89 两轮验证。

20:28：g88 的 `adb reboot bootloader` 之后手机没有出现在 fastboot，之后 USB 上既无 adb 也无 fastboot（Mac `ioreg` 只有两个 hub）。g88/g89 未运行。最可能是用户晚上拿起手机拔了线；已暂停自动测试，等用户确认手机状态。A2 的 0007 补丁与 `tools/mac/tsample.py` 已就绪（编译通过，模拟数据测过），手机接回后用 `tools/mac/a2-tsample.sh` 跑。

21:00–21:16：用户换了 Mac 的 USB 口后 fastboot 恢复识别（之前两次 `adb reboot bootloader` 后 fastboot 不枚举，换口解决）。g90（观看轮 + 采样）在 SpringBoard 起来约 20 s、采样器启动时 panic：`Lock not owned … @locks_arm.c:408`（`lck_spin_assert`）。原因：0007 调了 `thread_get_runq()`，它断言持有线程锁。改为直接读 `thread->__runq.runq`（`os_atomic_load relaxed`），已重编译。g91 观看轮（不采样，25 分钟）给用户看画面。

**用户实机反馈（g91，21:20 左右）**：画面正常（IO 一致性打开后无残影/花屏，与自检结论一致）；连续左右滑动"不太跟手，还是有点卡，但是比以前好多了"；不烫（sheng 调度器 + 小核占位关）。与测量一致：扫描输出 ~3 ms，合成器一帧仍 25–35 ms（30–35 fps）。下一步 A2 采样定位那 25–35 ms。
g91（用户实际操作约 5 分钟，Power+VolDown 结束）：在动的帧 26.6 fps（含开 App），间隔 <17/<25/<34/<50/<90 ms = 265/224/163/140/374，≥7 拍（≥58 ms）257 次；GPU 平均忙 2–5%，DCVS 每 10 s 约 100 次换档、0 失败，触摸升频 11–17 次/10 s；结束时电池 35.0°C。

**A2 第一份数据（g92，backboardd，35092 个 1 ms 样本）**：backboardd 大部分时间是空闲等待。CA display 线程（`com.apple.coreanimation.display`）只有 9.8% 在跑（小核 cpu2）+ 2.1% 在 GPU user client 的互斥锁上等（`lck_mtx_lock_contended ← runCore(AGXDeviceUserClient…)`），CA render 线程 2.2%（小核 cpu0）；metal2vulkan 的 scheduler/completion/handlers 线程 99% 在等；扫描输出 worker 80% 在 `pong_scanout_call` 里等请求。折算每帧（34 fps）backboardd 只花约 4 ms CPU，**其余时间在等上游**（SpringBoard 的 CA 事务提交）——下一步采样 SpringBoard。

**g93 提前结束的原因**：panic `Interrupts held disabled for 658541 ns (timeout = 625000 ns) @machine_routines_common.c:918`，线程是内核 `mk_timer_expire → kernel_mach_msg_send_common`。DEVELOPMENT 内核的关中断调试阈值（500 µs 级）在 Gunyah 下被普通抖动触发。按 UniXNU-sheng `board.toml`（`ml-timeout-lock-panic-scale=40`），`ci_boot_ufs.sh` 默认参数加 `ml-timeout-interrupt-masked-scale=40 ml-timeout-lock-panic-scale=40`（格式见 `osfmk/kern/machine.c` 的 `ml-timeout-<name>-scale`）。g77、g87 的提前结束日志里没找到同样的 panic 行（可能被位翻转/环形缓冲冲掉），原因未定。
g94（backboardd，带新超时）、g95（SpringBoard）进行中。

### A2 第二轮与 B1 第二步：大小核调度（21:30–22:00）

g94（backboardd，带放宽的超时）修正了 g92 的结论：g92 报告只打印前 48 项，运行样本按核拆成多项被挤掉了。实际 **CA display 线程（tid 1816）约 74% 的样本在运行，其中约 59% 在小核**（cpu0–3），14% 在大核——合成器是 CPU 密集、主要跑在 A510 上，每帧约 20 ms CPU。B1 第一步的占位无效，是因为 XNU 强烈偏向线程上次的核，display 线程优先级远高于优先级 1 的占位线程，直接抢占，不会迁到大核。g95（SpringBoard）的运行项也被截断，未能得出结论（tsample 统计头被位翻转时百分比按 1 计，数值放大 1e5 倍，读计数即可）。

于是做 B1 第二步：
- `patches/xnu-ipados18/0008-zizhan-amp.patch`：按 UniXNU-sheng `zzyf-sheng-amp.patch`（76780d7 + d4c299b）：`PONG.h` `__ARM_AMP__ 1`（去掉未用的 `NO_ECORE`）、`board_config.h` PONG `MAX_CPU_CLUSTERS 2`、`sched_clutch.c` 六处：无 CLPC 时固定优先级~default 桶默认偏好 P 簇、utility/background 偏好 E 簇；P 簇 pset 未建好前留在启动簇；UI 类线程不从 P 迁移/被偷到 E；空闲 P 核可偷 E 的活。我们的 xnu-11417.140.69 中 `sched_edge_steal_possible` 结构不同（按 QoS 循环），"不被偷到 E"那处按此版本重写为循环里跳过。sheng 后来的 d841bf5（Edge 矩阵检查）、b59018f（WFE 空闲）是 iPadOS 27/xnu-12377 分支的，不适用。
- `scripts/make-pong-adt-ipados18.py`（UniXNU-zizhan）：`PONG_ADT_CLUSTERS=1` 时把 DTS 的 `unixnu,cluster-type` 写成 ADT `cluster-type`（XNU `ml_parse_cpu_topology` 按 'E'/'P' 整数读；无 `HAS_CLUSTER` 时簇号即 `cluster_type == P`）。同一内核不带该属性就是单 SMP 簇，可 A/B。
- 0005 与 0007 都在 `thread.c` 文件尾追加，0007 应用后 0005 的"已应用"检查失败，合并为 `0005-pong-thread-diagnostics.patch`（旧文件在 Mac `src.noindex/patches-retired/`）。
- g93 的关中断超时 panic 处理见上；`ci_boot_ufs.sh` 已默认带 `ml-timeout-*-scale=40`。
g96（E/P 双簇）、g97（同内核单簇）进行中。

| 轮次 | 设置 | 结果 |
|---|---|---|
| g96 | E/P 双簇 + Edge（0008，ADT 带 cluster-type） | **在动的帧 48.8 fps**（此前 33–36），<17 ms 间隔 627 个；采样：CA display 线程运行全在大核（cpu 4/5/6）。但仍触发关中断超时 panic（约 1.7 ms，说明 `ml-timeout-interrupt-masked-scale` 没生效，原因未查），118 s 提前结束 |
| g97 | 同内核、ADT 不带 cluster-type | 60 s 回 fastboot（DarwinBoot 武装的 watchdog），日志 167 字节：AMP 内核在单 SMP 簇拓扑下早期卡死，**AMP 内核必须带 cluster-type**，无法同内核 A/B |

处理：`ci_boot_ufs.sh` 默认 `PONG_ADT_CLUSTERS=1`，并加 `interrupt-masked-debug-mode=1`（`SCHED_HYGIENE_MODE_TRACE`，只记录不 panic）。g98（backboardd）、g99（SpringBoard）确认中。
22:05：g98（E/P 内核 + `interrupt-masked-debug-mode=1`，采样 backboardd）21:56 启动后 420 s 内没回 fastboot，之后 USB 上看不到设备；g99 未运行（NO_FASTBOOT）。原因待用户看屏幕：卡在 iOS（自动复位/看门狗都应在 300 s/60 s 内生效）、fastboot 不枚举（20:28 那次的 USB 问题）、或其他。

### 22:00–22:35：E/P 调度确认、启动菜单问题

- g98 首次停在 UniXNU 菜单：当时怀疑命令行超长（1033 字节 > DarwinBoot24 的 1024），`ci_boot_ufs.sh` 去掉无效的 `ml-timeout-interrupt-masked-scale=40`，并加长度检查（≥1000 字节拒绝打包）。但缩到 967 字节后仍停在菜单——**不是（唯一）原因**。UniXNU `Library/UniXNULib/Main.c`：启动项返回/失败后回菜单、显示 "'<title>' returned: <status>"、关闭倒计时（有输入设备时一直等）。用户手动选 iOS 能正常启动，说明镜像没问题，是第一次自动启动失败过一次。原因待查（需要菜单上的 returned 状态码）。
- g98（E/P，backboardd 采样，用户手动从菜单进入）：**在动的帧 46.8 fps**（g96 48.8），无 panic（`interrupt-masked-debug-mode=1` 后不再误报）。采样：CA display 线程只在大核上跑约 10.6% 的样本（此前小核 ~74%），render 1.8%：**backboardd 不再是瓶颈**。
- g99（SpringBoard 采样）：运行项又被大量空闲线程挤出前 48 项，无结论。tsample 需要改成内核里按线程汇总（每线程 run-little/run-big/queued/wait）再输出，空闲线程合并。
- 下一步：① 查菜单第一次自动启动失败；② 改 tsample 按线程汇总后采 SpringBoard；③ 用户实机感受 E/P 后的流畅度。

### 22:40–：启动菜单停住的兜底、tsample 按线程汇总

- **DarwinBoot24 重试 + 失败记录**（`device/DarwinBoot24/DarwinBoot.c`，Mac `UniXNU-zizhan/drivers/DarwinBoot24`，旧版备份 `out/DarwinBoot24-pre-retry.efi`）：原 `Boot` 改名 `BootOnce`，新 `Boot` 最多试 3 次（间隔 0.5 s），每次失败把阶段（READ KERNEL / PRIMARY RAM / …，或 REQUEST）、状态码、NO WINDOW 行追加到 `logfs:\BOOTFAIL.TXT`；命令行带 `pong_autoreset` 时 3 次都失败就写 reboot-reason=bootloader、关中断、1 s 看门狗咬回 fastboot，不再停在 UniXNU 菜单（菜单那边返回后会关掉倒计时，一直等输入）。UniXNU 本身在 FD 里，没改。
- `cycle.sh`：取回 logfs 时若有 `BOOTFAIL.TXT`，存为 `logs/RUN.bootfail.txt` 并删掉（安卓把 logfs 挂在 `/dev/logfs`）。
- `ci_boot_ufs.sh`：`ALLOW_LONG_CMDLINE=1` 跳过长度检查，只用于测试上面的失败路径。
- **tsample 按线程汇总**（0005 的 `pong_ts_report`）：先输出每线程 `PONG TSUM rank name tid little big queued wait`（按 运行+排队 排序，最多 40 个），从没运行/排队过的线程合成一行 `PONG TSUM idle N threads wait W, untracked U`，之后的前 48 项也跳过这些纯空闲线程。`tools/mac/tsample.py` 解析 TSUM（两份投票）。改 0005 时先在 xnu-work 里 `git apply --reverse` 旧版再放新版（构建脚本对已应用但内容变了的补丁会走 `patch --fuzz` 分支，可能重复追加）。
- 批处理 `tools/mac/menu-sb.sh`：g100 用 1100+ 字节命令行故意让 DarwinBoot24 拒绝，验证重试/记录/回 fastboot；g101、g102 采样 SpringBoard。
- **构建脚本事故（22:50）**：第一次重编内核时 0001 被**反向撤掉**，`make` 报 `Unsupported CURRENT_MACHINE_CONFIG PONG`。原因：0008 改了 0001 新建/修改的 `PONG.h`、`board_config.h`，0008 在树上时 0001 的 `git apply --reverse --check` 失败；脚本退到 `patch --batch --fuzz=3`，而 `--batch` 会把"看起来已应用"的补丁当成反向补丁执行。g96 那次能编过，是因为 0001 检查时 0008 还没打上；之后 g97–g99 只重新打包没编内核。修复（`UniXNU-zizhan/scripts/build-xnu-ipados18.sh`，副本 `device/scripts/`）：0002 以后的编号补丁和 zz* 晚补丁一样，每次先倒序撤掉再顺序重打（原有的 mtime 快照保证没变的文件不重编）；回退分支加 `--forward`。xnu-work 已手工恢复（`git checkout`，删 .orig 和新建文件，按序重打 0001–0008，逐个反向检查通过）。
- **g100（失败路径验证）**：命令行 1204 字节，DarwinBoot24 三次 `REQUEST failed Bad Buffer Size`，写 reboot-reason `00 -> 04`、看门狗咬，**29 s 回到 fastboot**，`logs/g100.bootfail.txt` 四行齐全。以后自动轮第一次启动失败会自己重试，三次都失败也会回 fastboot 并留下失败阶段和状态码，不会停在菜单。
- **g101/g102（E/P 内核，SpringBoard 采样，按线程汇总）**：SpringBoard 主线程（tid 264，无名）运行 12.7–13.3%（全在大核）、排队 0.6–0.8%，其余线程运行 <0.5%；metal2vulkan 各线程 99% 在等。结合 g98 的 backboardd（display 线程大核 10.6%）和 GPU 忙 3–10%：**两个进程和 GPU 都远未饱和**。滑动帧率 48.x / 47.3 fps。g102 的统计头被位翻转吃掉，`tsample.py` 改为头缺失/不一致时用最忙线程的样本总数当分母。
- **帧间隔现在按节拍量化**：g101 `beats 540/679/267/316/82/24/42 off 838`（1–7+ 拍，偏离整拍 >2 ms 的 838/2944 = 28%），g82（E/P 之前）是 62%。也就是说 E/P 之后渲染变快，帧开始等 MiniIOMobileFramebuffer 自己的节拍了。annibale `docs/analysis/2026-10-28-panel-120hz-path.md`（同源驱动）：上屏时刻吸附到节拍网格（`for (next = fBeat + period; next < began; next += period)`），帧周期 = ⌈非节拍时间 / 周期⌉ × 周期；`fb_queue=2` 时后一帧在前一帧复制完才取出，节拍等待停在约一个周期减复制时间；`fb_beat=0` 直接不等。→ 方案 B2：`tools/mac/b2-pace.sh`，g103 基线 / g104 `fb_beat=0` / g105 `fb_hz=60` / g106 `fb_queue=1` / g107 `fb_beat=0` / g108 基线。
- g103–g105（B2 第一批）：汇总行几乎全被位翻转打坏（电池 35–36.5°C）。能读的 g105（`fb_hz=60`）：在动的帧 41.5 fps，节拍 1/2 拍 854/501，偏离整拍 923/约 2900。这一批不可用于比较，等 klog 上线后重跑。

### klog：内核把 ramoops 直接写进 UFS（23:20–）

位翻转是复位期间 DRAM 自刷新的问题，不是日志格式问题：再多份数投票也救不回连续多处翻转的行。改为不经过 DRAM 复位：
- `device/NativeUfs/PongUfsTransport.cpp`：boot-arg `pong_klog_4k=<相对 userdata 的 4 KiB 块号>`，指向安卓 /data 上一个 pinned 连续文件（同 root.img 的做法，写在 f2fs 和 dm-default-key 之下）。开机时检查：在 userdata 内、不和 root 镜像重叠、头块是 `PONGKLOG` + 自身块号 + 1024、`pong_ufs_readwrite` 打开；用 `ml_io_map_wcomb` 映射 ramoops 4 MiB（与内核自己的映射同属性）。`pong_klog_flush(reason)`：把 CONSOLE、DIAG 两个区原样用 FUA WRITE(10) 分 32 块写到头块后面，再写头块（state 1 = 写成，2 = 失败，flush 次数、时间、原因、IOReturn）。panic 时不取锁（其他核已停；轮询用 IODelay，可在 panic 上下文里跑），中途失败也不影响 DRAM 里那份。
- `ArmDTPlatform/PongBootReset.h`：截止复位（`resetNow` 之前）调 `pong_klog_flush(1)`，panic 复位（`panicReset` 开头）调 `pong_klog_flush(2)`（弱符号）。
- `tools/mac/klog.py`：`setup` 用 pinalloc 建 `/data/hackios/klog.pin`（6 MiB），确认连续，存 `klog.json`；`arg` 打印 boot-arg；`arm` 头块置 0 状态并把数据块清零（只写这 1025 块）；`read OUT` 头块 state=1 时按 RamoopsDump 的格式还原两个区（`XNULOG v1 (zizhan klog …)`），下游 fpsvote/tsample 不用改。
- `cycle.sh`：开机前 `klog.py arm`；回安卓后 klog 写成就把它当 `RUN.txt`（DRAM 那份改名 `RUN.ramoops.txt`）。`ci_boot_ufs.sh`：有 klog.json 就自动加 `pong_klog_4k`（`KLOG=0` 不加）。
- 命令行长度：`pong_ufs_async` 默认改成 0（=1 是 g16 那个会挂死的试验，此后每轮都显式关掉），省 17 字节；长度检查改到 DarwinBoot24 的真实上限（LEN ≤ 1020，DarwinBoot24 拒绝超过 1023 个字符）。
- **g109 验证**：klog 文件在 userdata+14344192（LUN 0 块 17357312，root 镜像之前，不重叠）。截止复位前写成（reason deadline），`PONG FPS SUMMARY` 96 份全部完整、8 份投票一致；**整份日志非 ASCII 字节 36 个，同一轮 DRAM 副本 63312 个**，DRAM 那份的汇总行已读不出。g109（基线，2 s 滑动，电池 34.9→36.7°C）：在动的帧 49.1 fps，间隔 <17/<25/<34/<50/<90 ms = 1118/556/439/273/37，节拍 1–7+ = 695/760/377/489/60/9/33，偏离整拍 1228/2423。
- B2 用 klog 重跑：`tools/mac/b2-pace.sh`，g110 基线 / g111 `fb_beat=0` / g112 `fb_queue=1` / g113 基线 / g114 `fb_beat=0` / g115 `fb_hz=60`。（第一次启动时 `${SPECS:-"a b" …}` 把每个参数拆开了，立即停掉重来，改用数组。）

### B2 结果（klog，2 s 滑动，降温到 35°C，每轮结束约 36.6°C）

| 轮次 | 设置 | 在动的帧 | 间隔 <17/<25/<34/<50/<90 ms | 节拍 1/2/3/4/5/6/7+ | 偏离整拍 |
|---|---|---|---|---|---|
| g109 | 基线 | 49.1 fps | 1118/556/439/273/37 | 695/760/377/489/60/9/33 | 1228/2423 |
| g110 | 基线 | 48.6 fps | 1131/524/445/269/40 | 689/750/391/427/106/15/31 | 1093/2409 |
| g113 | 基线 | 50.7 fps | 1256/499/455/248/42 | 804/789/322/455/80/12/38 | 1054/2500 |
| g111 | `fb_beat=0` | 40.7 fps | 759/281/566/364/93 | 586/249/411/616/94/23/84 | 1103/2063 |
| g114 | `fb_beat=0` | 49.5 fps | 1131/369/476/255/35 | 844/457/342/503/81/10/29 | 1140/2266 |
| g112 | `fb_queue=1` | 50.2 fps | 1252/449/487/245/42 | 818/722/335/479/71/12/38 | 1144/2475 |

结论：三种设置都在 49±1.5 fps 的噪声里（g111 偏低一轮），**驱动的节拍等待和交换队列深度都不是瓶颈**。`fb_beat=0` 时帧间隔仍然一半以上落在整拍上，所以量化来自 CoreAnimation 自己的 120 Hz 时钟（驱动注释：CA 按它读到的刷新率自己计时），一帧端到端跨 2–4 拍。默认保持不变。DCVS 统计（g113）：GPU 忙约 20%，57–65% 时间在 900 MHz。下一批 `tools/mac/gpu-lat.sh`：固定 900 MHz（`pong_gpu_dcvs=0 pong_gpu_level=13`）/ 下限 710 MHz（`pong_gpu_dcvs_min=9`）/ 基线。
- g115（`fb_hz=60`）41.4 fps（节拍 16.7 ms：1 拍 1194、2 拍 724），比 120 Hz 差，保持 120。

### GPU 频率与整机 GPU 丢失（00:15–00:45）

| 轮次 | 设置 | 在动的帧 | 备注 |
|---|---|---|---|
| g116 | `pong_gpu_dcvs=0 pong_gpu_level=13`（固定 900 MHz） | — | 第 55 帧 **GPU 丢失**，整轮 54 帧 |
| g117 | `pong_gpu_dcvs_min=9`（下限 710 MHz） | 48.9 fps | 64–70% 时间在 900 MHz |
| g118 | 固定 900 MHz（重复） | 40.8 fps | |
| g119 | 基线 | — | 第 63 帧 **GPU 丢失** |

结论：**GPU 频率不是瓶颈**（固定 900 MHz 不比 DCVS 好）。到这里 B1（大核）、B2（节拍/队列）、GPU 频率都排除了：每个进程 CPU 占用低、GPU 忙约 20%，帧仍跨 2–4 个 CA 节拍——剩下的是流水线里的串行等待（CA 提交 → backboardd 渲染 → metal2vulkan → 内核提交/fence → 扫描输出），需要逐帧时间线才能拆，下一步按计划 C/D（用户态）。

**整机 GPU 丢失**（g116、g119，之前约 20 轮 0 次）：backboardd 的扫描输出拷贝 2 s 不退休（`GPU copy not retired … status=0xe00002bc`），之后 metal2vulkan 报 `DeviceLost`、GPU 忙 0%，内核没有任何记录。界面就此冻住，对用户就是"卡死"。代码里（`PongAdreno/submit/`）：`NativeAdrenoSubmit` 和 `NativeAdrenoRing` 一旦 `poison()` 就对之后所有调用返回 NotReady；Ring 的 poison 条件包括读到的 fence 值 > 已发出的 seqno、RPTR 越界、缓存维护/写 WPTR 失败。提交与等待都在 `g.lock` 下，排除了"先写 WPTR 后 ++seqno"的竞态。健康轮里每轮都有壁纸扩展 MercuryPosterExtension 的 DeviceLost（只影响它自己），可能是同一机制的单进程版本。
- 诊断：`native_adreno_ring_poison[6]`（次数、调用点返回地址低 32 位、当时内存里的 fence、seqno、next、RPTR）、`native_adreno_submit_poison[2]`；`PongAdrenoScanout.cpp` 的 `fenceReport` 打印 `PONG FENCE`（原有但从未打印的 fence 等待记录）和 `PONG POISON`：扫描输出卡住时、每次 poison 计数变化（随 10 s DCVS 行）、截止复位前。批处理 `tools/mac/hang-diag.sh`（g121–g124，复现即停）。
- g120（`dcvs_min=9`）第 61 帧也丢了 GPU：最近 5 轮 3 次，都在第 55–63 帧。g121（48.8 fps）、g122（40.0 fps）正常，poison 计数 0：**壁纸扩展每轮的 DeviceLost 不经过内核 poison**。
- 时间线：MercuryPosterExtension 启动约 3 s 后报 `IOSurfaceSharedEventAddEventListener failed: e00002c7`（共享事件监听不支持），健康轮里它再过约 2 s 自己 DeviceLost（g121：09:47:28.6 → 30.8），而三次 GPU 丢失都发生在它开始渲染后 1–3 s，且那几轮它**没有** DeviceLost。推测：壁纸扩展的某个作业在 GPU 上等一个永远不来的共享事件，所有进程共用一条 ring，排在后面的 backboardd 扫描输出拷贝 2 s 不退休 → 扫描输出模块自己置 poisoned、backboardd DeviceLost；谁先超时谁"丢设备"。待证实。
- 诊断加强：`native_adreno_ib_log`（最近 4 次提交：fence、首个 IB 地址/长度、前 28 dword，原有但从未打印）加上提交进程名 `native_adreno_ib_log_who`，卡住时打印 `PONG IB` 4 行。批处理 `tools/mac/hang-diag2.sh`（g125–g130，复现即停）。
- g123 正常（48.9 fps）。**g124 复现**：卡住时已发出 160、完成 151，但截止复位时完成 197/198——**GPU 没有永久挂死，是停了 2 s 以上又继续**。卡住前一刻壁纸扩展报 `texture over buffer refused: OutOfMemory(Device)`（非 backboardd 的进程窗口较小）。用户态 worker（`PongMetal/PongMetalScanout.h` + `GpuScanoutPresenter.h`）先报 `GPU scanout copy failed … status=0x4` = `kGpuFailed`（blit 命令缓冲 `waitUntilCompleted` 后状态为 Error，即 metal2vulkan 判定失败），然后 worker 线程退出、内核 `finish()` 收到失败状态把扫描输出永久置 poisoned。**界面永久冻住的直接原因是这一次失败就放弃**。
- g125、g126 正常（50.5、49.6 fps）。**g127 复现**（带 IB 记录）：GPU 停在 fence 144（143 已完成），最后 4 次提交：145 backboardd，**146、147、148 壁纸扩展**；这一轮壁纸扩展自己也 DeviceLost 56 次。五次 GPU 丢失（g116/g119/g120/g124/g127）都在壁纸扩展开始渲染后 1–3 s。
- 对照实验：内核 `NativeAdrenoPublish.cpp` 加 boot-arg `pong_gpu_deny=<进程名>[,…]`（在 `AGXDeviceUserClient::mount` 拒绝连接），`tools/mac/deny-poster.sh` g128–g133 六轮拒绝 MercuryPosterEx。基线最近 13 轮丢 5 次，六轮 0 次的偶然概率约 5%。

### 壁纸扩展不用 GPU（01:30–02:05）

| 轮次 | 设置 | 在动的帧 | GPU 丢失 |
|---|---|---|---|
| g128 | `pong_gpu_deny=MercuryPosterEx` | 55.9 fps | 无 |
| g129 | 同上 | 53.4 fps | 无 |
| g130 | 同上 | 53.9 fps | 无 |
| g131 | 同上 | 54.3 fps | 无 |
| g132 | 同上 | 55.0 fps | 无 |
| g133 | 同上 | 54.2 fps | 无 |

对照：g109–g127 基线 13 轮里 5 轮 GPU 丢失，正常轮 48.6–50.7 fps。**拒绝壁纸扩展后 6/6 无丢失，滑动帧率平均 54.5 fps（+5 fps，约 +11%），1 拍（<8.3 ms）间隔占比从约 28% 升到约 37%**。壁纸扩展本身就在和 backboardd 抢 GPU（它的作业与合成器共用一条 ring），并且它的某些作业（窗口小、`OutOfMemory(Device)`、管线状态无效）会让 ring 停 2 s 以上。

处理：内核默认拒绝 MercuryPosterEx（`NativeAdrenoPublish.cpp` `deniedClient`，`pong_gpu_deny=-` 恢复，或给别的名单）。没加成 boot-arg 是因为命令行已接近 DarwinBoot24 的上限。**待用户看**：壁纸现在显示成什么样（黑屏/静态图/其他），以及滑动手感。根治方向（后续）：① 用户态扫描输出 worker 一次失败不再永久放弃（重试/重建设备）；② 查壁纸扩展的作业为何让 GPU 停顿（窗口大小、OOM 路径、无效管线）。
