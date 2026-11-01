# 兄弟项目的帧率工作调研（2026-10-31，一手资料）

来源：会话 scratchpad `research/` 下的克隆（akshaynexus/iOSPlus10、Chengdong-Yu/UniXNU-pong、iola1999/hackios-redmi-k90-annibale、Ken-u/MaliMetal、MC-XiaoXiao/ios_sagit、ShueiTsuki/data-a-live、steelbrain/metal2vulkan、UniXNU/Adreno740、UniXNU/AdrenoMetal（含 `origin/feat/ipados27-gpu-parity`）、UniXNU/UniXNU、UniXNU/UniXNU-sheng、YinCQL/UniXNU-picasso），2026-10-31 中午刷新。只记文档/提交/代码里有数的。

## 各项目的帧率时间线

**UniXNU-sheng + AdrenoMetal（SM8550/A740，最接近我们）**
- sheng 19960fa：UI 线程不上 A510，重滑动中位帧间隔 21→16 ms（40→55 fps）。8a2dd99（144 Hz，后在 f7bb0fd 改回 120）、36ec562、d4c299b、a338c71（GPU 调频）无帧率数；`porting.toml:239` 明说调频器"对帧时间的影响没量过"。
- AdrenoMetal main：461a842 "让 Turnip 按 pass 自选 tile/sysmem"——i553 强制 sysmem 时 GPU 忙 19%，i555 **70 fps p50** 跑 600 s 无停顿；c6d29ca 扫描输出线程 QOS_CLASS_USER_INTERACTIVE，>40 ms 帧间隔的 p90 降约 5 ms；31867dc 设备应答缓存（"QuartzCore 每秒问几千次……每次约 140 µs"）；`PrimeShaders.h:118-123` 滑动中做着色器预热让中位帧间隔 19→30 ms，所以 >8 CB/250 ms 时暂停预热。
- AdrenoMetal `feat/ipados27-gpu-parity`（不在 main）：1353601→6c7b0a0→0dd1516 backboardd 专用编码线程，"主屏拖动多出 10% 帧（**约 61→67 fps**），display 线程 34% 而不是 90%"；只在 render server 开（GFXBench 反而少 12% 帧）；f1f51bf 闭包代替 block（6% 编码线程时间在释放 block）；c363547 mimalloc；0f11e64 surface 导入保留 1 s（"CA 每帧给每个窗口 surface 建纹理……每帧分配、映射、释放"）；7824091 `METAL2VULKAN_QUEUE_PRIORITY=high`；11420f4 帧缓冲保持 8-bit BGRA。后面这些没有帧率数。

**data-a-live（小米、Adreno 840、120 Hz）——唯一上 100 fps 的 Adreno+m2v 移植**（`PLAN.md`）
- L504：发布 `IOMFBDisplayRefresh`（displayMinRefreshInterval 2^32/120）→ 动画 60→102–104 fps，"没满 120 是 GPU 慢（600 帧里 480–570 帧要等 GPU）"，swap→上屏平均 11 ms（ce355b3）。
- L521：热限频（GPU 95 °C → 500–726 MHz），"比较翻译层优化看 backboardd 的 CPU 样本数，不看帧率"。
- L545/L550：m2v 补丁 0001–0008（每次绘制分配 24.6→13.5、保留 draw commands/lists、拒绝重复绑定、KeyMap、双平面 shadow 不清零）101→106 fps；padded-target Alias（0003/0009，`pandora_m2v_padded`）111→**113 fps**，GPU 忙 75→62%，copy-back 0（也见 `docs/EXPERIENCE.md:71`）。
- 6265e2d：Turnip Darwin fence 等待每次只要 1 ms（backboardd 每秒 420 次短轮询超时）→ 一次最多 100 ms（UniXNU cd1b975 同修法，**我们已有**）。32c087f：IOMFB SwapWait gated + present 线程；d308bd5：present 线程由帧起始中断唤醒。
- `HANDOFF.md:25` 剩余："卡顿。GPU 调频跟不上（多数时间停在 222 MHz），温控把 GPU 限在 726 MHz；后台 coreduetd 占一个核的 75%"。6d80b66："到 120 的差距在 GPU 时间，不是温度"。

**MaliMetal（RK3588 G610、PanVK、60 Hz）——最细的分帧**（`docs/notes.zh-CN.md`）
- L175：8–10 fps → 壁纸 YCbCr 每个 encoder 转一次（73–77 ms）→ `0100-ycbcr-skip-unchanged-conversion` → 18–31 帧/5 s。
- L219 表：37.9（tl1）→ 44.7 FPK → 47.5 A76 优先编号 → **50.3** `cpumask=0x1f`；`0101-ycbcr-device-linear-filter` 壁纸 pass 22.5→7.9 ms。
- L325：编码线程优先级 50（`mali-epri`）+ `sched-slowcpus` → commit→complete p50/p90 13.7/17.3 ms，>19.3 ms 占 3.5%；GPU 1 GHz → 12.7/15.6、1.6%；52.6–54.0 fps，最好的手势 60.0。模型（§2）：render server 在 vsync 后约 14 ms 提交，在飞深度≈2，commit→complete ≤19.3 ms 才赶 V+2。剩余：SpringBoard 侧触摸→首帧延迟（§7）、流式提交（§8）。

**annibale（K90，自写 Metal shim over freedreno，非 m2v）**：控制中心下拉 4.6→15.2 fps（每 CB 一次提交）→45.0（直接常量绑定）→59.2；`docs/analysis/2026-10-29-frame-budget.md` §3.4：16.64 ms 里驱动拷贝 1.76（10.6%）、上一帧完成→swap 7.64（45.9%：fence 2.56 + AIR flush 1.3 + CA 侧约 3.7 未归因）、GPU 0.05、等节拍 7.23（43.4%）；`fb_hz=120` "收益 0"。`tl:` 行在 `MiniIOMobileFramebuffer.cpp:838/1017`、`RedwoodGPU.cpp:292`，`touch_lat=1`。

**iOSPlus10（A730，同 GPU）**：`docs/gpu-comparison-2026-10-27.md`："GPU 12–26% 忙而 77% 帧晚到，说明帧时间多半不是 GPU shader 时间：是 backboardd 的 CPU 编码/翻译、串行等待和拷贝"；固定 818 MHz vs DCVS 57.7 vs 43.5 presents/s（`patches/xnu/README-gpu-kernel.md` §18）；轻滚动最好 96–101 presents/s。其优化清单（shadow copies、sheng m2v 0009–0016、扫描输出优先级）均未量。

**pong**：约 10 fps（`migration/ipados18/upstream-20261027.md`），无量。**sagit**：原生驱动，合成 CA 动画 30→60。**picasso**：无 GPU。**metal2vulkan 上游**：44d97aa（调用处直接录制、去掉延迟重放线程）M3 上 60 Hz 拖动 commit→handler p50 4.185→3.671 ms，每 draw 编码 +8–16%；ca8340b 合批提交"帧率在噪声内"；d485925 提交路径移出调度锁（RX 7900 编码 5.27→4.79 ms/帧）。

## 分帧工具
- AdrenoMetal 439fb1b / gpu-parity 7183e58、de38c6f、809075b：`frames.trace`（逐进程帧迹、waitUntilCompleted 调用者、Turnip pass/submit 环、空隙 CPU 采样、半拷贝扫描输出 `gHalfRows`）。
- sheng：`scripts/analyze-sheng-frame-timing.py`（K 行 commit→completion、W 行 swap gap/copy-done）、`package/BootLog.c` `[fence wait]`/`[scanout wait]`/`[swap timeline]`、`TouchProbe.cpp:711-744` `sheng-swipe=`。
- MaliMetal：`mali-timeline=1` → `tl` 行 `rep.sub.sch.ca.cb`；`frame_timeline.py`、`frame_phases.py`、`cb_table.py --latency`。
- data-a-live：`pandora_prof=backboardd` + `tools/prof.py`；`pandora-metal B/W` 10 s 桥接计时；`pandora_autoswipe`。
- 没有谁在 Adreno 上把一帧拆到 CA 提交 / m2v 编码 / GPU / 扫描输出——我们的 A3（`pong_gtrace` + `touch_lat=1`，`tools/mac/gtrace.py`）补这一块。

## 候选优化（按 收益 × 对 A730+m2v 的适用性）
1. backboardd 编码线程：gpu-parity 1353601/6c7b0a0/0dd1516/f1f51bf（`NativeBridgeMetalFrontend.mm` EncodeBatch/EncodeThread，开关 `/private/var/ngpu/debug/[no-]encode-thread`），+10%。MaliMetal 数据说明它得待在大核上（d808606，"只开它也没有用"）。我们前端基于 bcbbc70，29 块里 7 块不直接套，要手工移。
2. padded-target Alias + 绘制路径补丁：data-a-live m2v 0003/0009，AdrenoMetal `bridge/patches/0008..0016`（ed654b4、01ea556、2d35c57）。我们已有 0009-pong-owned-iosurface-row-padding（要 env `PONG_IOSURFACE_PADDING=1`，没开）和 data-a-live 0012–0019；缺 AdrenoMetal 0016（在我们的 texture.rs 不直接套，主屏 2160×1916 正好对齐 16×4，暂不需要）。
3. Turnip fence 等待 100 ms：data-a-live 6265e2d / UniXNU cd1b975 —— **已有**。
4. Turnip autotune GMEM/sysmem、`TU_DEBUG=nocb`：461a842 —— 我们强制 `sysmem,nocb`，**g137 起 A/B**。
5. surface 导入保留 1 s（0f11e64）、内核保留映射时复用导入（d67af6a，我们的扫描输出已有 token 复用）。
6. 扫描输出线程 UI 优先级 c6d29ca —— **已有**。
7. 设备应答缓存 31867dc —— **g137 已移植**。
8. 动画期间暂停着色器预热（`PrimeShaders.h` waitForQuiet）—— 我们没开预热。
9. 队列优先级 high 7824091（未量）。
10. 8-bit BGRA 帧缓冲 11420f4（iPadOS 26+ 才有）。
11. 内核侧我们已有：128 IB/提交 c232716、不每次 TLBIALL 5c8c145、非一致导入 b686b90、handle index 25abaa4。
没找到：有数的三缓冲/`kCAContext` 选项（iOSPlus10 去掉 `CA_FORCE_TRIPLE_BUFFERING`：QuartzCore 里没这个串）；按帧率量过的管线缓存预热（只有编译时间 ms：MaliMetal L116 2340→149 ms）。

## 不重建 OS 镜像的用户态迭代
- UniXNU GPU addon：`scripts/make-gpu-addon.py` → `NGPUADD2` blob；`unixnu build payload --gpu-addon FILE` → `EFI/UniXNU/Payloads/kernel.macho.gpuaddon`；DarwinBoot 填 `/chosen/memory-map/GpuAddon`；`patches/xnu/zzy-native-gpu-addon-mount.patch` L206-322：boot-arg `sheng-gpu-addon=1`，写变化文件到 `/private/var/ngpu/addons/...` 再只读 `MNT_UNION` bindfs 到 `/System/Library/Extensions` 和 `LaunchDaemons`。CDHash 要在信任缓存里（`build-adreno-metal-plugin.py` → `trust.json`）。MaliMetal `metal-plugin/make-addon.py` 同格式。
- `XNU_NATIVE_OFFLINE_GPU` 与此无关（QEMU VirtIO-console GPU broker，`build-xnu-4k-baseline.sh:329-337`）。
- iOSPlus10：`scripts/metal/phone-test.sh push` 用 SSH scp 插件（先 unlink，b23fa3a）再重启 backboardd；开关在 `/var/db/negronimetal/{clients,off,env}`。
- annibale `tools/k90auto/respin.py`（940da2e）：`PROCESS:NAME=VALUE` 写进 sysctl `kern.redwood.env`，SIGKILL 进程由 launchd 重启；只能 env。
- data-a-live：`tools/gpu_test.sh --metal-from DIR --write-pack` 把插件打进 `PANDFW01` 固件包，原始写进 pinned userdata 文件；运行时开关都是 boot-arg（`pandora_m2v_padded`、`pandora_m2v_rebinds`）。
- 我们：`tools/mac/metal-refresh.sh`（refresh_metal + 只重写 OS 窗口，约 9 min）+ 插件读 boot-arg（`pong_tu_debug`），一次镜像多轮 A/B。
