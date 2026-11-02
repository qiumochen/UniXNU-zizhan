# ASTC 纹理布局与 App 黑屏（2026-11-02）

## 问题和改动

`pong_astc=1` 让 backboardd 和 SpringBoard 回答 `DeviceSupportsASTC=1`。旧版 g194 中，天气 App 已启动，但截图里 App 区域全黑；同配置关闭 ASTC 的 g193 有天气的蓝天画面。原因和其他移植项目的记录吻合：`NBTextureLayout` 拒绝压缩格式、多级 mip 和数组纹理；`newTextureWithBytesNoCopy` 也只接受页对齐的线性纹理。详见 `docs/research/2026-11-02-sibling-gpu-update.md` §3a。

`drivers/PongMetal/NativeBridgeMetalFrontend.mm` 增加 `NBBlockTextureLayout`：按压缩块计算每个 slice / mip 的偏移和大小，实现线性数据与纹理内存的双向拷贝；NoCopy 入口把该布局的数据拷进普通纹理，`length=0` 时取布局大小。布局还限制纹理尺寸、mip 数量，拷贝时核对 region 边界，防止异常 descriptor 导致大小计算或访问越界。原来的单级 2D 线性纹理路径保留。

## 主机构建

`metal-refresh.sh m239` 在 08:52 成功编译 Metal 桥接库和插件，更新本地 OS 镜像；09:00 安装了该 OS 窗口。证据在本地 `$ZIZHAN_SRC/work/gpu/logs/{bridge,plugin,refresh,install}-m239.log`。增加边界检查后，`metal-refresh.sh astc-final` 再次构建成功，20:55 完成 OS 窗口安装；布局核对显示 SystemOS 和 Data 未变，安装前保留了 OS 镜像副本。证据在本地同目录的 `*-astc-final.log`。本次代码的 ASTC 枚举范围和块尺寸已与本机 iPhoneOS SDK 的 `MTLPixelFormat.h` 核对；`git diff --check` 通过。

## 设备验证

测试均通过临时 `fastboot boot` 启动，带自动返回参数，结束后设备回到 Android。日志和截图留在本地 `$ZIZHAN_SRC/logs/`，未放进源码仓库。

| 轮次 | ASTC | 结果 |
|---|---|---|
| g193 / g194，修复前 | 关 / 开 | `shot4`：关时天气显示蓝天；开时 App 区域全黑。 |
| g239，m239 修复后 | 开 | backboardd 和 SpringBoard 都回答 ASTC=1；Weather 启动；83.113 s 的 `shot1` 有蓝天天气画面。 |
| g246，m239 修复后 | 开 | backboardd 和 SpringBoard 都回答 ASTC=1；Weather 启动；60.051 s 的 `shot1` 为主屏，63.067–65.074 s 的 `shot4`–`shot6` 连续显示天气蓝色画面，6 张截图均为 120/120 行。自动返回 Android。 |
| g247，增加边界检查并部署 `astc-final` 后 | 开 | 20:55 临时启动；backboardd 和 SpringBoard 都回答 ASTC=1；Weather 启动；60.144 s 的 `shot1` 为主屏，63.298–65.333 s 的 `shot4`–`shot6` 连续显示天气蓝色画面，6 张截图均为 120/120 行；20:58 自动返回 Android，ADB 已确认。 |

因此，当时能确认的是**自动点开天气时采到的左侧区域不再全黑**。截图每 16 像素取 1 个点，不能用于判断细节像素或动画手感；g247 时尚未验证整屏、其他 App 和多任务画面。

**截图范围更正**：旧 `PONG SHOT` 每行日志被 `IOLog` 截到约 255 B，实际只留下左侧 39–40/135 个采样列；旧解码器把右侧缺失数据补黑。以上结果只能证明采到的左侧天气区域不再全黑，不能证明整屏已经正确。修正见 `docs/progress/2026-11-02-shot-and-watermark.md`。

**随后整屏验证**：g248 用分段截图重新跑 `pong_astc=1` 和自动点天气，6 张截图均完整记录 135/135 列；主屏和天气画面横跨整幅图。因此天气 App 的整屏黑区在这条路径上已排除，旧图右侧黑区确属解码伪影。

安装布局 watermark 版桥接库后，g249 再次验证天气整幅画面；g251 时钟、g252 设置也启动并有整幅 App 截图。g250 照片 App 虽显示白色启动页，却反复因 `!PXPixelSizeAreaIsZero(roundedSize)` 断言崩溃，不能算通过。详情见 `docs/progress/2026-11-02-shot-and-watermark.md`。

用同一截图区域（画面宽度约 8%–25%、高度约 12%–88%）抽样：g194 `shot4` 的黑像素占 84.7%；g193 `shot4`、g239 `shot1`、g246 和 g247 的 `shot4`–`shot6` 的蓝色像素均占 100%。这只量化上述画面差异，不是帧率数据。

## 分开跟踪的问题

当时把 g239–g247 截止日志里的 1 个未完成 fence 理解为 GPU 停滞。g253、g254 增加了时间线记录窗口和 fence 年龄后确认：提交均全部完成，未完成项是自动复位前 26 ms 刚发出的正常在途工作。详见 `docs/progress/2026-11-02-fence-diagnostics.md`。这些数据仍不能当作本次修复的性能收益；早先有直接证据的整机 GPU 丢失继续由 stuck-copy 和 poison 检测跟踪。

需要人工集中确认：多任务画面及实际打开/返回动画是否正确。照片 App 崩溃另需定位。确认前保持 `pong_astc` 默认关闭；本轮只在 boot-arg 中显式设 1。

已在本地重新打包 `$ZIZHAN_SRC/out/boot-astc-manual/pong-xnu-initial.img` 供集中验收：触摸开启、ASTC=1，无合成触摸；`pong_autoreset=600 pong_autoreset_sb=240`，命令行 907 B。此镜像使用分段截图内核和当前已安装的 watermark 版 OS，尚未临时启动。用已有 `cycle.sh` 临时启动时可在手机上依次检查多任务、设置、天气和动画，结束后脚本会取回日志并让手机返回 Android。
