# GPU：ASTC 布局与 fairplayd 回填（2026-11-02）

时间均为 UTC+8。接 [兄弟项目 GPU 调研](../research/2026-11-02-sibling-gpu-update.md)，补记 08:49 后至 09:39 会话中断前的工作。依据最后会话交接记录和保留下来的源码、恢复脚本；本次整理没有重新上机，也没有直接复核 Mac 本地 g239–g245 原始日志。

## 1. 六项任务的当前状态

| 顺序 | 任务 | 状态 |
| --- | --- | --- |
| 1 | 原生 ASTC 导致所有 App 黑屏 | WIP：布局代码已部署试跑，**正确绘制未验证**；Weather 卡住的根因未定 |
| 2 | 翻页 / 回弹动画约 30 fps | WIP：SpringBoard display-link 拦截已写，**未构建、未上机验证**，中断时未同步到 Mac |
| 3 | ir3 `IR3_SHADER_DEBUG=nopreamble` A/B | 未开始、未验证；A635 的 6.6→2.6 ms/draw 是兄弟项目结果，不是 A730 的结果 |
| 4 | GPU 内存池常满、设备分配量回答、BO 缓存和预制块 | 未开始；已有 keep-two-empty-blocks 补丁不等于这一项完成 |
| 5 | 动态壁纸 / Poster GPU 放行 | 未开始；仍默认拒绝 MercuryPosterEx，后续需限速、单采样和隔离对照 |
| 6 | 内核截图仅左约 30%、HUD 在 x≈2040 后不可见 | 未开始；布局、格式、压缩和脏区均只是待查假设 |

## 2. ASTC WIP 与轮次

`drivers/PongMetal/NativeBridgeMetalFrontend.mm` 新增 `NBBlockShape` / `NBBlockTextureLayout`，参照 dada / hakios-arm64e-dada gpu2 的布局，处理压缩块、mip 和 array/cube slice，接入 `newTextureLayoutWithDescriptor:` 和 `newTextureWithBytesNoCopy:`。后者把布局中的数据一次复制到普通纹理，并保留调用者的释放回调。它不是已经验证成功的零拷贝 ASTC 路径。

g239–g244 使用已部署的 Metal plugin；调查参数组为 `pong_touch_synth=3 tsa=1118 touch_lat=1 pong_shot=63 pong_shot_ms=700`，ASTC 开启轮另带 `pong_astc=1`，均须保留 autoreset / panic / 电源键复位路径。具体完整命令行和镜像版本应从本地轮次日志补齐，不能用这段参数替代完整记录。

| 轮次 | 条件 / 操作 | 已记录的现象与限制 |
| --- | --- | --- |
| g196 | 11-01 回退到系统原生 ASTC 回答的旧对照 | 截图确认 App 有内容；交接引用总帧数 3531，不是严格同镜像 A/B |
| g239 | 新布局 + `pong_astc=1`，点 Weather | ASTC 回答生效，无 texture-refused 错误；仅 392 帧，约 uptime 58 s 后不再 swap，Weather 大量 fairplayd lookup |
| g240 | 采样调查组 | g240/g241/g243 分别调查 backboardd、SpringBoard、Weather；单轮进程对应及栈结论待原始日志复核 |
| g241 | 采样调查组 | 同上，未得根因结论 |
| g242 | ASTC 系统原生回答对照 | 已跑；完整帧数、截图和时序待分析 |
| g243 | 采样调查组 | 同上，未得根因结论 |
| g244 | 关闭 ADSP 排查回归 | 已跑；不能据此宣称 ADSP 有关或无关 |
| g245 | 回填 fairplayd 后，09:36；`pong_touch_synth=3 tsa=1118 touch_lat=1`，ASTC 状态沿用当时镜像 / plugin | `PONG FPS SUMMARY`：frames 2560、moving 2453、72.0 fps；`failed lookup: name = com.apple.fairplayd` 为 0 行；launchd 仍记录 fairplayd 反复重启。**App 内容、Weather 恢复、ASTC 开关实际状态均待验证** |

没有 texture-refused 和查找失败日志不代表纹理内容正确，也不代表 fairplayd 已能正常服务。g245 的帧数恢复不能单独证明 ASTC 修好了。

## 3. fairplayd.A2：当前镜像与重建策略不同

11-01 的 `EXCLUDED18` 排除了 `com.apple.fairplayd.A2`，`jobs-refresh.sh g213 …` 同时从已安装镜像移除了任务。09:28 为调查 ASTC / Weather 卡住，前一会话使用反向脚本，从保留的 **g213 修改前镜像**回填 launchd 服务缓存中的 job 和对应 plist，重算 UFS layout 后只安装 OS 窗口，耗时约 480 s。

回填的是当前本地 OS 镜像；`userland/build-ipados18.py` 的 `EXCLUDED18` **仍排除 fairplayd.A2**，全量重建会再次去掉它。本次只保存为 [job-restore.sh](../../tools/zizhan/mac/job-restore.sh)，不改变默认排除策略。恢复服务配置不等于恢复服务功能；为什么仍重启、是否与 UUID 有关尚无结论。原始问题见 [电池记录 §8](2026-11-01-battery.md#8-设置--电池卡死根因链g208g216)。

## 4. display-link WIP（item 2）

`userland/ui_compat18.c` 在 SpringBoard 内钩住 CADisplayLink 的工厂、preferred FPS 和 frame-rate range；只标记 target 为 `UIViewInProcessAnimationManager` 的 link，并清掉它请求的速率。代码默认开启，`pong_dl_full=0` 保留全部原速率，`touch_lat=1` 打印创建与请求的调用方。来自 hakios `7caa457`，其 78→115 fps 不能当作本机收益。

**未验证**：22H374 是否使用该 target / 工厂、清零后的实际速率、首屏时构造函数是否已能取得类、其他动画的影响。中断前未同步到 Mac，不能把 g245 当成这段代码的结果。

## 5. 下一步

1. 在 Mac 保存并对照 g245 / g239 的干净 klog、launchd 和 Weather 栈；补记 plugin、UI 库、内核版本及完整命令行，确认 g245 的 ASTC 实际回答。结合 g242/g244 分离 ASTC、fairplayd 和 ADSP 变量。
2. 同镜像做 fairplayd 有 / 无对照，检查 Weather 是否绘制、设置电池页是否回归、respawn 原因，再决定是否移出 `EXCLUDED18`。不要先把假设固化成默认策略。
3. ASTC 开 / 关均用 `pong_shot` 确认 Weather、设置、多任务确实有内容；截图会影响性能，视觉轮和计时轮分开。11-01 的教训：黑屏 App 的“提速”没有意义。
4. 在 Mac 构建两套内核配置作回归门禁，另编 Metal plugin 和 UI 库；随后部署 item 2，用 `pong_dl_full=0` A/B，检查 33 ms 回弹段是否变为约 8.3 ms，并确认没有无效唤醒或新卡顿。
5. 按原顺序继续 item 3–6：nopreamble（记录 spill / draw 时间）；真实分配量与有界 BO 缓存、spare blocks；受限 Poster 放行；画布格式 / 压缩 / 脏区和截图全宽。

## 6. 待用户验证

- ASTC 开启时 Weather、设置和多任务均显示完整内容；记录对应轮次与截图，不能只看图标。
- fairplayd 回填后 Weather 能打开，设置 › 电池仍能进入；健康度转圈属于既有未解问题。
- item 2 A/B 后翻页回弹是否更顺、其他动画是否异常。
- 内核截图与真实屏幕是否同样缺右侧；HUD 右侧数字是否可见。

## 7. 本次公开交接检查

在 Linux 接管环境运行 `git diff --check`、恢复脚本的 `bash -n` 和内嵌 Python 语法检查；`porting.toml` 可解析。按仓库 CI 固定的 UniXNU `0fc0fc1f9ddd0fbd4787738afef38667df5e0a95` 准备检查器并补入 PongPlatform24 元数据，执行 `scripts/porting-record.py check --device <device-checkout> --base upstream/main`，13 个区域通过。暂存 diff 执行隐私关键词扫描并人工复核。

没有运行 Mac 的两套内核配置、Metal plugin 或 UI 库构建；没有启动手机、重装镜像或重新测量 g245。本次保存 WIP 不代表构建或设备验证通过。
