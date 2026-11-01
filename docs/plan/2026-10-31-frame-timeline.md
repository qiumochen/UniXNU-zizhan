# 帧率：逐帧时间线 → 用户态提速（2026-10-31）

起点（2026-10-31 02:00，g128–g134）：主屏滑动 53–56 fps（屏 120 Hz），帧间隔按 CA 的 8.33 ms 节拍量化、一帧跨 1–4 拍；已排除：内核扫描输出（约 3 ms）、大小核（E/P 已做）、显示驱动节拍/队列深度、GPU 频率（固定 900 MHz 不更好）、壁纸扩展抢 GPU（已拒绝）。各进程 CPU 都很低（SpringBoard 主线程 13%、backboardd CA display 11%）、GPU 忙约 20%。缺的是**一帧端到端在哪等**。

## 调研（一手资料，克隆在会话 scratchpad `research/`）

同类项目怎么过 50 fps、怎么拆帧：

- **data-a-live**（小米、Adreno 840、120 Hz，唯一上 100 fps 的 Adreno+metal2vulkan 移植）`PLAN.md` L504/L545/L550：发布 `IOMFBDisplayRefresh` 后 60→102 fps，"没满 120 是 GPU 慢（600 帧里 480–570 帧要等 GPU）"；m2v 补丁 0001–0008（每次绘制更少分配等）101→106；padded-target alias 111→**113 fps**，GPU 忙 75→62%。6265e2d：Turnip Darwin 后端 fence 等待每次只向内核要 1 ms（backboardd 每秒 420 次超时轮询）→ 改成一次最多 100 ms（**我们的 Turnip 已带 UniXNU cd1b975 同一修法，排除**）。
- **AdrenoMetal**（sheng SM8550/A740）main：461a842 "让 Turnip 自选 tile/sysmem"——i553 强制 sysmem 时 GPU 忙 19%，之后 **70 fps p50**（前提 bridge 0016：tile store 不越行）；31867dc 设备应答缓存（QuartzCore 每秒问几千次、每次 140 µs）；c6d29ca 扫描输出线程 UI 优先级（我们已有）；`feat/ipados27-gpu-parity` 分支 1353601/6c7b0a0/0dd1516/f1f51bf：backboardd 专用编码线程 **61→67 fps**，display 线程占用 90%→34%；0f11e64 surface 导入保留 1 s。
- **MaliMetal**（RK3588）`docs/notes.zh-CN.md` §2/§325：模型是"render server 在 vsync 后约 14 ms 提交；在飞深度≈2；commit→complete ≤19.3 ms 才赶上 V+2"；编码线程优先级 + 避开慢核把 commit→complete p50 从 13.7 降到 12.7 ms。
- **annibale**（K90，自写 Metal shim）`docs/analysis/2026-10-29-frame-budget.md` §3.4：16.64 ms 里驱动拷贝 1.76、上一帧完成→swap 7.64（fence 2.56 + AIR flush 1.3 + CA 侧 3.7 未归因）、等节拍 7.23；`tl:` 逐帧行（我们的 MiniIOMobileFramebuffer 同源，`touch_lat=1`）。
- **iOSPlus10**（A730，同 GPU）`docs/gpu-comparison-2026-10-27.md`："GPU 12–26% 忙而 77% 帧晚到，说明帧时间不是 shader 时间，是 backboardd 里的 CPU 编码/翻译、串行等待和拷贝"。
- 时间线工具：AdrenoMetal 439fb1b `frames.trace`、sheng `scripts/analyze-sheng-frame-timing.py`（K 行 commit→completion、W 行 swap）、MaliMetal `mali-timeline=1`、data-a-live `pandora_prof`。
- 快速迭代用户态：UniXNU `scripts/make-gpu-addon.py` + `zzy-native-gpu-addon-mount.patch`（启动载荷带插件、只读 union 挂到 Extensions）；我们现有路径是 `build-ipados18.py refresh_metal` 写 OS 镜像 + `install_pinned.py --only OS`（`tools/mac/metal-refresh.sh`）。

## 方案

### A3 GPU 提交时间线（内核，已做：g136）
内核记录每次提交（fence、进程、IB 数、是否切上下文、µs）和 CP 时间戳推进（200 µs 轮询），扫描输出拷贝的 offer/pickup/done；与驱动 `touch_lat=1` 的 `tl: s/v` 同一时钟。`pong_gtrace=<s>`，SpringBoard 后 20 s 起（与合成滑动同步）。分析 `tools/mac/gtrace.py`。实现：`submit/NativeAdrenoSubmit.cpp`、`NativeAdrenoRing.h::peekFence`、`PongAdrenoScanout.cpp`、`ArmDTPlatform/PongBootReset.h`。

### D1 让 Turnip 自选 GMEM（用户态，AdrenoMetal 461a842）
我们强制 `TU_DEBUG=sysmem,nocb`。改为 boot-arg `pong_tu_debug=<flags>` 可覆盖（默认不变），A/B：`nocb` vs `sysmem,nocb`。0016（tile store 越行检查）暂不需要：主屏 surface 2160×1916，16×4 块正好对齐，且 `PONG_IOSURFACE_PADDING` 没开（padded alias 路径本来就关着）。

### D2 设备应答缓存（AdrenoMetal 31867dc）
`NativeBridgeMetalFrontend.mm::askAnswer` 按 (selector, 参数) 记住应答。

### D3 候选（按 A3 结果定）
编码线程（gpu-parity 1353601…，我们的前端基于 AdrenoMetal bcbbc70，差 341 行，可移）；surface 导入保留 1 s（0f11e64）；m2v 44d97aa/ca8340b/d485925 升级；扫描输出 worker 失败重试而不是永久放弃。

## 顺序
A3（g136）→ D1+D2 一起进镜像（g137 起 A/B，用 boot-arg 切）→ 看时间线再定 D3。
