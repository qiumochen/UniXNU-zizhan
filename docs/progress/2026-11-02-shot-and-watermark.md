# 截图右侧黑区更正与纹理布局标识（2026-11-02）

## 截图右侧黑区

旧 `shotCapture` 对 2160×1916 的画面每 16 像素采一个点，得到 135×120 的 RGB 图。它把一整行的 135 个像素（810 个十六进制字符）交给一次 `IOLog`。实测 g247 的每行日志只有 255 B：例如第 60 行只存 40/135 个像素。旧 `logs/shot.py` 仍宣称“120/120 行完整”，把剩余像素补零，于是 PNG 的右侧约 70% 成了人工黑区。这个黑区不能用来判断面板或 CA 画布的真实内容。

现在 `PongAdrenoScanout.cpp` 每 32 个采样像素输出一段，格式为 `PONG SHOT <n> <row> <x> <RGB hex>`；每段最多约 220 B，低于已观察到的单行上限。仓库内新增 `tools/zizhan/mac/shot.py`，按行号和列偏移拼接，并检查每个像素是否真的在日志里。旧日志只能恢复左侧 39–40 列：解码器裁到全部行都真实拥有的 39 列，明确报告“old IOLog truncation”，不再补黑。g247 的 6 张本地 PNG 已据此重新生成。遗失的右侧像素无法从旧日志找回。

主机验证：真实 g247 日志被识别为 `120/120 rows, cropped to 39/135 columns`；合成的 135 列分段日志能拼成完整 PNG，右端像素已核对；故意缺一段时裁到 64/135 列并提示。`python3 -m py_compile` 通过；`ci_kernel.sh` 使用 `zizhan_ipados18_gpu_defconfig` 构建成功，见本地 `$ZIZHAN_SRC/ci_kernel-shotfix.log`。`$ZIZHAN_SRC/out/boot-shotfix-deferred/pong-xnu-initial.img` 已打包，命令行 953 B。

用户随后允许自动化验证。g248 用上述镜像临时启动，`pong_astc=1`，自动点天气。取回的 6 张图均为 **135/135 列、120/120 行**，不再有缺失数据；`shot1` 的主屏图标和 Dock 横跨画面，`shot4`–`shot6` 的天气蓝色画面横跨整幅图。旧截图右侧黑区确为日志缺失造成；这条测试路径没有发现对应的整屏黑区。21:20 自动回到 Android，ADB 已确认。日志和 PNG 留在本地 `$ZIZHAN_SRC/logs/g248*`。

## 另一项改进：布局 watermark

`NBBlockTextureLayout` 之前的 128-bit `watermark` 只包含像素格式、宽、高；相同宽高但 mip 层数、深度、数组层数不同的纹理得到同一个标识，虽然它们的布局大小和偏移不同。现把支持范围内的格式、纹理类型、宽、高、深度、mip 数、slice 数和每块字节数逐字段编码进两字，避免这类碰撞。格式和类型上限也与编码位宽核对。Metal 桥接库在主机构建成功，日志为本地 `$ZIZHAN_SRC/work/gpu/logs/bridge-watermark-host.log`。`metal-refresh.sh watermark` 于 21:29 完成设备 OS 窗口安装，安装前保留 `pong-ipados18-system.astc-final.cdr`，日志为本地 `$ZIZHAN_SRC/work/gpu/logs/install-watermark.log`。

g249 使用已安装的 watermark 版桥接库和分段截图内核，临时启动且显式启用 ASTC。backboardd、SpringBoard 回答 `DeviceSupportsASTC 1`，Weather 成功启动；6 张截图均为完整的 135×120 采样图。`shot1` 的图标横跨主屏，`shot4`–`shot6` 的天气蓝色画面横跨整幅图。21:31 自动回到 Android，ADB 已确认。截止快照里的 1 个未完成 fence 后来由 g253、g254 证明是自动复位截断的正常在途提交，详见 `docs/progress/2026-11-02-fence-diagnostics.md`。

g250 把合成触控移到主屏照片图标（`tsa=6436`）。6 张截图仍完整；`shot4` 捕获到横跨整屏的照片白色启动页，之后回到主屏。日志显示 Photos 反复启动、约 1.5 秒后以 `NSInternalInconsistencyException` / `!PXPixelSizeAreaIsZero(roundedSize)` 退出；此前有 `IOSurfaceSharedEventAddEventListener failed: e00002c7`。这轮没有通过照片 App 验收，也不能单凭这些日志断定崩溃由 watermark 或 ASTC 引起。后续根因和修复见 `docs/progress/2026-11-03-photos-context.md`。21:34 自动回到 Android，ADB 已确认。

g251 原计划点设置，但 `tsa=6547` 实际落在时钟图标。日志确认 MobileTimer 启动，`shot4`–`shot6` 记录到横跨整幅图的世界时钟地图；6 张截图均完整。21:38 自动回到 Android，ADB 已确认。此轮只算时钟 App 的自动验证，不能算设置。

g252 把合成触控改为 `tsa=3669`，日志确认 Preferences 启动。6 张截图均完整；`shot1`、`shot6` 显示横跨整幅图的设置双栏界面，`shot4` 记录到主屏过渡帧。21:43 自动回到 Android，ADB 已确认。g249–g252 截止时都有 1 个在途 fence；g253、g254 的提交年龄和时间线证明它们不是持续停滞。

## 累积的设备验收

1. 主屏、天气、时钟、设置的整幅自动截图已由 g248–g252 验证；多任务截图尚未自动化。
2. 照片 App 的零尺寸断言已修复并由 g262–g264 验证；SharedEvent 能力仍缺失。
3. 人工确认多任务、打开/返回动画与触摸体验；项目见 `docs/progress/HUMAN_CHECKLIST.md`。
