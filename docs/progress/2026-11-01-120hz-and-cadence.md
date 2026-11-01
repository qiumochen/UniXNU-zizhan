# 2026-11-01 凌晨：面板 120 Hz、四拍帧成因、show() 不等 latch（下一步 1/2/3）

接 `2026-11-01-zero-copy-and-return-paths.md` 第 7 节的"下一步 1–3"。顺序按"先诊断后动手"调整为 3 → 2 → 1。
所有轮次都是无人值守（`pong_touch_synth=2` 合成滑动、默认 300 s 回路），合计 g153–g160 共 8 轮，未出现挂死。

---

## 0. 一句话结论

- **面板已切到 120 Hz 并设为内核默认**（`pong_panel_hz`，默认 120）：DSI 命令 FIFO 发 DT 里 timing@1 的 timing-switch 命令（8 包、5.7 ms），
  INTF_1 帧计数从 60.5/s 变为 **120.2/s**，翻页 latch 上限从 16.6 ms 变 8.3 ms；合成滑动 moving fps 44.6 → **54–55**，单拍（8.3 ms）帧 0 → 270–350。
- **"四拍帧"的成因不是管线丢帧**：它们是 CoreAnimation 自己按 120 Hz 网格选的自适应帧率（60/40/30 Hz，GPU 每帧只有 0.3–2.5 ms 的轻动画），
  在 60 Hz 面板上 40 Hz 内容只能显示成 33/33/17 ms（抖动）。120 Hz 面板让这些档位整拍对齐。
- **show() 不等 latch（步骤 1）已实现并验证正确**（`pong_zc_async=1`：presenter 每帧 latch 等待 4.2 ms → 0.05 ms、驱动内耗时 6.5 → 2.4 ms、in-place 0），
  但两轮 moving fps 都略低于同步版（51.0/50.8 vs 54.7/53.8），**默认不开**。

---

## 1. 下一步 3：四拍帧成因（g153 诊断轮，g154 对照）

### 1.1 工具
- 内核：零拷贝帧也写 GT c 记录（`id` 第 31 位置 1）：zcFlip 进入 / fence 完成且 flip 写入 / latch 时刻（`PongAdrenoScanout.cpp` zcFlip、finish）。
- `tools/mac/gtrace.py`：识别零拷贝记录（不再误当 blit），新增 fence/latch 段与"慢帧（显示间隔 > 1.5 周期）各段中位数"；`--period=8333` 用于 120 Hz。
- `tools/mac/slow.py`（逐帧：swap 间隔、显示间隔、swap→上屏、SwapWait 应答）、`frames.py`（逐帧 GPU/flip/latch）、`sw.py`（SwapWait 应答统计）。
- 轮次脚本 `tools/mac/gdiag.sh RUN [args]`（gtrace 8 s + touch_lat + 合成滑动 + 上述分析）。

### 1.2 结果（g153，默认零拷贝，60 Hz 面板）
- 稳定滑动段（手指拖动 + 翻页回弹，GPU 5–9.5 ms）：**每个 TE 都显示一帧**，显示间隔恒为 16.5 ms。
- 66 个慢帧里 **60 个是 CA 自己晚交 swap**，0 个是 GPU 渲染超过 0.75 周期。
- 一个 2 s 合成滑动周期的形状（`frames.py g153.txt 173 262`）：
  1. 手指拖动 250 ms + 翻页回弹：重帧（GPU 5–11 ms），显示 60 Hz 连续；
  2. 回弹尾段：CA 交帧间隔 8/25 ms 交替（CA 在 120 Hz 网格上跑 60 Hz，与真实 TE 相位漂移），显示仍是每 TE 一帧；
  3. **轻动画段（GPU 0.3–2.5 ms）**：CA 交帧间隔 33/33/25/8 → 平均 25 ms = **40 Hz**（120 Hz 的 3 拍），随后 30 Hz；60 Hz 面板只能显示成 33/33/17。
- framebuffer 自己的统计印证：`0 swaps waited for the frame before`（内核队列从未卡住 CA），`~110 swaps came a refresh late themselves`/5 s。
- 根因：我们向 CA 报 120 Hz（`fb_hz` 默认 120，vsync 通知是 120 Hz 自由定时器），CA 按 120/n 选帧率；面板实际 60 Hz。

### 1.3 对照 g154（`fb_hz=60`，让 CA 以为是 60 Hz）
- moving fps 44.6 → **40.3（更差）**；CA 改用 60/30 Hz 两档（40 Hz 段变成 30 Hz），另外 fb_beat 在 60 Hz 节拍上每帧多等 4–6 ms。→ 否决，正确方向是让面板真的 120 Hz。
- 旁证：旧拷贝路径 g145 同样有 ~110 个 29–37 ms 的间隔（问题早于零拷贝），且 CA 还产出几百个 4–8 ms 的帧（多数没上屏）；零拷贝后 CA 节奏跟随面板。

---

## 2. 下一步 2：面板 120 Hz

### 2.1 只读探测（g153，`intfProbe`，dbInit 时一次 + 每 64 帧 `PONG TE` 行）
偏移按 Linux `dpu_hw_intf.c`（INTF_FRAME_COUNT 0xac、INTF_TEAR_* 0x280–0x2b4），与 sheng `DisplayTimingProbe.cpp` 的 0x284/0x2b4 一致；
INTF 块 = live.dts `qcom,sde-intf-off <0x35000 0x36000 0x37000 0x38000>`（MDSS 相对，内屏 INTF_1）。
| | INTF_1（内屏） | INTF_2（外屏） |
|---|---|---|
| tear_en / vsync_sel | 1 / 0 | 1 / 1 |
| sync_cfg | 0x1800a7：TE 脚 + 计数器使能，**vsync_count 167** | 0x18007e，126 |
| init = start_pos | 0x77c (1916) | 0x9d8 (2520) |
| thresh / height | 0x40004 / 0xfff0 | 同 |
| autorefresh | **0x80000001**（每 TE 一帧，此前是从 iOSPlus10 搬来的假设，现实测） | 同 |
| 帧率（INTF_FRAME_COUNT） | **60.5/s** | ~60/s |
- 19.2 MHz / (167 × 1916) = 60 Hz：固件按 vtotal = vdisplay 算 vsync_count（不含 porch）。INT_COUNT_VAL 的帧字段读出恒 0（只有行计数），帧率改用 INTF_FRAME_COUNT。

### 2.2 切换实现（`PongAdrenoScanout.cpp` panelHz / dsiDcs / dsiSet）
- DSI 命令通路：移植 iOSPlus10 0250 `negroni_dsi_dcs_write`（OnePlus 10 Pro 上已上机的 FIFO 模式；同为 SM8450 系 DSI 2.6 @0xae94000，
  = Linux `dsi_ctrl_hw_cmn_kickoff_fifo_command`）+ 0259 `negroni_dsi_dcs_set`（DT 包格式）。实机 `DSI0 version 0x20060000 ctrl 0x1f5`。
- 命令：live.dts 内屏 `xiaomi l18 38 0c 0a` 的 `qcom,mdss-dsi-timing-switch-command`，HS 模式。timing@0（60）与 timing@1（120）只差两处：
  `B9 11`→`B9 02`、末尾 `60 08 01`→`60 08 00`；DSI 时钟、porch、DSC 完全相同，故无需改 DSI 时钟/PPS。
- 时机：第一次 GPU 帧（klog 已就绪），前后各 `pong_klog_flush(3)`；之后 300 ms 内测 INTF 帧率打 `PONG TE after the switch`。
- INTF tearcheck 的 vsync_count **没改**（TE 脚 + init=start，传输仍从 TE 开始；0259 的推理）；`pong_panel_tc=1` 可按 19.2 MHz/(1916×120)=83 改写，未测。

### 2.3 结果
| 轮次 | 配置 | 结果 |
|---|---|---|
| g155 | `pong_panel_hz=120` | `8 packets sent, rc 0, 5712 us, MDP-busy waits 0`；`frames 117.7/s`（切换后 382 ms）→ 之后稳定 **120.1–120.2/s**；latch max 8.3 ms；moving **54.7 fps**；beats 348/209/89/156 |
| g158 | 同 g155（重复） | moving 53.8 fps；beats 268/166/83/134 |
| g160 | **不带任何显示参数**（默认 120） | 见 2.4 |
- 120 Hz 下慢帧（> 1.5 × 8.3 ms）161/174 仍是 CA 自己晚交（自适应帧率），50/174 的 GPU 渲染 > 6.2 ms（重帧在 120 Hz 下变成 GPU 受限）。
- 画面是否正常（颜色/亮度/闪烁/花屏）**还没人看过**：见第 5 节待用户验证。

### 2.4 g160（默认值验证）
- 命令行无任何显示参数（982 字节）：`timing switch to 120 Hz: 8 packets sent, rc 0, 7000 us` → `frames 115.9/s`（切换后 379 ms）→ 稳定 120.2/s；
  flips 961 taken 961、in-place 0；8 s 窗口 454 次零拷贝翻页（≈ 60/s 持续）。
- 本轮画面内容与其他轮不同（WidgetRenderer 提交 60 次，其他轮约 5 次：合成滑动停在了带动态小组件的一页），moving 70.0 fps、pause 只有 9 个，
  **不能和 g155/g158 直接比**，只作为"默认值生效、长时间 120 Hz 翻页稳定"的验证。

---

## 3. 下一步 1：show() 不同步等 latch（`pong_zc_async=1`）

### 3.1 实现
- 内核（`PongAdrenoScanout.cpp`）：framebuffer 在 `pong_scanout_frame` 前调用 `pong_scanout_set_tag(swapID)`；zcFlip 写完 CTL_FLUSH 后记
  `pong_zc_flipped_tag`，**不再等 latch**，返回 3（= 已翻页、latch 未到）；下一帧翻页前仍等上一次 latch（保持顺序，避免两个 SSPP 地址跨 latch 各半）。
  `pong_scanout_latch_poll()`：不拿 gate，先读 tag 再读 CTL_FLUSH，单调 CAS 更新 `pong_zc_latched_tag`。
- xnu 补丁 `0013-zizhan-latch-async.patch`（生成器 `tools/mac/edit_0013.py`，叠在 0012 上）：`fLatchAsync`；异步帧不在 show() 开头/结尾标 presented，
  SwapWait 每次先 poll latch；阻塞型等待每 1 ms 重查；`swapPresentedLocked` 的 retired 改为 `id - 1`（poll 可能跳号）。
- 与 iOSPlus10 0234（fZCShown/fZCNext + vsync tick 里 zeroCopyRetire）同思路，区别是用 SwapWait 时的寄存器读代替 vsync 中断/定时器。
- 内核与旧 xnu 兼容的前提是 `pong_zc_async=0`（默认）：旧 xnu 把返回值 3 当成非 GPU 帧。

### 3.2 结果
| 轮次 | 配置 | moving fps | presenter latch 等待 | 驱动内每帧 | in-place |
|---|---|---|---|---|---|
| g155 / g158 | 120 Hz 同步 | 54.7 / 53.8 | avg ~4.2 ms，max 8.3 | 6.5 ms | 0 |
| g156 / g159 | 120 Hz + async | 51.0 / 50.8 | avg 0.01–0.19 ms | 2.4 ms | 0 |
| g157 | 120 Hz + `fb_swapwait=0` | **58.5** | 同步 | — | 0（但见下） |
- async 机制正确，但 CA 产帧略少，两轮一致 → **默认关**。原因未查清：同步版在 show() 末尾（latch 之后立刻）标 presented，异步版要等 CA 来查询时才 poll 到，
  CA 可能据此把节奏放慢；也可能只是 moving fps 在 async 下按"翻页写入时刻"计时造成的统计差异（FPS SUMMARY 的时间点不同）。
- `fb_swapwait=0`（永远答"已上屏"）最快，但 CA 退回 2 张画布，可能在面板仍在取 A 时往 A 里画（cmd 面板传输期间 GPU 写入 → 单帧撕裂），
  `in-place` 计数看不到这种情况 → 不作默认，可留给用户目测对比。
- 观察：所有轮次 SwapWait 应答 **100% NotReady**（"已上屏？"在 swap 后 5–10 ms 就问，早于 latch；"已被替换？"在下一个 SwapEnd 前问，必然未替换）。
  CA 不阻塞，只是据此多建一张画布。帧总数随轮长波动大（g155 905 vs g158 750 同配置），比较用 moving fps 或每 5 s 报告的帧率。

---

## 4. 开关与默认值（新增/变更）
| 开关 | 默认 | 含义 |
|---|---|---|
| `pong_panel_hz` | **120** | 第一次 GPU 帧时发 timing-switch；60 = 保持固件的 60 Hz |
| `pong_panel_tc` | 0 | 1 = 同时改写 INTF tearcheck vsync_count（未测） |
| `pong_zc_async` | 0 | 1 = 零拷贝帧不等 latch（需 xnu 0013） |
| `fb_hz` | 120（不变） | 现在与面板一致 |
命令行：gtrace+touch_lat 的诊断轮 1000 字节，再加 17 字节到 1017（上限 1020）。

## 5. 待用户验证（攒着）
- [x] **120 Hz 画面**（v1，09:12，用户实机）：无撕裂；多任务列表、设置页切换很流畅；**桌面左右滑的第一帧顿一下，打开 App / 返回桌面的第一帧顿一下**（见第 8 节）。原文：颜色/亮度与 60 Hz 时是否一致、有无闪烁/花屏/横纹（DPU 取帧和 DSI 带宽都翻倍）；滑动和翻页手感。
  `round.sh gXXX pong_autoreset=600 pong_autoreset_sb=240`（默认即 120 Hz）；对比 `... pong_panel_hz=60`。
- [ ] 可选：`fb_swapwait=0` 时滑动中有无单帧撕裂/错帧（它 fps 最高）。
- [ ] 上一批遗留：零拷贝三缓冲的目测（`2026-11-01-zero-copy-and-return-paths.md` 第 6 节），现在与 120 Hz 一起看即可。
- 结束任一轮：按住电源键 3 秒。

## 6. 下一步
1. **vsync 通知锁相到真实 TE**：CA 的 vsync 仍是 120.0 Hz 自由定时器，面板 120.2 Hz，约 5 s 滑一个相位；用 INTF_VSYNC_TIMESTAMP（0x210–0x218，
   0259 用过）或 latch 观测时刻校准 `armVSync`。这会让 CA 的提交落在固定相位，减少"恰好错过 TE"。
2. 重帧 GPU 受限（120 Hz 下 5–11 ms > 8.3 ms）：A4 的结论仍成立（满屏多次绘制 + ROP），可看 GPU 频率策略在动画时是否拉满。
3. 弄清 async 为何让 CA 产帧变少（tl 行逐帧对比 presented 被看到的时刻）；若能消除，async 可省 presenter 每帧 4 ms CPU。
4. tearcheck vsync_count 是否需要随 120 Hz 改（`pong_panel_tc=1` 一轮，看是否有变化）。
5. 亮度控制：DCS 通路现在有了（0x51 DBV，0250 的做法），可以接 CoreBrightness。

## 7. 文件
- 内核：`device/PongAdreno/PongAdrenoScanout.cpp`（intfProbe/intfRate、panelHz/dsiDcs/dsiSet、zcFlip async、GT c 零拷贝记录）→ Mac `UniXNU-zizhan/drivers/PongAdreno/`
- xnu：`device/patches/xnu-ipados18/0013-zizhan-latch-async.patch`（`tools/mac/edit_0013.py` 生成）
- 工具：`tools/mac/gtrace.py`（零拷贝段、慢帧分解、`--period=`）、`gdiag.sh`、`g153.sh`、`slow.py`、`frames.py`、`sw.py`、`edit_0013.py`
- 日志（Mac `logs/`）：g153–g160 `.txt/.batch.out/.slow.txt`
- 参考：iOSPlus10 0250/0259（已复制到 `device/ref/iosplus10-dpu/`），Linux `dpu_hw_intf.c`，live.dts 内屏 timing 节点

---

## 8. 09:10– 用户实机（v1）与"桌面动画第一帧顿一下"

- v1（`round.sh v1 pong_autoreset=600 pong_autoreset_sb=240`，默认配置 = 零拷贝 + 120 Hz）：用户反馈**无撕裂**；多任务、设置切换流畅；
  **桌面左右滑第一帧顿、打开 App / 回桌面第一帧顿**。
- 旧数据里的同一现象（合成滑动，g155/g158/g156/g159）：动画结束 → 静止约 350 ms → 新手势的第一帧 → **再停 160–190 ms** → 8 ms 节奏。
  `frames.py g158.txt 188 200`：停顿期间 backboardd 没有任何提交，GPU 空闲（停顿两侧帧 GPU 5–12 ms），只有 WidgetRenderer 偶尔提交；
  不是 GPU、不是 latch/翻页（swap→上屏 2–16 ms 正常），是 CA 没拿到新内容 → SpringBoard / UIKit 一侧。
  同一页每 2 s 一次都会停 → 不是一次性的着色器编译。WidgetRenderer 只出现在约一半停顿里，小组件不能单独解释。
- 兄弟项目同样的现象：MaliMetal `docs/notes.zh-CN.md` §7（右滑触摸→首帧 150–200 ms，GPU 1 GHz 也不变，推测 Today View 小组件，未验证，
  建议"手势触发的 SpringBoard 用户栈采样"）；annibale `docs/analysis/2026-10-28-ios26-perf-gap.md` L23（控制中心下拉触摸→首帧 196–208 ms）；
  iOSPlus10 `docs/gpu-comparison-2026-10-27.md` #6（sheng PrimeShaders 空闲预热着色器，解决 App/控制中心首次使用的卡顿）。
- 新工具：`pong_fts.c`（0002，`touch_lat=1`）打 `tl: t TYPE ID at US`（按下/第一次移动/抬起，与 tl: s/v 同一微秒时钟）；
  `tools/mac/gesture.py LOG [--frames]` 每个手势一行：触摸→第一个 swap / 上屏、首帧后 500 ms 内最大间隔。
- v2 = 默认 + `touch_lat=1`（不开 gtrace：只会再证明 GPU 空闲），用户手动做 左右滑 / 开关 App / 多任务（对照组）。
- 下一步（视 v2 结果）：手势触发的线程采样（0005 sampler 改成"按下后 300 ms、按时间片记录 SpringBoard 主线程 + backboardd 状态"），定位停顿里 SpringBoard 在跑还是在等谁。

### 8.1 v2 结果（09:22–09:25，用户手动，power-key 结束，klog 1.7 MB 完整）
用户口述：每次都顿；回桌面那下"感觉是 App 开始缩小时，不确定"。`gesture.py v2.txt --frames` / `gesture_frames.py v2.txt`：
- **桌面翻页 8/8 都停 158–216 ms，位置在抬手处**：按住期间有帧（但稀，25–80 ms 一帧），最后一帧在抬手前 0–30 ms，下一帧在抬手后 130–230 ms
  （按下后 301–376 ms）。即"拖动 → 翻页回弹动画"交接的那一帧。其中 2 次在回弹结束处（按下后约 590 ms）**又停约 200 ms**。
- **点开 App**：首帧之后停 275–349 ms（113.9 s、119.8 s 两次），其余点按 50–125 ms。
- **上滑回桌面**：388→679 ms 停 291 ms（122.5 s）、415→556 停 141 ms（127.8 s）、125.4 s 按下后 538 ms 才有第一帧。
- 多任务（长按上滑 320–391 ms）：最大间隔 25–92 ms，和用户"流畅"一致。
- 停顿时长集中（翻页 160–215 ms）且每次都有 → 像 SpringBoard 主线程在滚动代理回调（结束拖动 / 结束减速）里同步做一件固定成本的事
  （CPU 活或同步 IPC 等待），这期间 UIScrollView 的动画（主线程驱动）出不了帧。整轮 moving 61.4 fps，平时节奏没问题。
- 下一步：手势触发的 SpringBoard 主线程采样（抬手后 0–400 ms，1 ms 一次，记 运行/排队/等待 + 等待点 + 用户态 PC/栈），合成滑动可复现（抬手即停），无需用户。

### 8.2 手势触发采样器 → 根因（g161–g164，无人值守合成滑动）
- 工具：xnu `0005` 追加 **手势采样器** `pong_gesture_sample`（`osfmk/kern/thread.c`），`pong_fts.c` 每次按下触发；boot-arg（短名，命令行已近 1023 上限）：
  `gs=<ms>`（采样时长，≤1000）、`gs_proc=<进程>`（默认 SpringBoard）、`gs_all=1`（该进程所有运行/排队线程，跨手势累计，每 4 个手势打印一次）、
  `gs_from=<ms>`（all 模式只记按下后 ≥ 该时刻）、`gs_n`（手势数，默认 24）。每 1 ms：主线程状态（运行核 / 排队 / 等待 + block hint + 内核等待点）、
  用户态栈（保存的 pc/lr + 帧指针链，`vm_map_read_user`，同 kperf 的做法）、8 个核各自在跑哪个进程；第 0 个手势打印 dyld 镜像表（非共享缓存的库）和共享缓存 slide。
  分析 `tools/mac/gsample.py LOG [--all] [--top N] [--each]`：共享缓存用 `ipsw dyld a2s`（Mac 上 `brew install blacktop/tap/ipsw`，缓存从 IPSW 抽到
  `apple.noindex/dsc/22H374__iPad7,11_12/`），插件库用 `atos` + `work/gpu/plugin/PongAdrenoMetal.bundle`。坑：`VM_MAX_ADDRESS` 在这个配置是 0xF0000000（低于用户栈），要用 `MACH_VM_MAX_ADDRESS_RAW`。
- g161/g162（SpringBoard 主线程）：停顿里 74 % 在 `mach_msg` 收消息，但栈是 `CFRunLoopServiceMachPort ← GSEventRunModal ← UIApplication _run`
  = **空闲等事件**，SpringBoard 已把动画交出去了；停顿期间 backboardd 占核 1.07（平时 0.84）。
- g163/g164（`gs_proc=backboardd gs_all=1 gs_from=100 gs=330`）：停顿窗口里 `com.apple.coreanimation.display.primary` 线程：
  - 75 %：`CA::OGL::MetalContext::add_memoryless_textures` → `-[NBDeviceMethods newTextureWithDescriptor:]` → m2v `Device::new_texture` →
    `memory::keep_room` → `BlockMaker::make` → `vkMapMemory` → Turnip `darwin_bo_map` → `IOConnectMapMemory64`
  - 18 %：同一路径的 `vkAllocateMemory` → `darwin_bo_init`（内核分配 64 MiB：池满后散页 + 清零 + clean + 逐页改写合并属性）
  - 其余：提交（`darwin_queue_submit`）、render-server 线程。
- **根因**：m2v 每个池只保留 1 个空块（`Pool::release`），`keep_room` 要求每个池留半块（32 MiB）空间。CA 的新 surface 一放进备用块就超过一半 →
  `keep_room` 再造一个 64 MiB 块（内核分配 + 16384 页逐页非缓存映射进 backboardd，约 150–200 ms）；动画结束两个块都空 → 按规则放掉一个；
  下一次动画重来。兄弟项目：UniXNU AdrenoMetal `bridge/patches/0013-growing-memory-blocks.patch`（首块 4 MiB 倍增，依赖更新的 m2v）、
  MaliMetal / iOSPlus10 `BLOCK_BYTES` 8 MiB（"each block cost 70 ms to make"）。我们不改小块：16.5 MB 的整屏纹理超过半块会变成每次单独分配 + 映射。
- **修复（g165）**：`patches/metal2vulkan/0021-zizhan-keep-two-empty-blocks.patch`（`KEPT_EMPTY_BLOCKS = 2`，内存压力下仍由 `trim_empty_blocks` 回收），
  `build-pong-metal-bridge.py` 的补丁表里按 marker `KEPT_EMPTY_BLOCKS` 打。部署 `metal-refresh.sh g165`（备份 TAG g165）。

### 8.3 修复结果（g165 采样轮 / g166 无采样轮，合成滑动）
| | g164（修复前） | g165（修复后，同采样参数） | g166（修复后，无采样） |
|---|---|---|---|
| 首帧后 500 ms 内有 > 50 ms 间隔的手势 | **14/14** | **1/14**（开机后第一次，块在建） | 4/18（1 次热身；3 次是统计口径：把手势前最后一个空闲帧当成"首帧"，真正首帧在 75–91 ms 后） |
| 500 ms 内最大间隔中位数 | **275 ms** | **25 ms** | **25 ms** |
| backboardd display 线程在停顿窗口的运行样本 | 2424 | 501 | — |
- g166 moving 54.9 fps（同类合成滑动轮 g155/g158 是 54.7/53.8；g160 的 70 fps 停在动态小组件页，不可比）。滑动中节奏 8/17/25 ms。
- `SBRendererService` 每轮崩一次，g155 起就有，与本修复无关（待查）。
- 合成滑动的"按下 → 第一帧"中位数约 120 ms（v2 真手指数据里首帧更早但拖动中帧较稀，25–80 ms 一帧）：下一个可查的点。

## 9. 待用户验证（11-01 上午这一批）
轮次：`round.sh v3 pong_autoreset=600 pong_autoreset_sb=240 touch_lat=1 gs=500 gs_all=1 gs_proc=backboardd`（带采样，有残留卡顿时数据同轮可查）。
- [x] 桌面左右滑：抬手那一下的顿是否消失（开机后第一次滑可能还顿一下：块在建）。**v3 用户：不顿了，只有第一次卡一下。**
- [x] 点开 App / 上滑回桌面：第一帧的顿是否消失或减轻。**v3 用户：还是顿，看到缩放动画时卡住**（见第 10 节）。
- [x] 拖动跟手程度。**v3 用户：跟手。**
- [ ] 顺便：颜色/亮度/闪烁（120 Hz 下）有无异常。
- 结束：按住电源键 3 秒。

## 10. App 打开 / 回桌面的顿（v3 实机 + g167–g172 合成）
- v3 数据（`gdelta.py v3.txt 19 23`、`gcores.py`）：开 App 停顿里 SpringBoard ~0.85 核在算、backboardd 少；回桌面那几次 backboardd ~1 核（仍有 keep_room 新块，
  这次以 `darwin_bo_init` 为主：内核 256 MiB 连续池已满 → 散页 + 逐页改写合并属性）、splashboardd（快照）0.6 核。
- 新工具：
  - 合成"开 App / 回桌面"：`pong_touch_synth=<s> tsa=XXYY`（`pong_fts.c`，0002）：在 XX%,YY% 点一下（默认用 `tsa=5096` = Dock 中间），等 <s> 秒，
    从底边上滑到 55% 回桌面，再等。复现了：每次点开都停 200–300 ms。
  - 内核 BO 统计：`PONG GPU BO:` 每 10 s 一行（`PongAdrenoPlatform.cpp` dcvs 报告里，方法 2/3/4/13/14 = BoAlloc/BoMap/BoFree/BoCpuMap/BoCpuUnmap 的次数、平均、最大 us，
    `NativeAdrenoUserClient.cpp` Tally 新增 `native_adreno_method_max_us`；池用量、池内/回落/散页 BO 数）。g167：池 256/256 MiB 满，BoAlloc 最大 33–39 ms、BoFree 最大 36–38 ms，
    但每 10 s 累计只有几十 ms，解释不了每次点开 250 ms。
  - 采样器加精确寄存器：运行中的线程用 IPI（`cpu_xcall` 到它所在的核，读 `cpu_int_state`，同 kperf）取当下的 pc/lr/fp，标 `P`；`gs_sum=1`（跨手势累计、只记
    `gs_from` 之后、最后打印一次，避免整轮日志冲掉 ramoops）；表 400 项；`gsample.py --profile` 按函数出 self / total。
- g168–g170（SpringBoard 主线程，合成点开）：点开停顿里主线程 **75 % 在跑**；精确样本：图片解码约 30 %（CA `prepare_image` → PNGReadPlugin 15 %、
  ATXReadPlugin **ASTC 软解** 5 %、预乘/通道置换 9 %），其余是 UIKit/SpringBoard 的转场搭建（objc_msgSend、layout、`dispatch_once`、SBFluidSwitcher modifier）。
- **CPU 没有触摸升频**：`drivers/ArmCpu/PongCpuPolicy.h`（data-a-live 的策略经 pong 移植，32 ms 一窗、每窗最多翻倍、4 窗才降）——手势开始时核在低频，
  前 64–96 ms 的活在低频跑。参考 data-a-live `xnu/patches/0097-fuxi-touch-boost.patch`（fuxi_perf：输入时把 gold/prime 提到高档，带 TSENS 温控和预算）。
  实现：触摸计数（`pong_touch_activity`）一变，所有域立即拉到策略自己的上限档（不用 boost 行），保持到最后一次触摸后 `pong_cpu_boost_ms`（默认 400，0 = 关）；
  轮询 32 → 8 ms（负载策略仍 32 ms 一次）；每 30 s 一行 `Pong CPU: touch boosts N, X ms boosted`。
- A/B（合成点开，无采样）：
| | g172 boost 关 | **g171 boost 开（默认）** |
|---|---|---|
| 点开 App：500 ms 内最大间隔中位数 | 258 ms | **191 ms** |
| 上滑回桌面：同上 | 100 ms | **58 ms** |
| moving fps | 47.1 | **51.5** |
- 剩余（下一步）：点开仍停 ~190 ms，主线程在满频下仍算 ~150 ms。最可疑的是**图片在 CPU 上解码**：ASTC 快照/图标本该直接作为压缩纹理交给 GPU（插件已答 Apple1–3，
  Apple2 含 ASTC），CA 仍走 ImageIO 软解 → 要查 CA 怎么决定（render server 的能力 / 像素格式答复）。其次 PNG 解码、首次 `dispatch_once`。
  回桌面时 backboardd 的 keep_room 新块：内核池满后的散页 BO 很贵，可考虑加大 `pong_gpu_pool_mib`（在 build-ipados18-prelinked-boot.py 的默认里，boot-arg 先到先得，不能在命令行覆盖）。

## 11. 待用户验证（11-01 中午这一批）
- [ ] v4（默认 = 零拷贝 + 120 Hz + m2v 0021 + CPU 触摸升频）：点开 App / 回桌面的顿是否减轻；桌面滑动、拖动是否仍正常；手机是否明显发热（升频只在触摸时，最长到抬手后 400 ms）。
  `round.sh v4 pong_autoreset=600 pong_autoreset_sb=240 touch_lat=1`

## 12. 用户不在时的自主一批（11-01 下午）
### 12.1 CPU 实测、L3 投票（g173/g174）
- 疑点：升频后 SpringBoard 主线程仍算 ~150 ms，是不是核没到频率，或共享缓存 / 内存慢？
- 参考 UniXNU-sheng `package/CpuFreqProbe.cpp`（SM8550）：EPSS 的 0x20 是"域实际运行频率"（整簇空闲时也会掉），L3 在 0x17d90000
  （mainline osm-l3.c "qcom,epss-l3"，LUT 0x100，投票 0x90）。我们从来没投过 L3。
- `PongCpuPolicy.h` 新增：
  - `pong_cpu_probe=1`：启动时依次绑到 cpu0/4/7，用依赖加法链测时钟、指针追逐测 32K/256K/3M/32M 延迟，先按引导时的 L3 测一遍，再投到最高档测一遍。
  - `pong_l3=0/1/2`：0 不动；1（默认）投最高档；2 跟随最快的域。
  - 30 s 报告加上"boost 期间各域实际频率"和 L3 平均投票。
- 实测（g173/g174）：

| | A510 cpu0 | A710 cpu4 | X2 cpu7 |
|---|---|---|---|
| 时钟（依赖加法） | 1804 MHz | 2649–2745 MHz（比我们请求的 2496 还高：其余核的投票行仍是引导值） | 2994 MHz |
| 3 MB 延迟 | 22–23 ns | 6.1–6.5 ns | 5.5–7.1 ns |
| 32 MB 延迟 | 134–137 ns | 120 ns | 93–95 ns |

  - 引导程序已把 L3 投在 13 档（1593 MHz，最高 1804）：**L3 和 CPU 频率都不是瓶颈**，主线程就是在满频下算了这么多活。
  - boost 期间域实际频率平均 1.78/2.62/2.96 GHz。
- A/B（合成点开）：

| | g173 `pong_l3=2` 跟随 | g174 L3 最高档 | g171 引导值（上一批） |
|---|---|---|---|
| 点开 App，500 ms 内最大间隔中位数 | 183 ms | 175 ms | 191 ms |
| 回桌面 | 116 ms | 58 ms | 58 ms |
| moving fps | 47.5 | 54.3 | 51.5 |

  跟随模式空闲时把 L3 压到引导值以下，变差 → 默认改为 1（最高档，比引导值只高一档）。

### 12.2 SBRendererService 每轮崩一次：已查明，已修
- 抛错栈（g170 slide 符号化）：`CI::MetalMainProgram::compile` → `CIMetalComputePipelineStateCreateFromSource` → `-[_MTLDevice newLibraryWithSource:]`
  → `MTLLibraryBuilder` → `_MTLGetMTLCompilerLLVMVersionForDevice` → `[NativeBridgeMetalDevice featureProfile]`（没有这个方法）→ NSInvalidArgumentException。
- 前因：CoreImage 先加载 `ci_stdlib.metallib` / `ci_filters.metallib`，它们是 file_type 1（Core Image 库）；m2v 只收 0（可执行）和 2（动态），
  所以拒收（`mtlb.rs`）。CI 于是改为运行时编译 Metal 源码，而 m2v 本来就不编 MSL（REFUSALS `LibrarySource`）。
- 修：`NativeBridgeMetalFrontend.mm` 覆盖 `newLibraryWithSource:options:error:` 和带 completionHandler 的版本，返回 nil + NSError（和其他拒收的库一样），
  不再落到基类去问 Apple 硬件档案。插件 g175（`metal-refresh.sh g175`）。
- 结果：g176/g177 里 SBRendererService 不再退出（只剩 CI 自己的 "Failed to load 'ci_stdlib'" 两行，进程一直在跑）。CI 的 GPU 滤镜在这台机器上仍不可用
  （m2v 不编 MSL），用到 CI 的效果会缺失，但不再崩。

### 12.3 开 App 时 CPU 解码图片：CA 判断 ASTC 的开关
- QuartzCore 22H374：`CADeviceSupportsASTC` = `MGGetBoolAnswer("ji56BO1mUeT7Qg9RO7Er9w")`，即 MobileGestalt `DeviceSupportsASTC`
  （混淆键 = base64(md5("MGCopyAnswer"+key)) 的前 22 位）。为真时 CA 会把 ASTC 图（`CGImageTextureData`）建成 `CA::Render::CompressedImage`，
  直接作为压缩纹理交给 GPU；为假时由 ImageIO ATXReadPlugin 在 CPU 上软解（g168–g170 的 profile 里有约 5 %）。
- m2v 支持 ASTC 像素格式（`pixel_format_info.rs`）；Adreno 730 / Turnip 原生支持 ASTC LDR。
- 插件 g175 每个进程报一次 `MobileGestalt DeviceSupportsASTC <0|1> in <进程>`，下一轮看答案再决定。

### 12.4 X2 优先（0014）
- 采样分核（`gsample.py` 现在把 cpu7 单列为 (X2)）：g170 开 App 停顿里 SpringBoard 在 A710（cpu4–6）上 0.73 核，在 X2 上只有 0.09 核。
  Edge 把 P 簇四个核当成一样的：`choose_processor()` 先取上次的核，否则按顺序取下一个空闲核。
- `patches/xnu-ipados18/0014-zizhan-fast-core.patch`（sched_prim.c `choose_processor`）：非实时、`sched_pri >= pong_fast_pri` 的线程
  （默认 BASEPRI_FOREGROUND 47，0 = 关）如果 `pong_fast_cpu`（默认 7）空闲就先放到那里。
- A/B（合成点开，同一插件 g175、L3 最高档）：

| | g176 X2 优先（默认） | g177 `pong_fast_pri=0` |
|---|---|---|
| 点开 App 中位数 | 158 ms | 166 ms |
| 回桌面 | 50 ms | 50 ms |
| moving fps | 51.6 | 51.2 |

  差别在轮间波动（±10 %）以内；留着默认开，等采样轮（g178）看主线程是否真的到了 X2。
- 答案（g176，插件 g175）：**所有进程 `DeviceSupportsASTC 0`**（ADT 身份是 iPhone17,1，MG 不给真）。

### 12.5 让 CA 用 GPU 的 ASTC（ui-refresh g178）
- QuartzCore 里读这个开关的三处：`CA::Render::Texture::Texture()`（为假时给每个纹理打 0x80000 标记）、`update_texture_flags`、
  `CompressedImage::new_image_with_texture_data`。render server（backboardd）也用同一套 Render 代码，所以 SpringBoard 和 backboardd 都要答真。
- `userland/ui_compat18.c`：已有的 MGGetBoolAnswer 钩子（原来只给 iPhone17,1 的 Dynamic Island 用，只装在 SpringBoard / Preferences）
  现在也装进 backboardd；`DeviceSupportsASTC`（及其混淆键）答真，boot-arg `pong_astc=0` 还原原生答案；Dynamic Island 的答案仍只在 UI 进程里给。
- 新部署脚本 `src.noindex/ui-refresh.sh TAG`（镜像 `tools/mac/ui-refresh.sh`）：按 `prepared/ui-compile-command.json` 重编 libpong-ui18.dylib，
  换进 prepared OS 镜像，重算 UFS 布局，只写手机 OS 窗口（和 metal-refresh 同路）。备份 TAG g178。
- A/B（合成点开，同一内核和插件）：

| | g179 ASTC 真 | g180 `pong_astc=0` |
|---|---|---|
| moving fps | **88.6** | 46.9 |
| 回桌面：500 ms 内最大间隔中位数 | **25 ms**（0/10 > 50 ms） | 133 ms（10/10） |
| 点开 App | **141 ms** | 158 ms |
| 回桌面"按下 → 首帧" | 80 ms | 19 ms（ASTC 关时屏幕一直在出帧，首帧来得早；开了以后静止时不出帧，首帧要等手势识别） |

  g178（同配置 + 采样）：88.9 fps；SpringBoard 在 X2 上的份额 0.09 → 0.33 核（0014 生效）。点开的剩余工作里 PNG 解码（CA prepare_image）仍约 13 %，
  ATX 软解还有 5.6 %（调用者在 14 帧栈深之外，看不到是谁）。
- **待确认显示正确**：Apple 的 ASTC 数据是 twiddled 块序，若 CA 把它原样交给 m2v 当线性数据，图标/快照会花。→ 12.6 的截图。

### 12.6 内核截图（pong_shot）
- `PongAdrenoScanout.cpp` `shotCapture`：`pong_shot=<s>`（开机后秒）起每 `pong_shot_ms` 抓一张、共 `pong_shot_n` 张：零拷贝帧 flip 后，
  非缓存映射读每 16 行的每 16 个像素（135 × 120），线程打 `PONG SHOT` 行；`tools/mac/shot.py LOG` 出 PNG（3 倍放大）。
- g183/g184 截图：主屏图标（FaceTime、备忘录、小组件）清晰，没有块状花屏 → **ASTC 走 GPU 显示正确**。模糊过渡也正常。
- **合成"点开 App"一直点错了地方**：截图里 Dock 在左下（x 2–30 %），`tsa=5096`（x 50 %、y 96 %）点到的是搜索入口 → 每次打开的是
  **Spotlight**（模糊主屏），不是 App；g171–g180 的"点开"数字测的都是 Spotlight 打开。`tsa=0747`（备忘录图标）没有启动任何 App；
  `tsa=1118`（左上蓝色天气小组件）在 g187 启动了 com.apple.weather → 触摸坐标与截图坐标一致，截图可信。
- 截图只在零拷贝帧上抓：App 在前台时 CA 的画布不在池槽里，走拷贝路径 → 已在 finish() 的拷贝帧 flip 处也抓。
- g187（ASTC 真，点天气小组件开 App）：点开 500 ms 内最大间隔中位数 **33 ms**（10 次里 1 次 > 50 ms），moving 93.8 fps。
- **真·开 App 的 A/B**（点天气小组件 `tsa=1118`，同一内核 / 插件 / UI 库）：

| | g188 `pong_astc=0` | g189 ASTC 真（默认） |
|---|---|---|
| 点开 App：500 ms 内最大间隔中位数 | 125 ms（10/10 > 50 ms） | **33 ms**（0/10） |
| 上滑回桌面 | 141 ms（10/10） | **17 ms**（0/10） |
| moving fps | 71.1 | **93.4** |

- 截图映射：每次新建映射（单帧或整池，不缓存 / 写合并 / 回写）在有 App 启动过之后都被拒（g187–g191）；改为显示初始化时（`pong_shot` 设了才做）
  一次性不缓存映射整个 96 MiB 池并保留（g192 成功）。注意：g192 的手势数字明显变差（点开 100 ms、回桌面 83 ms），疑似这个内核不缓存别名映射的副作用 →
  **pong_shot 只做诊断，测性能的轮不要开**。
- g192 截图：点小组件 → 小组件按下变暗 → 主屏模糊、小组件缩进 App → 天气 App 区域是黑的（模糊主屏在后）。全程没有花块。

### 12.7 现状与下一步
- 默认：插件 g175（Metal 源码库拒收）、UI 库 g178（DeviceSupportsASTC 真）、内核（L3 最高档、X2 优先 0014、截图代码默认关）。
- 待用户（v4，第 13 节）。
- 未做：采样器栈深 14 → 28（看 ATX 软解的调用者；ASTC 开后剩余量已很小）；SpringBoard 主线程 PNG 解码（CA prepare_image，约 13 %）。

## 13. 待用户验证（11-01 下午这一批）
`round.sh v4 pong_autoreset=600 pong_autoreset_sb=240 touch_lat=1`（全部默认）
- [ ] 点开 App、上滑回桌面：卡顿是否明显减轻（合成测试 125 → 33 ms、141 → 17 ms）。
- [ ] 图标、App 启动时的快照、小组件：有没有花屏、色块、错位、发黑（ASTC 改走 GPU 后第一次给人眼看）。
- [ ] 桌面左右滑、拖动跟手：是否仍正常。
- [ ] 用一阵后手机是否明显发热（L3 最高档 + 触摸升频 + X2 优先）。
- [ ] 顺便：主屏是不是只在左边一列（左上蓝色小组件、下面 4 个图标、左下 Dock），右边大片黑？（截图看到的样子，用来确认截图工具）
- 结束：按住电源键 3 秒。

## 14. v4 结果与更正（11-01 15:30）
- 用户 v4：桌面左右滑很流畅、不卡；**但所有 App 都打不开**：点开 → 桌面模糊 → 顿一下 → 黑屏；上滑能回桌面；多任务也是黑的。
- 截图对照（点天气小组件，App 打开后 63–66.5 s）：g193 `pong_astc=0` 天气的蓝天正常；g194 ASTC 真 → App 区域全黑（`g193/g194.tiles.png`）。
  回头看 g192 的"天气区域是黑的"其实就是这个问题，当时误判为 App 本身黑。
- **更正**：12.5 的 A/B（33 vs 125 ms、93 vs 71 fps）无效：ASTC 真时 App 根本没画，"不卡"是因为没东西画。DeviceSupportsASTC 让 backboardd / SpringBoard
  走了另一条路径（推测是 App 的内容 / 快照面换成我们画不出的格式），具体机制未查。
- 处理：`ui_compat18.c` 改为**默认原生答案**；`pong_astc=1` = 原先行为（SpringBoard + backboardd），`pong_astc=2` = 只在 SpringBoard（试验，未测）。
  `ui-refresh.sh g195` 已部署；g196（默认）截图确认天气画面正常、回桌面缩小动画正常。
- 教训：凡是"变快很多"的改动，先用截图确认 App 内容真的画出来了，再看数字。
- 仍然有效的：SBRendererService 不崩、L3 最高档、X2 优先、触摸升频、m2v 0021、截图工具。

## 15. 待用户验证（v5）
`round.sh v5 pong_autoreset=600 pong_autoreset_sb=240 touch_lat=1`
- [ ] App 能正常打开、多任务有画面（回到 ASTC 之前的状态）。
- [ ] 桌面左右滑是否仍然流畅（v4 的流畅里有多少来自 ASTC，这轮能看出来）。
- [ ] 打开 / 返回 App 的顿和 v3 比如何。

## 16. v5 结果、帧率 HUD（暂停）、换方向（11-01 16:20）
- 用户 v5：上滑退出不卡、左右滑不卡、App 正常打开、多任务正常；**打开 App 能看到图标放大然后顿一下；退出 App 好像没动画**。
- 合成（点天气小组件 tsa=1118，ASTC 原生）：轮间波动很大（点开 42–183 ms）。g197 采样：点开的停顿里 SpringBoard 主线程多在等（问题在别处）；
  退出的停顿 50–75 ms 里 SpringBoard 主线程在跑。g198 backboardd 显示线程：GPU 提交 IOConnect、madvise（大块 free）、
  keep_room → BlockMaker::make → darwin_bo_init（内核 256 MiB 池满后散页分配）。**未修**，候选：加大 `pong_gpu_pool_mib`、查 madvise 的来源。
- 帧率 HUD（`fps_hud=1`，`PongAdrenoScanout.cpp` hudDraw/hudPaint/hudTick，默认关）：每帧 flip 前在右上角画白字黑底（5×7 字模 ×6）。
  `pong_shot_hud=1` 全分辨率截右上角：数字框在预期位置（黑框嵌在 App 的蓝底里，第一位 "1" 字形和位置都对），但 x ≈ 2040 以右既没有 App 内容也没有我写的数字
  （g200/g201 `tiles.png`）→ 画布右上这一块的布局还没弄懂（与"主屏只占左边"的截图现象可能同源）。**暂停**，未交给用户。
- 用户建议：帧率先记下，转向 WiFi / 蓝牙 / 传感器 / 电池等日用功能。现状：profile 里 `pong_wifi=0 pong_audio=0 pong_adsp=0`；
  UniXNU-zizhan 有 `drivers/PongWifi`、`drivers/PongPower`；UniXNU-sheng（SM8550）有 `ShengBattery/ShengBattmgr`（ADSP battmgr 经 glink）、
  `WifiNet/WifiProbe`（PCIe，sheng 已记录 WiFi 连上并测速）。
