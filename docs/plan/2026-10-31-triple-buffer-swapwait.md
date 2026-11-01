# 零拷贝之后：三缓冲、SwapWait 语义、面板 120 Hz（研究备忘，2026-10-31 深夜）

只读研究，没有改任何代码。引用约定：`0001:N` = `UniXNU-zizhan/patches/xnu-ipados18/0001-pong-darwin24.patch` 第 N 行（Mac，
MiniIOMobileFramebuffer.cpp 从 5059 起、MiniIOSurface.cpp 从 6671 起）；`Scanout.cpp:N` = `UniXNU-zizhan/drivers/PongAdreno/PongAdrenoScanout.cpp`；
`0011:N` = `0011-zizhan-panel-pool-canvases.patch`；`0234:N`/`0269:N` = `device/ref/iosplus10-dpu/` 下的两份 negroni 补丁；`display.md:N` =
`device/ref/iosplus10-dpu/2026-10-26-display.md`；`sheng …` = `device/ref/UniXNU-sheng/package/`；`live.dts:N` = `raw/collect/live.dts`；
日志在 Mac `工作区 logs/`。前提见 `docs/progress/2026-10-31-frame-timeline.md` §10 和 `docs/plan/2026-10-31-zero-copy-step3.md`。

## 0. 结论（先看这里）

1. **CA 用几张画布不是我们报的任何属性决定的，而是 CA 按"哪张还被占用"动态决定的。** 我们的内核让 CA 看到的是"显示从不占用任何 surface、
   SwapWait 永远立刻成功"，所以 CA 只需 2 张（自己的 17 + 向 framebuffer 要来的默认表面 18）。没有 `IOMFBSwapQueueDepth`/`TripleBuffering` 一类旋钮：
   0001 一共只发布 `TimingElements`/`ColorElements`（0001:5418-5419）、`IOMFBDisplayRefresh`（0001:5550）、`PongSoftwareRenderer`（0001:5532）和
   DT 节点上的 `display-scale`（0001:5506）；`fb_queue`/`SWAP_QUEUE` 只是 SwapEnd 的内核闸门（0001:5212-5218、5949-5957），CA 看不见。
2. **让 CA 建第三张的办法 = 让它看见占用。** 两条渠道：(a) SwapWait 的非阻塞查询在该 swap 未上屏/未退役时返回 NotReady/Busy（iOSPlus10 0254 这么做后，
   同一 CA 保持 3 张 surface：display.md:31）；(b) 显示持有期间给 IOSurface 的 useCount 置位（真 IOMFB 的做法；我们的内核 hold 只加 `holders`，
   不碰共享页 useCount：0001:7517-7533 vs 8710-8732）。推荐先做 (a)，判据是日志里出现第三条 `canvas slot given … (3 taken so far)`。
3. **SwapWait(id, flags, 0)：flags bit31 = 非阻塞查询，低位 = 等待类型**（sheng OfflineLegacySwap.mm:60-70）。CA 每帧两问：`0x80000001`（N 上屏了吗）在
   SwapEnd N 后 2–7 ms，`0x80000000`（N 退役/被超越了吗）紧贴 SwapEnd N+1 之前（g138.txt:8017-8021）。我们恒答"是"（0001:6503-6509）。
4. **正确实现后 show() 可以不等 latch**，前提是内核同时 hold "在屏 + 已 flush 未 latch" 两张（0234 的 fZCShown/fZCNext），而 CA 对 NotReady 的反应
   只能靠上机看：要么它等（吞吐同现在、但等待在 CA 线程），要么它建第三张（吞吐上去）。分两轮做（§2.4）。
5. **面板现在是 60 Hz**：内核对 CA 报 120（0001:5162、5204；g152t.txt:123），但 latch 周期 16.7 ms（g152t.txt:7586-7590）。DT 里内板 timing@0 = 60 Hz、
   @1/@3/@4 = 120 Hz（live.dts:25090、25147、25261、25318），固件 splash 停在 timing@0。切 120 要走 DSI 发 timing-switch DCS + 重设 INTF tearcheck，
   第一步先只读 INTF_1 的 tearcheck 寄存器确认 60 Hz（§3.3）。

## 1. CA 的画布数量由什么决定

### 1.1 我们向 CA 暴露了什么（全部核过，没有缓冲数相关项）
- 选择子：3/4/5/6/7/8/12/20/22/25/28/29/33/52/57/72/73/74/77/79/81/82（0001:5121-5143）。`SupportedFrameInfo` 答 0 个（0001:6531-6539），
  `GetBlock` 只认 0x40 当前模式（0001:6541-6554），`ChangeFrameInfo`/`SwapSignal`/`SwapCancel*` 直接成功（0001:6510-6515）。没有任何"队列深度"类选择子。
- `GetLayerDefaultSurface`（选择子 3）返回内核自建的 `MiniIOSurfaceCreateSimple(fWidth,fHeight,'BGRA')`（0001:6475-6485、5582-5591）——这就是 swap 伙伴
  surface 18（bpr 8640，g152t.txt:2924-2925 `kernel_task` 拿槽）。CA 把它当作第二张画布与自己的 17（bpr 8704）轮换（g152t.txt:2926-5266，swap 1..8）。
- 0001 自己的注释记录了另一种历史状态："CoreAnimation makes itself a new one for every frame"（0001:5289-5299，软件渲染时期）——说明数量是动态的，
  不是 CA 写死的 2。

### 1.2 CA 判断"这张能不能再画"的两个渠道，我们都答"随便画"
- **IOSurface useCount**：用户态 `IOSurfaceIsInUse` 读共享页 `minisurf_shared.useCount`（0001:8993-9001，偏移 0x18）。它只被用户态 trap 0/1 改
  （`useCountCommon`，0001:8788-8800、8710-8732）。内核在 SwapEnd 对 surface 做的 `MiniIOSurfaceCopy`/`Drop` 只动生命周期计数 `holders`
  （0001:5964、5994、7517-7533），**从不写 useCount** → CA 眼里显示永远不占用任何 surface。
- **SwapWait**：`case kFBMethodSwapWait` 只打 `tl: w` 日志就 `return kIOReturnSuccess`（0001:6503-6509）。
- 实际的互斥只靠内核闸门：SwapEnd 在 `fSwapsWaiting >= swapQueue()`（默认 2）时阻塞（0001:5949-5957），show() 在**开始**时就放行下一个
  （0001:6045-6049），零拷贝帧则在 `zcFlip` 里等 fence、翻页、等 latch（≤20 ms）才返回（Scanout.cpp:708-737）。所以 CA 的节拍被锁在 latch 周期上，
  而且 CA 画进 17 时 17 可能仍在屏上——`zcInPlace` 只统计"到达 zcFlip 时目标槽 == 在屏槽"（Scanout.cpp:713），g152t 为 0 是因为 show 串行等了 latch，
  并不证明 GPU 没有在 DPU 取帧期间写过在屏画布（cmd 面板 GRAM 自持，一次取完就不再读，所以肉眼难见）。

### 1.3 参考移植怎么处理
- iOSPlus10（iOS 16，SM8450）：0254 把 SwapWait 改成"上屏返回 0，否则**立刻** kIOReturnBusy"（0269:519-521 的注释；0234 早先是阻塞等最多 50 ms，
  0234:626-648，忽略 flags，只看 `scalarInput[0]`：0234:667-669）。效果："CoreAnimation keeps 3 surfaces (notes.md:451); SwapWait gates reuse"
  （display.md:31）。0234 的生命周期：swap 时 hold（0234:237-240），`fZCShown/fZCNext` 各持一张（0234:125-126），vsync tick 里 `zeroCopyRetire`
  看 CTL_FLUSH 清零才放前一张（0234:401-431）。
- sheng（S9）：没有内核实现，只有用户态探针用 `IOMobileFramebufferSwapWaitWithTimeout(conn,id,flags,double)`，先 `0x80000001` 查、再 `1` 阻塞等
  （OfflineLegacySwap.mm:67-70；OriginalRenderBoxPresentation.mm:50、62-64），并写明了语义（§2.1）。
- UniXNU-pong 的 0001 与我们相同（pong 0001:6503 同样恒成功）。

### 1.4 结论与旋钮
- 没有现成的"缓冲数"属性。**2 张是 CA 在"永不占用 + SwapWait 永远成功"下的最小配置**；要第三张，必须让 SwapWait（或 useCount）如实报告占用。
- 槽容量：池 96 MiB / 17 MiB = 5 槽，dbuf 自用 `min(pong_dpu_bufs=3, 3)` 张，剩 2 槽给画布（Scanout.cpp:443-452；g152t.txt:119 "3 buffers, 2 canvas slots"）。
  `pong_zc=2` 下 presenter 不再拷贝，**`pong_dpu_bufs=2` 即可腾出第三槽**（最少 2：Scanout.cpp:412）。0011 放行条件是 backboardd/kernel_task 的
  面板尺寸 BGRA（0011:44-49），第三张 CA 画布（bpr 8704）会自然进槽。

## 2. SwapWait 语义与正确实现

### 2.1 flags 的含义（三处证据一致）
- 调用形态：3 个 scalar `(id, flags, 0)`（0001:6504-6506 打印 in[0..2]）；用户态 API 带 double 超时（sheng ORB.mm:50）。
- sheng 的逆向注释（OfflineLegacySwap.mm:60-66）："Type 1 waits for removal from pending/current. Type 0 also waits until the last completed
  generation is superseded… The original high bit requests a nonblocking query. A pending control must return real NotReady; only a completed
  query proceeds to blocking wait." 即：**bit31 = 非阻塞查询**；类型 1 = 该 swap 已不在 pending/current（已被显示处理完、上屏），类型 0 = 更进一步，
  等它被后一代超越（退役，其 surface 空闲）。
- 0234 的理解（0234:626-630）："CoreAnimation passes the swap that showed its other framebuffer before it draws into this one again (ios-s9 0096)"
  —— 与"类型 0 = 退役"一致：CA 再画 A 之前，问显示 B 的那个 swap 是否已经把 A 替换下来。
- 我们的 trace（g138.txt，copy 路径，859 个 swap）：启动 `w 0 0x80000003`（:1287），首帧 `w 1 0x1`（:2598，全程唯一一次阻塞型调用）；之后每帧
  `w N 0x80000001` 933 次（偶有重复查询，:7995/:7997）、`w N 0x80000000` 858 次；典型顺序 `s 499`@285203 → `w 499 0x80000001`@291794 →
  `w 499 0x80000000`@293417 → `s 500`@293929（:8017-8021）。即 CA 在**交出 N+1 之前**确认 N 已呈现并退役，然后放心复用 N-1 的画布。

### 2.2 正确实现的样子（在 MiniIOMobileFramebuffer + Scanout 之间）
- 状态：`fEndedID`（SwapEnd 最大 id）、`fPresentedID`（flip 已 latch 的最大 id）、`fRetiredID`（= 被后一帧 latch 替换下来的最大 id，恒 ≤ fPresentedID）。
  zcFlip 在 `dbLatched()` 为真时更新 presented 并把前一张标 retired（Scanout.cpp:726-729 处）；拷贝路径在 presenter 拷完时同时置 presented=retired=id。
- `SwapWait(id, flags)`：`done = id==0 || id>fEndedID || 掉电 || (type==1 ? fPresentedID>=id : fRetiredID>=id)`（0234:639-641 的例外沿用）。
  `flags&0x80000000`：done → Success，否则 **kIOReturnNotReady**（sheng 的说法；0254 用 kIOReturnBusy，日志里两者都试一遍）。
  非 bit31：`IOLockSleepDeadline` 到 done 或 50 ms 上限（0234:67 `NEGRONI_ZC_WAIT_MS`），超时返回 kIOReturnTimeout。
- 日志：`tl: w` 行加上返回值；统计 NotReady 次数与 CA 随后的行为（是再问、还是直接 SwapEnd）。

### 2.3 这能让 show() 不等 latch 吗
- 能，但要两件事同时成立：(1) 内核像 0234 一样**同时 hold 在屏那张和已 flush 未 latch 那张**（0234:125-126、401-431），`MiniIOSurfaceDrop` 延后到
  retire；presenter 写完 `dbFlip` 就返回，retire 放到下一次 zcFlip 开头或 vsync tick（Scanout.cpp:319 `dbLatched`）。(2) CA 尊重 NotReady——
  要么等到 Success 再画（等待从 show() 挪到 CA 线程，帧率同现在），要么另建一张（帧率上去）。若 CA 无视返回值直接画，`zcInPlace`>0 且肉眼撕裂，
  那就回到 show() 等 latch。
- 顺带的收益：SwapEnd 闸门 `fb_queue` 不再是节拍器，CA 的 120 Hz 自时钟（0001:5303-5313 注释）能跑满 GPU 允许的帧率。

### 2.4 建议的上机顺序（每轮一个变量）
1. **只改 SwapWait 返回值**（保留 show() 等 latch，保留 2 槽）：看 CA 是否把 `0x80000000` 的 NotReady 变成再查询/延后 SwapEnd，以及是否出现
   `MiniIOSurface: … no panel slot` 的第三张画布请求（0011:55-58 的 miss 日志）。
2. **加 `pong_dpu_bufs=2`**（3 槽）：第三张画布进槽，`canvas slot given … (3 taken so far)`；对比 `PONG FPS SUMMARY` 的 beats 分布是否离开 2×8.33 ms。
3. **去掉 show() 的 latch 等待**，改为双 hold + 延后 retire；判据 `in-place 0`、`taken == flips`、moving fps。
4. 若 CA 完全不理 SwapWait 的返回值，再试 useCount 路线：显示持有期间 `shared()->useCount` 的类别 0 计数 +1（0001:8730 的写法），退役时 −1。

## 3. 面板刷新率：现状 60 Hz，120 Hz 的代价

### 3.1 现状证据
- 内核对 CA 报 120 Hz：`kFBRefreshHz 120`、`fb_hz` 默认 120（0001:5162、5204），`IOMFBDisplayRefresh` 按此计算（0001:5541-5550），日志
  "Pong fixed refresh 120 Hz"（g152t.txt:123）。Scanout 的节拍也按 fb_hz=120 算 8.33 ms（Scanout.cpp:771）。
- 实际 latch 周期 16.7 ms：g152t `latch wait avg 13405 max 16603 us`、`flips 2642 taken 2641`，beats `0/1953/0/518/0/30/22`——全部是 2× 或 4× 8.33 ms
  （g152t.txt:7586-7590）。g152f/g152h 的 latch 等待上限同样 15–16.6 ms（progress §9）。→ DPU/面板按 60 Hz 取帧。
- 还没读过的东西：探测只读 SSPP/CTL/SMMU/时钟（Scanout.cpp:126-130；g149.txt:117-140），**没有读 INTF tearcheck**；"AUTOREFRESH_CONFIG 0x80000001，
  每个 TE 一帧"是从 iOSPlus10 搬来的假设（Scanout.cpp:253；display.md:145）。

### 3.2 DT 里的内板模式
- 活动面板：bootargs `msm_drm.dsi_display0=qcom,mdss_dsi_l18_38_0c_0a_prim_dsc_cmd`（live.dts:16），节点 `xiaomi l18 38 0c 0a prim cmd mode dsc dsi panel`
  （live.dts:24989、25050）：cmd 模式 + TE 脚（25022-25027）、2160×1916（25088-25089）、`qcom,mdss-dsi-panel-mode-switch`（25019）、单 DSI 控制器
  （`dsi-ctrl-num <0>`，25042）、拓扑 2 LM/2 DSC/1 INTF（25056）。
- 五个 timing：@0 60 Hz（25090）、@1 120（25147）、@2 60（25204）、@3 120（25261）、@4 120（25318）；同为 2160×1916、同 porch（v 0x20/0x40/0x20）、
  同 DSI 时钟 0x47868c00（25075/25132）。没有 `qcom,dsi-supported-dfps-list`/qsync（对比参考面板 live.dts:21044-21048 有）——
  **换率 = 发 `timing-switch-command` DCS（HS 模式，25067/25124）**，120 Hz 的那组是 `F0 5A 5A; 60 08 00; B0 03 F2; F2 70; B9 02; BD …`（25125）。
- 固件 continuous splash 用的是第一个 timing（@0 = 60 Hz），与 16.7 ms 一致；120 Hz 需要我们自己切。

### 3.3 要做到 120 Hz 需要什么、先跑哪一步
- 需要：(a) DSI 控制器的命令发送路径（iOSPlus10 0250 做过：FIFO/DMA 命令、CMD_DMA_DONE，display.md:33），对 l18 发 timing@1 的 switch 命令
  （`dsi_panel_switch` 的做法，display.md:34-35）；(b) 重编 INTF_1 tearcheck 的 `vsync_count = 19.2 MHz/(vtotal×fps)`（display.md:28），
  vtotal = 1916+0x20+0x40+0x20 = 2076 → 60 Hz 154、120 Hz 77；autorefresh 保持 1 帧/TE；(c) 可能要重发 PPS（display.md:37，同分辨率同 slice 时可省）。
  工程量 ≈ iOSPlus10 的 0250+0259 两个补丁，是第 4 步以后的事。
- **第一个探测（只读，安全）**：在 `dpuProbe` 里加 INTF_1（MDSS+0x36000，`qcom,sde-intf-off`，live.dts:20408-20410；块长 0x2c4）的 tearcheck 读：
  `TEAR_CHECK_EN`、`SYNC_CONFIG_VSYNC`（低 19 位 = vsync_count）、`SYNC_CONFIG_HEIGHT`、`VSYNC_INIT_VAL`、`START_POS`、`INT_COUNT_VAL`（frame<<16|line）、
  `AUTOREFRESH_CONFIG`（Linux `dpu_hw_intf.c` 的 0x280–0x2b4 偏移，上机前对照 live.dts 的 sde 版本核一遍）。相隔 1 s 读两次 `INT_COUNT_VAL` 的 frame
  字段 = TE/s（期望 60）；`vsync_count` 期望 154。有了这两个数，§3.1 的"60 Hz"从推断变实测，也验证 `AUTOREFRESH 0x80000001` 的假设。
- 在面板仍是 60 Hz 期间，把 `fb_hz=60` 报给 CA 是否更好，值得顺手 A/B（CA 的自时钟会从 120 改 60，少做一半无效帧；0001:5383 注释说明该参数直接
  进 `TimingElements` 和 `IOMFBDisplayRefresh`）。

## 4. 风险与未知
- CA 对 NotReady 的具体反应（等 / 建新 / 无视）没有一手资料，只有 iOSPlus10 的"变 3 张"结论（display.md:31，iOS 16）；iPadOS 18 的 backboardd
  行为需上机确认。`ios-s9 0096` 原补丁不在本机也不在 Mac（只在 0234 注释中被引用）。
- 返回码选择：NotReady（sheng）vs Busy（0254）——实现时做成 boot-arg 可切。
- 第三槽来自 `pong_dpu_bufs=2`，意味着拷贝路径退化为双缓冲；`pong_zc=2` 下无影响，但 `pong_zc=0` 的回退配置要分开写进 boot-args。
- INTF 寄存器偏移是 Linux 上游 dpu 的，downstream sde 在 8475 上一致的概率高但未核；探测只读，即使偏移错也只是读到无意义值。
