# 兄弟项目 GPU 进展更新（2026-11-02，接 10-31 调研）

来源：各仓库的只读 blob:none 克隆，加上 `gh api`。前一份是 `docs/research/2026-10-31-sibling-fps-survey.md`，截止 10-31 约 12:00（UTC+8）；那里写过的内容这里不重复。时间一律 UTC+8。"已上机"指作者在手机上量过，"未上机"指只在主机上构建或测试过。我们自己的代码只做了只读对照：`drivers/PongMetal/NativeBridgeMetalFrontend.mm`、`PongMetalPlugin.mm`、`patches/metal2vulkan/`。

**先注意一件事**：11-01 05:20–06:22，UniXNU 系仓库把剩下的远端分支一次性合并进 main，提交日期被改写成 11-01 05:xx。涉及 UniXNU-sheng PR #28/#30、UniXNU #130/#134、Adreno740 #8/#9、AdrenoMetal d0512ed/831a9dc（PR #11）。不少 GPU 改动的作者日期其实在 10-28～10-31，原先在 `feat/ipados27-gpu-parity` 等分支上，旧调研只看过 AdrenoMetal 的同名分支，所以这里一并列出，并标出作者日期。

## 0. 最值得做的五件事（细节见 §3）

1. **ASTC 黑屏的原因基本可以确定在我们的前端**：`NBTextureLayout +layoutForDescriptor:` 遇到压缩格式（`info.flags&0x400`）、mip>1、array>1 时返回 nil（`NativeBridgeMetalFrontend.mm:233-234`）；`newTextureWithBytesNoCopy` 又要求页对齐且 `length>=size`（`:1438-1443`）。所以 ASTC 的布局和 NoCopy 纹理都是 nil。annibale 已经把同一条链查清并修好（bo47→bo48）：
   - ImageIO/CoreUI 按设备的纹理布局解码 ATX/KTX；布局为 nil 时数据长度为 0。
   - 于是 `mach_make_memory_entry_64` 大小为 0，返回 `KERN_INVALID_ARGUMENT`，backboardd 报 "CompressedImage port is MACH_PORT_NULL!"，拒收 `SBMainSwitcherWindow` 的整次 commit。
   - 所有 App 的 scene 都挂在这个窗口下，所以表现为"App 和多任务全黑、主屏正常"，和我们 v4 的现象完全一致。
   - 可直接移植的实现有三份：annibale `redwoodmetal_texture_layout.inc`（6ece701）、dada `nativemetal-dada.patch:2183-2338`（基于 m2v 720ef1c，和我们同一基线）、AdrenoMetal 4d789e9 `DVMLinearTextureLayout`（main `:3573-3700,3948-3971`）。
2. **翻页回弹那段 30 Hz 很可能是 UIKit 的 display link 自己要的**。hakios 7caa457（11-01，已上机）：`UIViewInProcessAnimationManager` 的 display link 请求 20..30 fps，翻页停下后约 0.7 s 里帧间隔都是 33.3 ms。SpringBoard 里丢掉它请求的速率后，右滑 78→115 fps，>16.7 ms 的帧 17%→1%。我们 g136–g143 的帧间隔 p90 恒为 33.2 ms，g153 也记录过"轻动画段 40 Hz→30 Hz"，两者对得上。另外：我们的 ADT 身份是 iPhone17,1，缺 `CADisableMinimumFrameDurationOnPhone` 的进程会被 CA 限在 60（c7b5f9a）。
3. **CA 着色器的寄存器溢出与常量折叠**：
   - hakios-metal 9a61305：CA 的 uber shader `fixed_frag_lph_cpf` 开着 ir3 preamble 时用满 48 个寄存器，每个 fiber 仍溢出 512 B；关掉 preamble 只要 21 个寄存器，每个 draw 6.6→2.6 ms（A635，已上机）。
   - e59090f：按 52 字节的 `FragmentShaderConfig` 特化后，一个 draw 9.6→0.2 ms。
   - m2v 路径上的同类补丁是 AdrenoMetal `0004-fold-constant-global-field-loads` 和 dada `const_loads.rs`（`fixed_frag` 的 SPIR-V 约 46000 条降到几百条）。**我们的 patches/metal2vulkan 里没有这一条。**
4. **内存的三处缺口**（按收益排）：
   - 设备级 `currentAllocatedSize`/`recommendedMaxWorkingSetSize` 没回真实值。我们只在 heap 上实现了（`:1272`）。dada g2k15 记录过：回 0 时 CA 在 backboardd 的缓存涨过 1 GB，手机卡死。
   - Turnip darwin BO 缓存没有上限。Droid-VM 记录 render server 的缓存一分钟涨到 434 MiB；UniXNU b624df5 的做法是 1 s 内没被复用就释放。
   - m2v 每次造 64 MiB 块要 30–110 ms：AdrenoMetal e0997b0 room keeper，dada `M2V_SPARE_MB`。
   - 内核侧：sheng c8806db 改用系统内存 BO，view 映射的是 VM 对象，从结构上不可能出现 pmap_assert_free。
5. **壁纸**：sheng 有现成的两层放行（`NativeSandboxGrantProcess="*"`，加 MIG 2863/2864 的 kobject 过滤豁免）。之后还要按 annibale 的顺序补一串缺口；单采样能把壁纸每帧 135–195 ms 降到 40–55 ms（dada）。要先有 GPU 优先级或限速：sheng 实测慢帧多数是排在海报扩展约 77 ms 的任务后面。

---

## 1. 各项目 10-31 以来的更新

**iola1999/hackios-redmi-k90-annibale（K90 / SM8750 / A830，iOS 26，AIR→NIR→freedreno Gallium，不用 m2v）**：10-31 后最有价值的一组，提交 32c56d3…f36fc12，内核与驱动在 `iola1999/hackios-xnu-kernel` 分支 k90-port。
- 壁纸（`docs/analysis/2026-10-31-mercury-wallpaper.md`；驱动 8c22c63、b6a6110、52e1bd2）：bo24→bo38 逐轮补齐，从 bo37 起主屏和锁屏都有壁纸，详见 §3c。bo38 有壁纸时 59 次滑动平均 52.7 fps，无壁纸的 bo23 是 52.9。
- 应用打开黑屏（`docs/progress/2026-10-31-boot-order-and-performance.md` §6，驱动 6ece701）：根因就是上面第 1 条 ATX→纹理布局 nil 的链；bo48 起 Safari、短信、设置都能画出来。
- 局部重绘残留：backboardd 设 `CA_DISABLE_DIRTY_REGIONS=1` 后，设置里成块的旧内容、空白或变色的小组件都消失（bo48/49）。代价是每次提交 GPU 时间 1.96→3.38 ms，每帧送面板的行 393→910；翻页仍是 53.6 fps。从 bo50 起默认整帧重画。
- 交互闪烁（bo55）：iOS 26 的 CA 只在两块 surface 之间交替。内核在一块的像素拷完之前就放行了下一次 swap，CA 于是在正在显示的那块上画下一帧。改为拷完才放行（`fb_queue` 默认 1）。
- Dock 玻璃（bo50–52）：ir3 把片段着色器的第一次纹理读改成启动前预取，在 A830 上读到全 0。修法是 `0006-a830-no-fs-tex-prefetch.patch`。
- 回首屏图标空白（bo57–61）：iconservicesagent 的 GPU 空闲 3 s 定时器会清掉 RenderBox 的全部管线。驱动按模块、入口名与附件形状缓存管线（4589382），48 条里 44 条复用；再补上 Metal 着色器磁盘缓存目录 `/var/empty/Library/Caches`（属主 `_iconservices`）。从开始重画到导入图标纹理 0.91–1.06 → 0.24–0.26 s。
- 触摸（d62d3b6）：改读 TLMM 锁存的中断沿，触摸采样间隔 36–90 → 9.3 ms，拖动帧率中位 42.8 → 51.6 fps。
- GPU 内存（bo62/63）：内核对 ≥1 MB 的数据分配不再放保护对象。之前地址空间按 2 的幂和 4 槽浪费，约 1 GB 时 2267 MB 的数据区间就用完，出现 `no memory for a buffer`，接着 Mesa `fd_bo_heap_alloc` 拿到 NULL，backboardd 崩溃。修后 GPU 内存峰值 584 MB 无崩溃。
- 调频（cmd50）：频率下限 342 MHz、触摸后保持 1.5 s。20 次翻页 53.4 vs 53.6 fps，**和帧率无关**。
- 两份对照文档：`docs/analysis/2026-10-31-dada-reference-comparison.md`（对照新仓库 superboyyy/hakios-arm64e-dada）、`docs/analysis/2026-11-01-hakios-upstream-lessons.md`（对照 oosexgang 上游）。11-01 起转去做 Wi-Fi（ath12k）。

**Chengdong-Yu/UniXNU-pong**：5e14181、4f43170、aaa2d73（11-02）只把 CI 改成 UniXNU 的打包路线（`package.yml`、os-profiles 22H374）。没有 GPU 改动。

**Ken-u/MaliMetal**：
- ca77d29（10-31 23:52）：iconservicesagent 和 Preferences 也拿到 Metal 设备，因为 Core Image 在这台设备上没有 OpenGL ES 回退。
- a2b7671：音频插件进信任包。
- 没有新的帧率数。

**MC-XiaoXiao/ios_sagit（A540 自写原生驱动）**：
- 1cd124f（11-01）：纹理的后备内存在私有任务地址空间里整体映射，因为 Safari 回主屏时 A5xx 会读到行尾 padding 并 fault。普通 buffer 仍按范围映射。
- 8016626：电池和 USB 驱动。

**ShueiTsuki/data-a-live**：10-30 之后没有提交（各分支都没有）。

**YinCQL/UniXNU-picasso**：57 笔提交，内容是 SMP、Novatek 触摸、cpufreq、看门狗、到达锁屏和主屏。显示仍是软件路径（69405d1）。没有 GPU。

**akshaynexus/iOSPlus10**：`main`（A730/negroni）从 10-29 的 146e889 起没有新提交。10-31 之后的 286 笔全在 laurel_sprout 支线（小米 Mi A3，SDM665/A610，iOS 16.5，backboardd 仍走 GL），11-01 19:00 之后的 GPU 改动都标 "built, NOT BOOTED"。与我们相关的条目：
- 海报扩展的 jetsam 限额：Rendering 30 MiB，而一个 Metal 设备就要 15–20 MiB。0ccdcbc 把扩展点提到 ≥64 MiB、海报提到 128 MiB（`root-add-metal-ios26.sh:71-75`）。
- launchd 扩展 check-in 超时默认 5 s，海报开设备加建管线会超时。`launchd_disable_all_timeouts=1` 之后不再被杀（已上机）。
- 2156（c536651，已上机）：GPU 池的空闲表没有锁，多核并发把它撕裂。修法是一把锁、buffer 不跨 64 MiB chunk、free 必须精确匹配发出的 buffer。
- 2895（未上机）：页表一次性预分配，池最后 64 MiB 只给 backboardd。2916（未上机）：在拒绝分配之前，先收回除 backboardd 外所有客户端缓存的 buffer。
- m2v 从 f6a96b7d 升到 219f013：主机上 QuartzCore 的 SPIR-V 1,906,384 → 788,556 B（−58.6%），没在 Adreno 上跑过（`upstream-2026-11-01.md`，013e72c/bb5388a）。
- Turnip 有 3312 个 weak import 走 flat lookup，SpringBoard 打开 Metal 设备要 10138 ms（2e16699 `macho-weak-self.py`，未上机）。
- negroni 的教训：kHz/MHz 弄错，A730 在约 5 MHz 跑了好几天（0237）。GPU 频率必须读回实测值。

**pawlickerr/UniXNU-negroni**：只有 10-22 的 6 笔提交，没有 GPU。devices.toml 说它有 "iOSPlus10 Metal compositor results"，在仓库里找不到。

**UniXNU/UniXNU-sheng + UniXNU/UniXNU + UniXNU/Adreno740（SM8550/A740）**：
- sheng 已在 **iPadOS 27.0.1 24A446** 上合成，内核是源码编的 xnu-12377（41168fa，作者日期 10-28；`patches/xnu-12377/zzzz-sheng-ipados27-bringup.patch` 1761 行），下一个目标是 26.5.2 arm64e（7bfbe9a）。11-01/02 的提交多是打包和 CI。
- 10-31 12:00 之后真正新写的 GPU 改动：
  - Adreno740 3f4cb94/18d79d7 与 sheng 25a0eaa：CP 两环抢占，默认关。
  - dd43abd：fence 轮询不持锁。
  - 01ed692：连续页请求只到 128 KiB，BoAlloc 最长 260/297/239 → 137/228/48 ms。
  - 14e4a7d：BoFree 在锁外 unwire，之前一次 free 持锁 38 ms。
  - 36c4e44：VM 映射改按区索引，之前 BoMap 持锁最长 70 ms。
  - 733a5cc：大分配带调用栈导出。
  - 7329ccc/407db1a：着色器预热列表嵌进插件（162 个库）。
  - UniXNU 359d77c `docs/zh/porting/gpu.md`「定位慢帧的耗时」：慢帧多半排在海报扩展每个约 77 ms 的任务后面；render pass GPU 时间中位 0.5 ms，全屏 1.1 ms。
- 作者日期在 10-28～10-31、之前在分支上的：c8806db（系统内存 BO，不再用固定池）、e7a7a31（把 GPU 授权给所有 Metal 客户端）、0c52609/cc62bf4（vmm-present→RGhA）、b624df5/caffd5d/bdcf746（Turnip BO 缓存）、e038ba3（线性 pitch 改为 64 B 对齐）、2c40ae9（环内依赖不做 CPU 等待）、1105b0e（hangcheck、重启并重放）。
- Issues：UniXNU#126（fuxi/A740，开任何 GPU 开关 userland 就 sig=5）无人回复。

**UniXNU/AdrenoMetal**：
- d0512ed/831a9dc（11-01）把 gpu-parity 等分支以 ours 方式合并，所以**编码线程已经在 main**（`NativeBridgeMetalFrontend.mm:1965-2065`，render server 默认开）。上次说"不在 main"已经过时。
- e0997b0（10-31 18:06）`0021-spare-image-blocks-and-room-keeper`：释放的专用块留作备用（10 s 或内存压力时释放），`keep_room` 挪到单独线程。注释："each 64 MiB block … 30-110 ms in the texture creation that asked for it"。依赖 `0013-growing-memory-blocks`。
- 94db828/69712c0：trace 加每个 pass 的 GPU 起止（type 7）和 CB 各阶段（type 12，`0022`）。
- 8f6a2eb：按 task 设 GPU 优先级（内核方法 17）；`nmb_slowest_pass()`。
- 2a96ba6/a0f0287：≥3 ms 的 pass 带最重着色器的指令数、取纹理数、GPR、带宽等，经方法 18 写进内核日志；`0023` iOS 上的 QoS。
- 这些都**没有帧率数**。11-01 之后没有新提交。

**steelbrain/metal2vulkan**：10-31 04:00Z 之后 master 和 development 都没有提交，最新是 development bf3ce3813（10-31 01:39Z）。我们的基线 720ef1c 之后值得关注的几条：
- bf3ce3813：能读 Core Image 的 kernel 库（file_type 1）。我们 SBRendererService 现在只靠拒收绕开。
- ec6b322bb：能读 fat metallib 的 AIR slice（QuartzCore 那个 187 MB 文件）。
- 6e88d181d：管线创建记录加跨进程后台预热。
- 8d75695ea：`Buffer::host_backing()`。
- 0dfdbcd：去掉 padded 别名、去掉 MetalLevel。Mac 上逐像素对比 qc 115/3 → 116/0，没上过手机（`dada:gpu2/M2V-NEXT.md`）。

---

## 2. 新发现的项目（旧调研没有）

| 项目 | 设备 / SoC / GPU | 路线 | 进度与对我们有用的点 |
|---|---|---|---|
| **oosexgang/hakios-metal**（vcvkk） | POCO X5 Pro / SM7325 / A635（7c+ Gen3, A6XX） | 自写 C++ Metal 驱动，直接出 PM4，Mesa 只用 NIR→ir3；iOS 16.5 | CA 经 Metal 已上机（hakios-xnu-kernel f693d9d/96ffbc5/5c819b8）：**设置滚动中位 8.5–8.8 ms、打开 App 8.0 ms、翻页 8.4–8.5 ms**（约 115–120 fps）。GPU 分析最细：GMEM/sysmem、硬件分 bin、UBWC、uber shader、spill（§3e） |
| **oosexgang/hakios-xnu-kernel** | 同上（hakios 主线，含 HakiosMetalCA.m） | 默认 CA 走 GLES（eagl），Metal 是 `METAL_PLUGIN=ca` | GLES 路线设置滚动 115、home 键 70（f693d9d）；120 Hz 全链：`IOMFBDisplayRefresh`（b6c1f82/0dedaec）、`fb_ca_hz`（f6ebf31）、`CADisableMinimumFrameDurationOnPhone`（c7b5f9a）、**display link 20..30**（7caa457）、caenv 给 SpringBoard（a5620ff）；`ax_rt/ax_rm` 归因（a48d2b2） |
| **superboyyy/hakios-arm64e-dada** | 小米 15 / SM8750 / A830，iOS 26 arm64e | 两条：AIR→freedreno（默认），gpu2 = UniXNU 前端 + m2v 720ef1c + Turnip | gpu2 补丁基于我们同一 m2v 基线：ASTC 布局/NoCopy、swizzle、XR10、MSAA→单采样、`M2V_BLOCK_MB/M2V_SPARE_MB`、设备内存回答、常量折叠、`ubwc_version=0`。**没有整体帧率数**（最后提交 f1ae906，10-31） |
| **iola1999/hackios-xnu-kernel** | K90（annibale 的内核与驱动） | 同 annibale | 6ece701 纹理布局；4589382 管线缓存 |
| **Droid-VM/UniXNU-droidvm** | crosvm/Gunyah 虚拟机，virtio-gpu（drm2kgsl 到宿主 Adreno） | AdrenoMetal + Turnip，iPadOS 17 / **27** | iPadOS 27 壁纸的 GPU 客户端名单（8277f55）；GPU 内存不计进 footprint（b846440）；`memorystatus_pressure_threshold_mb=1280`（65d9d21）；**Turnip BO 缓存预算补丁**（fork `Droid-VM/UniXNU` 分支 wip/droidvm `patches/mesa/turnip-darwin-bo-cache-budget.patch`） |
| **none24smu/UniXNU-zeta** | Surface Duo 2 / SM8350 / A660，iPadOS 26.5.2 | AdrenoMetal + m2v | 壁纸修了多轮仍黑；Mercury GPU VA 高水位 2855 MiB；连续页搜索卡顿；Turnip 提交背压；present 55–83 ms（README "Graphics runtime and contiguous-allocation stalls" 节） |
| **nithinyadavamaraboina-online/IOS-26-Hot** | Realme X2 Pro / Pixel 4，SM8150 / A640，iOS 26.5.2 23F84 | m2v 720ef1ce + 19 个补丁 + Turnip（`HotMetal2`） | 自报主屏翻页 **52.9 fps**、设置 39.7（60 Hz，关 live backdrop）；CA 压缩四项开关；纹理池限额 backboardd 192 MiB/32、SpringBoard 64 MiB/24；0143 BoFree 前先等完 fence（HANDOFF.md、NOTES.md） |
| **RigoLigoRLC/unixnu-sm-t733** | Galaxy Tab S7 FE / SM7325 / A642L | AdrenoMetal + Adreno6xx | 按 pass 比 GMEM 和 sysmem（`docs/GPU_SYNC_CAPTURE_72_2026-10-31.md`）：8 draw 5.143 vs 7.794 ms，33 draw 3.802 vs 1.816 ms；A6xx 上 GMEM 红蓝互换（`docs/GPU_COLOR_75_2026-11-01.md`） |
| **rootstockos/UniXNU-asteroids** | Nothing Phone 3a / SM7635 / A810 | AdrenoMetal | T62b：1 GiB 池满后 GPU 停住 → T64 改用 sheng 882f4bf 的可分页 BO；T78q：GPU 在 23% 的时间里闲着等串行的 CPU 阶段；T86 线性对齐只对 backboardd 答 64（porting.toml） |
| **IssacL04/UniXNU-Pagani_SM8750** | 一加 13T / SM8750 / A830 | AdrenoMetal | 7312ba7 A830 Metal 起来了，滑动最高约 15 fps（STAGE6.md:1313） |
| **YahiaAngelo/vphone-linux** | Linux 上跑的 vphone（ParavirtualizedGraphics 加 reims-vgpu） | m2v fork（vphone-next2 cd1a08ae） | iOS 27 主屏、壁纸、小组件跑在 NVIDIA 上（README:55-63）；8f516b4 `air.block_read_texture_2d` |
| Emilelu/UniXNU-mondrian（K60, **SM8475/A730**）、takasugl/UniXNU-udon（SM8475）、1696108518/UniXNU-liuqin（SM8475）、cotunjr/hakios-mayfly（12S, SM8475） | 和我们同 SoC | — | mondrian 链接了 PongAdreno，但**没上机**，现在转去 darwin24/22H374 路线；其余几个还没到 GPU |
| 其他 | lonzzi/UniXNU-houji（A750，"GPU submission pending"）、hoshikv/UniXNU-peridot（A735，跟 UniXNU 的 gpu-parity）、Snapintosh/SnapMetal（X2-90，10-25 后无更新）、Dracen-W/MaliMetal（G52，脚手架）、ry-diffusion/Chlorophyll（Bergamota A630，AIR→ir3）、CrossDarwin/macOS-ForSheng、xVanTuring/UniXNU-tb128fu（A610 出帧） | | 都没有对问题 a–g 有用的数 |

---

## 3. 按问题：别人怎么做 → 对 A730+m2v 是否适用 → 我们下一步（各节内按预期收益排）

### a. ASTC（DeviceSupportsASTC=真时 App 全黑）
**别人的做法**
- annibale 6ece701（`redwoodmetal_texture_layout.inc`）：`newTextureLayoutWithDescriptor` 返回驱动自己的线性布局：每个 slice 的每一级按纹素块成行、无填充，alignment 256，`copyGranularity` 等于块尺寸。实现了 `initializeTextureMemory`、`copyFromLinearBytes`、`copyFromTextureMemory`；未实现的协议方法打一行日志并返回 0。`newTextureWithBytesNoCopy` 按同一布局建纹理并拷一次，length 为 0 时按整个布局处理。bo48 起 App 正常。
- dada `nativemetal-dada.patch:2183-2262,2304-2338`：
  - ASTC 186–200/204–218/222–236、BC、ETC2/EAC。
  - "RenderBox hands its ASTC images (CGImageTextureData, Calendar/Clock icons, g2k9) with a length of 0"。
  - "QuartzCore in backboardd asks for the App Library's full-screen ASTC 4x4 image … 282 x 609 blocks"。
  - 布局回 nil 时 SpringBoard 每次 respring 都在 `CGImageTextureDataGetOffsetWithBlock` 里 SIGBUS（`:2473-2475`）。
- AdrenoMetal 4d789e9：**只**给块压缩格式回答布局；NoCopy 建 Shared 纹理后 `replaceRegion` 一次。
- 采样本身没问题：data-a-live 在 A840+m2v 上 GPU 读回 171738 个 ASTC 块全对（`docs/GPU_PLAN.md:29`）。hakios-metal 3d26134 用 fdl6 的块布局在 A635 上采样，3720 个像素全对。

**对我们**
- 我们的布局对压缩格式和 mip>1 返回 nil，NoCopy 要求 `length>=size` 和页对齐（`NativeBridgeMetalFrontend.mm:229-240,1438-1450`），三份实现指出的漏洞我们全中。
- 现象也和 annibale bo47 相同：App 区域全黑、主屏和图标正常、上滑能回桌面。
- 区别只在于：ASTC 答"假"时 CA 走 ImageIO 软解，不经过这个布局；答"真"时才走 CompressedImage→布局→NoCopy。

**下一步**
1. 用 `pong_astc=1` 跑一轮。只 grep 日志，判据三条：backboardd 的 `serialization error`/`CompressedImage port is MACH_PORT_NULL`、内核里 memory entry 失败、我们的 NBLOG "raw texture requires supported native linear layout"。
2. 移植 dada 的 `NBLinearLayout`（和我们同为 m2v 720ef1c），只接管块压缩格式和多级 mip；非压缩格式保留现在的实现。NoCopy 对压缩格式改成"建纹理 + replaceRegion 一次"，length=0 时取布局大小。
3. 次要可疑点：我们的 m2v 0010 给 22H374 写了 `heap_blocks.astc: None`，CA 把 ASTC 放进 heap 时可能被拒（未核实）。
4. 判据先看截图：天气 App 是蓝天，多任务有画面，然后才比 125 ms 和 33 ms。
- sheng 记过一条旁证：如果 IsVirtualDevice=1，QuartzCore 有 40 处读 `CADeviceIsVirtualized`，其中包括 ASTC 路径（AdrenoMetal `NativeAdrenoMetalPlugin.mm:84-90`）。我们 ADT 的 `vmm-present` 值要顺便确认。

### b. 打开 App 的缩放动画顿挫、SpringBoard 主线程解码
**别人的做法（有数）**
- **display link 速率**：hakios 7caa457，见 §0 第 2 条。它影响的是 SpringBoard 进程内动画（翻页回弹等），开 App 的缩放是否也走这个 display link 没有核对。
- 顺带可以做：`touch_lat=1` 时打印每个 display link 和每次速率请求，调用方用 `dladdr(__builtin_return_address(0))` 换成"镜像+偏移"（hakios 同一提交）。
- hakios a48d2b2 的归因：`ax_rt=1`（降低透明度）让设置打开 49→52 fps，`ax_rm=1`（交叉淡化代替缩放）51 fps 不变；打开过程 GPU 忙 95%，每帧约 19 ms 分 7 次提交，其中 App 内容进全屏层 5.8 ms、再过两遍各 4 ms。结论是模糊和缩放都不是开 App 卡在 50 的原因。
- m2v 造块：AdrenoMetal e0997b0 记每个 64 MiB 块 30–110 ms，正好落在要它的那次纹理创建里；dada `M2V_SPARE_MB=96`："CA's render server makes and frees some 25 screen-sized textures a second"，全用 8 MiB 块时每张整屏纹理约 2 ms（g2vk19：10 s 内 238 张）。我们 g198 的停顿栈就是 `keep_room → BlockMaker::make → darwin_bo_init`。
- 管线与着色器缓存：annibale bo58–61。sheng 记录打开设置或 Safari 时 94–112 MiB 的块 wire 要 281–340 ms（gpu.md）。
- PNG/CA prepare_image 的主线程软解：**没有找到任何人有记录或数字。**

**下一步**
1. 先修 a。ASTC 走 GPU 后 ATX 软解的 5.6% 会消失；PNG 那约 13% 另算。
2. 在 SpringBoard 钩子（`userland/ui_compat18.c` 一类）里丢掉 `UIViewInProcessAnimationManager` 的 display link 请求的速率；给系统 App 的 Info.plist 加 `CADisableMinimumFrameDurationOnPhone=YES`。判据：翻页回弹段的帧间隔从 33 ms 变成 8.3 ms，gtrace p90 从 33.2 ms 下降。
3. 移植 dada 的 spare blocks（m2v 720ef1c 补丁 `metal2vulkan-720ef1c-dada.patch:884-960`），或 AdrenoMetal 0013+0021。判据：开 App 期间 `darwin_bo_init` 的样本和 >50 ms 间隔数。
4. 用 `ax_rm`/`ax_rt` 做一次 A/B，量缩放和模糊在 A730 上占多少 GPU。

### c. 壁纸（MercuryPosterExtension 默认拿不到 GPU）
**别人的做法**
- **放行**：
  - sheng e7a7a31：加速器发布 `NativeSandboxGrantProcess="*"`（`package/GpuPowerProbe.cpp:1357-1359`）。
  - 沙盒进程的 kobject 过滤不放行 `io_connect_map_memory_into_task/unmap`（MIG **2863/2864**），sheng 在 `iokit_rpc.c` 加 `native_kobj_map_grant`，在 `ipc_kobject_server_internal` 的 `mac_task_kobj_msg_evaluate` 前短路（`zzzz-sheng-ipados27-bringup.patch` L1305-1331、L1480-1505）。
  - 我们"默认拒绝"可能出在这两层中的任何一层。
- **Mercury 渲染链的缺口**（annibale `2026-10-31-mercury-wallpaper.md` §4，按出现顺序）：
  1. 扩展的 stdout 是 /dev/null，要先把输出改道才看得到错误。
  2. coordinator 建不起来（AIR 拒绝 Space 着色器）。
  3. 顶点阶段的纹理被丢弃。
  4. ASTC_6x6/4x4 和 BC1 的 KTX 被拒。
  5. 缺 `copyFromTexture:…sliceCount:levelCount:`（zeta 和 dada g2k10 也卡在这里：不补就"wallpaper stayed black"）。
  6. 缺 `copyFromBuffer:…toTexture:`、非整除网格的 `dispatchThreads:`。
  7. bicubic 采样器按 linear 处理。
  8. 海报快照存成 ATX，SpringBoard 建的 memory entry 大小为 0，commit 被拒（a 的同一条链）；annibale 改存 PNG。
  9. **4x MSAA 解析出全 0**，改单采样后才显示（bo37）。
- dada：Poster 进程设 `NB_SINGLE_SAMPLE=1`，壁纸 4x MSAA 每帧 135–195 ms GPU，单采样后 40–55 ms；再设 `NB_MIN_COMMIT_MS=100`，因为只有一条环，合成排在后面（`nm:723-737,2043-2056`）。
- dada 的 swizzle：不透明图层用 alpha=One 的视图，不做就过曝发白（`nm:1317,3243`）。XR10 只对 backboardd 答 no，对所有进程答 no 时 AegirPoster 每轮都 abort（`nm:2734`）。**我们 `:1671` 对所有进程答 0**；iPadOS 18 默认海报是否会因此 abort 还没核对。
- iOSPlus10 的进程环境：jetsam 30→128 MiB、check-in 超时、拿到 nil 设备时 2→60 s 退避。
- Droid-VM：
  - iPadOS 17 上，`*Poster*` 拿到设备后 Kaleidoscope 壁纸在锁屏和主屏实时渲染（1784bf8）。
  - iPadOS 27 还要给 PaperBoardUI 的 SBRendererService 设备，否则 NeutrinoCore 断言（8277f55）。
  - Preferences 读壁纸快照也要"有设备纹理布局的进程"。
- sheng 自己仍用**静态**壁纸：内核 addon 把 7570 号 CA 文档壁纸的 HEIC 换成 PNG（bringup patch L250-273）。PosterBoard 的数据存储要 data-protection class D（`owner-dataprotect=1`）。
- 代价：sheng 359d77c 记录海报扩展每个任务约 77 ms，环按顺序执行，慢帧多半由它造成；annibale bo38 有壁纸时滑动 52.7 vs 无壁纸 52.9 fps（扩展只在开机时提交）。

**对 A730+m2v**：m2v 本身支持 MSAA resolve。A830 上 4x 得全 0 是 freedreno 的问题，Turnip 不一定有。但 135–195 ms 的成本是 m2v+Turnip（dada gpu2）实测的，同样适用于我们。

**下一步**
1. 先移植 sheng 的两层放行，只给 `MercuryPosterExtension`（再加 PosterBoard）。
2. 在扩展里打开 stdout/os_log 改道，把错误逐轮看出来。
3. 默认就上单采样、commit 间隔 ≥100 ms、`copyFromTexture…sliceCount:levelCount:`、swizzle，并且先做 a 的布局。
4. 有了 per-task 优先级（AdrenoMetal 8f6a2eb 方法 17 / sheng 抢占）之后再常开。
5. 兜底：sheng 的静态 PNG 壁纸 addon。

### d. CA 画布布局（截图只在左侧约 30%；HUD 在 x≈2040 以右丢失；2160x1916，bpr 8704）
**已知事实**：8704 = align(2160, 64 px)×4 B，正是 fdl6 线性 pitch 的规则（hakios-metal README：`align(width*cpp, 64*cpp)`）。所以行距本身不可疑。

**别人的线索**
- 压缩：
  - IOS-26-Hot/iOSPlus10：env `CA_DISABLE_FRAMEBUFFER_COMPRESSION=1`（在 main 之前设），加上 `com.apple.backboardd.plist` 里 `CAEnableDeepFramebuffer`、`CAEnableFramebufferCompression`、`CAEnableIOSurfaceCompression` 都设 false，"CA 只读一次"，所以首启前就要写好（`root-add-metal-ios26.sh:54-70`）。
  - sheng 把同样三项写进 backboardd 和 .GlobalPreferences 的 mobile、root 两份（`make-nosetup-seed27.py:39`）。
  - AdrenoMetal 插件注释：render server 默认压缩帧缓冲，"an Apple/AGX layout this GPU does not write"。
  - **我们已在插件里设了这些**（`PongMetalPlugin.mm:170-173`），但只写了当前进程的 app 域；有没有写进持久 prefs、有没有在首启前生效，都没有核对。
- 8 B/px：sheng 0c52609 记录 IsVirtualDevice=1 时 QuartzCore 选 64-bit RGhA 帧缓冲。我们 `NativeBridgeMetalFrontend.mm:1670` 也遇到过 '&w4a' 8 B/px 的 swap。把 8 B/px 按 4 B/px 读，就会只看到部分内容或错位。
- 两块 surface 加脏区重画（annibale bo48/bo55）：CA 只在两块之间交替。如果内核在扫描完之前就把 surface 还给 CA，CA 会在正在显示的那块上按**脏区**画下一帧。这能解释"HUD 的一部分（恰好是 CA 重画的状态栏区）被盖掉"，也能解释截图里"只看到局部更新"。annibale 改为拷完才放行，并设 `CA_DISABLE_DIRTY_REGIONS=1`。
- GMEM 整块写越过行尾（AdrenoMetal 0016）：只在 GMEM 下相关，我们默认 sysmem。
- 线性 pitch：e038ba3 之前 fdl6 对显式线性布局按 16 px 对齐，3048×RGBA16F 的 IOSurface 不能原地导入。
- AdrenoMetal 的 `TB` 遥测行统计"行宽或偏移不是 64 B 整数倍"的宿主纹理（`:1382`）。

**下一步**（都是推断，没有任何人记录过同样的现象）
1. 在 backboardd 里对画布 IOSurface 打印 `IOSurfaceGetPixelFormat`、`BytesPerElement`、plane 数、压缩类型，以及 CA 实际拿到的 `framebuffer_pixel_format`。
2. 查 `kern.hv_vmm_present` 和 MG IsVirtualDevice 的值。
3. 一轮 `CA_DISABLE_DIRTY_REGIONS=1`：HUD 改为画在 flip 后的副本上，或者推迟把 surface 还给 CA；看右侧数字是否回来。
4. 把三项 CAEnable*=False 写进持久 prefs（同 sheng seed）后重启 backboardd，看截图是否变化。

### e. GPU 吞吐（滑动卡在约 60 fps、重帧约 9–10 层全屏填充、目标 120 Hz）
**别人的数（几乎全是 hakios-metal，A635/7c+ Gen3，已上机，经 ir3 编译，和 Turnip 用的是同一个编译器）**
1. **寄存器溢出与 preamble**（9a61305）：见 §0 第 3 条，6.6→2.6 ms/draw。规则：某个 stage 溢出，就关掉 preamble 再编一次，溢出更少（或溢出相同而寄存器更少）就用新结果。Turnip 也开 preamble。粗测开关是 `IR3_SHADER_DEBUG=nopreamble`，它会同时关掉 descriptor prefetch，所以只能作粗测（annibale `hakios-upstream-lessons.md` §5）。
2. **uber shader 特化**（e59090f）：`fixed_frag_lph_cpf` 每个片段都按 `FragmentShaderConfig` 的 52 字节在 15000 条指令里选路。一个 draw 覆盖 1.2 MPx 要 9.6 ms，把纹理全换成空纹理也一样慢，时间花在分支上。折叠后 16–48 条指令，每个 draw 0.2 ms。m2v 侧的同类补丁：AdrenoMetal `0004-fold-constant-global-field-loads`、dada `const_loads.rs`（46000 条→几百条 SPIR-V）。另外 hakios f693d9d 发现 iOS 16.5 的 QuartzCore pipelinelib 把 4 个 uber shader 存成 451 个特化变体，按管线标签选变体就能避开通用版。
3. **采样大纹理**（61ef867）：4 个全屏 blend draw，采样小纹理 7.4 ms，采样 1125×2436 的纹理 18.4 ms；每个 draw 多 2.7 ms，读速约 4 GB/s，而写回目标约 22 GB/s。**UBWC**（e7e7ad1）：整屏纹理采 4 遍，压缩 9.4 ms、线性 18.4 ms，像素 0 差；replace 后经压缩副本读 10.1 vs 15.8 ms。blur 的纹理布局（87a5012）：线性对比分块 15.0 vs 12.9 ms，1.16 倍。
4. **GMEM**：
   - da3b052：8 层全屏 blend，sysmem 7.6 ms、GMEM 3.1 ms。
   - 5980a94：小 pass 在 GMEM 反而慢（5 ms vs 0.13 ms），在 GMEM 里做 framebuffer fetch 读速约为读内存的 1/4。
   - 24a8cec/1dbe3cc/0c78612：按"上一帧同位置 pass 的 draw 数"决定，SpringBoard 一轮 GPU 4.27 vs 4.43 s，最长 commit→完成 46 vs 49–110 ms。
   - 6ee719f：硬件分 bin（必须在每个 tile 后发 `RM6_BIN_END_OF_DRAWS`），141–161 draw 的 pass 17–20 → 14–17 ms，一轮 4.27→3.83 s。
   - f21500d：重 pass 有两个颜色附件，GMEM 因此要 52 个 tile 而不是 24。
5. CPU 侧编码：cc08862/8c54e65，每个 draw 55–63 → 38–47 µs，host 编码 7.9→4.7 µs/draw。
- 别的项目：
  - sheng 和 AdrenoMetal（A740，8-bit BGRA）：GMEM 每 CB 17.8/56.1 ms（均值/p95），sysmem 17.1/52.1；GMEM 下还留着一帧红蓝互换，所以默认 sysmem。
  - sm-t733（A642L）按 pass 比：8 draw GMEM 5.143 vs sysmem 7.794 ms，33 draw 3.802 vs 1.816 ms，34 draw 1.622 vs 2.712 ms。
  - **调频**：annibale bo46/47 翻页 53.4/53.6、我们 g141 都显示和帧率无关；sheng 的 DCVS（`GpuPowerProbe.cpp:1755-1810`）"对帧时间的影响没量过"。
  - **LRZ/UBWC 在 m2v 下**：没有任何人有帧率 A/B。dada 的 Mesa 补丁设 `ubwc_version=0`（"no UBWC until the kernel says what the display reads"）；sheng 只写了 UBWC 4.0 寄存器。
  - 优先级/抢占：Adreno740 3f4cb94/18d79d7 的两环抢占（默认关，没有帧率数）。
  - IOS-26-Hot 只对 backboardd/SpringBoard 关掉 live `CABackdropLayer`，自报省 400–500 MB 瞬时内存。

**对 A730 的适用性**：
- 我们的重帧里，一个全屏 draw 约 0.55 ms，即 4.1 MP÷7.5 Gpx/s。这比 hakios 通用 uber 版快得多，说明我们的 CA 着色器可能已经是特化的，或者不在那条路径上。所以第 2 条要先量再改。
- 第 1 条（溢出）和第 3 条（UBWC、采样带宽）更可能直接适用：我们的 IB2「全屏 5 draw 2.78 ms」很像"每个 draw 采样一张整屏纹理"。

**下一步**
1. 插件按 boot-arg 读 `IR3_SHADER_DEBUG=nopreamble`，同法 `pong_tu_debug`，跑一轮 A/B，看重帧 IB2 的 µs。更细的做法：在 Turnip 编译 CA 管线时打印每个 stage 的 `max_reg` 和 `pvtmem_size`，列出溢出的 stage（annibale §5 建议的同一做法）。
2. 移植 AdrenoMetal 2a96ba6 的"≥3 ms pass 带最重着色器统计"，或在我们的 A4 逐 IB 时间里加着色器名和指令数，确认重 pass 是不是 `fixed_frag` 类、采了几张整屏纹理。
3. 如果是 uber 或常量分支：移植 m2v 常量折叠补丁（AdrenoMetal 0004 / dada `const_loads.rs`）。
4. 统计 m2v 建的可采样纹理有多少走了 UBWC（Turnip `tu_image` 的 ubwc_enabled）。如果多数是线性（IOSurface 导入或 linear tiling），就看能否让 CA 的离屏层（非 IOSurface）用 optimal tiling 加 UBWC。dada 关 UBWC 是因为显示读不了，我们的扫描输出只读画布，不受影响（推断）。
5. GMEM、调频对我们已证明无效（g137–g142），暂不再测，除非先上 Turnip 的 GMEM swap 修正和逐 pass 选择。

### f. GPU 内存寿命（256 MiB BO 池稳态被填满；客户端被杀时 pmap_assert_free）
**别人的做法**
- 设备级内存回答（dada `nm:2183-2195`）：`currentAllocatedSize` 回真实值，`recommendedMaxWorkingSetSize` = 2 GiB。原来回 0 时 CA 在 backboardd 的缓存涨过 1 GB，手机卡死（g2k15）。`heapTextureSizeAndAlignWithDescriptor:` 返回 0 时 CA 的 heap 大小算成 0，里面放的纹理被拒（"OutOfMemory(Heap), 300 in g2k10"，锁屏闪烁）。
- Turnip darwin BO 缓存：
  - UniXNU b624df5：进缓存 1 s 内没复用就释放（同 freedreno `fd_bo_cache_cleanup`），因为系统内存 BO 不会分配失败，否则缓存会一直占着。
  - caffd5d：订阅 `DISPATCH_SOURCE_TYPE_MEMORYPRESSURE`。
  - `TU_DARWIN_BO_CACHE=0` 关掉缓存。
  - Droid-VM `turnip-darwin-bo-cache-budget.patch`：上限默认 96 MB（`TU_DARWIN_BO_CACHE_MB`），超了就 LRU 驱逐。注释："the iPadOS 27 render server's cache grew to 434 MiB over a minute of page swipes"。只有内存压力而没有上限时，缓存从来不会被清。
  - IOS-26-Hot：m2v 纹理池限额 backboardd 192 MiB/32、SpringBoard 64 MiB/24，只给瞬时 2D 渲染目标。
- m2v 块：AdrenoMetal 0021 加 0013（块按池倍增，内存压力时归还，`:1091-1101`）；dada `M2V_BLOCK_MB` 设 backboardd/SpringBoard 16、其余 8，因为内核拒绝给沙盒进程 64 MiB，而且 64 MiB 块会把 3.5 GB 地址空间填满（g2vk20–22）。
- 内核池与 BO：
  - sheng c8806db：不再用固定池，用户 BO 是可分页的 `IOBufferMemoryDescriptor`，wire 时零填并设 WC；**客户端 view 映射 descriptor 的 VM 对象而不是物理页**。原因：1536 MiB 的池被 4–64 MiB 块填满，404 个 BO 退化到散页，96 次 96 MiB 分配失败，压力标志一直置位，缓存维护 5.6 GB（`NativeAdrenoMemory.h:69-79`）。`NativeAdrenoMemory.cpp:309-345` 统计"释放时仍被用户映射"的页（`native_adreno_free_user_mapped`），注释点名 pmap_assert_free："A view of system memory maps its VM object, which keeps the pages until the view goes"。
  - asteroids T62b：1 GiB 池满后 GPU 停住，T64 改成同样的可分页 BO。
  - 客户端关闭（`NativeAdrenoPublish.cpp:423-460`）：dropViews → `releaseAll(clock, 2 s)` 等完所有 job 并 unmap、free → `forgetVm` → `destroyVm`；任何一步失败就让对象和地址空间继续存活（"nothing is forced or reused"）。IOS-26-Hot 0143：在 `BoFree`/`free()` unmap SMMU PTE 或回收池页之前，先等完排队的 fence。
  - 1105b0e：hangcheck 500 ms，最多重试 3 次，失败就重启 GPU 并重放，只让肇事 VM 看到 device lost。
  - iOSPlus10 2156：池加锁、精确 free；2895 池最后 64 MiB 只给 backboardd；2916 收回非 backboardd 客户端缓存的 buffer。
  - Droid-VM b846440：GPU 页挂 graphics ledger、不计 footprint，避免 iconservicesagent 被 jetsam 按 15 MB 上限杀掉。
  - 几处记过 `fd_bo_heap_alloc` 不检查 NULL（annibale、iOSPlus10）：池满时会把 backboardd 带崩。

**下一步**
1. 前端加设备级的 `currentAllocatedSize`（m2v 设备总分配）和 `recommendedMaxWorkingSetSize`（例如 1–2 GiB）。这一条最便宜，可能就是池稳态被填满的原因。判据：池占用曲线，以及 CA 的缓存是否在内存压力下回落。
2. 查我们 `tu_knl_darwin.cc` 的 BO 缓存策略，加 1 s 过期或 96 MB 上限加压力回调。判据：滑动 1 分钟后的缓存字节数、`darwin_bo_init` 落散页的次数。
3. m2v spare/room keeper（同 b 的第 3 步）。
4. 长期：内核改成 sheng c8806db 式的系统内存 BO 加 VM 对象 view，从结构上替代现在的"unmap view + pmap_disconnect"；关闭顺序照 `clientClose()`。在那之前，至少在 free 前加上 sheng 式的"仍被用户映射"计数和断言。

### g. 最新路线
- **原生 Metal 驱动已在 Adreno 上让 CA 跑到约 120 fps**：hakios-metal（A635）加 HakiosMetalCA，设置滚动中位 8.5–8.8 ms（5c819b8，11-02）。依靠的是 GMEM 逐 pass 选择、硬件分 bin、uber 特化、溢出重编、管线磁盘缓存（9d52b51：每个 uber 变体编译最长约 1 s，做两遍）。它没有经过 Vulkan/m2v，是 PM4 直出；目标是 A6XX 和 iOS 16.5。其中的编译器侧做法（特化、溢出重编、磁盘缓存）可以搬到 m2v+Turnip。
- **CA 的 GLES 路线**：hakios 在 iOS 16.5 默认走 eagl，设置滚动 115 fps。我们在 iPadOS 18 上 CA 是否还有 OGL 渲染器没有核实，大概率不可用。
- **iPadOS 26/27**：
  - sheng 用 AdrenoMetal 在 iPadOS 27 24A446 上合成（RGhA 帧缓冲的处理见 d）。
  - Droid-VM 在 iPadOS 27 上要额外的 GPU 客户端（SBRendererService、Games、WebKit.GPU）。
  - IOS-26-Hot 在 23F84、A640 上自报 52.9 fps。
  - vphone-linux 在 NVIDIA 上用 m2v 跑 iOS 27 的主屏和壁纸。
  - m2v 上游 0dfdbcd、bf3ce3813、ec6b322bb 是对齐 26/27 的前提（CI 库、fat metallib、去掉 MetalLevel）。
- dada 并存两条路径（AIR→Gallium 和 m2v），annibale §3.7 的结论是"切换路线没有性能依据"。

---

## 4. 没能核实的

- hakios-metal 的数字全来自 A635/A6XX、iOS 16.5。在 A730/Turnip/iPadOS 18 上 CA 的 uber shader 是否走通用分支版，**我们没量**。
- 本文"ASTC 黑屏是布局和 NoCopy 返回 nil"是读代码推断的，没有真机日志。`info.flags&0x400` 是否就是"压缩"位，没有反汇编核实。
- `UIViewInProcessAnimationManager` 在 iPadOS 18 22H374 上是否仍请求 20..30：annibale 只确认了 iOS 26 UIKitCore 里有这个类。
- 画布"左 30%"和"x≈2040"：没有任何兄弟项目记录过同样的现象。d 里的三条（8 B/px、脏区加提前放行、压缩）都是假设。
- 以下都没有帧率数：sheng 的抢占、DCVS、着色器预热；AdrenoMetal 10-31 之后的所有提交；dada gpu2 的整体；m2v 219f013 和 0dfdbcd 在 Adreno 上的表现。
- 自报、未独立验证的：IOS-26-Hot 的 52.9/39.7 fps 和省 400–500 MB；zeta 的 55–83 ms/present。
- Droid-VM 的 BO 缓存预算补丁只在 fork 分支 `wip/droidvm` 上，提交信息里的 434 MiB 是虚拟机环境（每个 BO 都是 wired guest 内存）的读数。
- 没读的：negroni 设备页说的"independent iOSPlus10 Metal compositor results"（仓库里不存在）、friendlycatt/Bergamota、none24smu/UniXNU 的 ios26 分支、ShueiTsuki/ios-pandora。
