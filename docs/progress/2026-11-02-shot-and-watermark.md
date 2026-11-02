# 截图右侧黑区更正与纹理布局标识（2026-11-02）

## 截图右侧黑区

旧 `shotCapture` 对 2160×1916 的画面每 16 像素采一个点，得到 135×120 的 RGB 图。它把一整行的 135 个像素（810 个十六进制字符）交给一次 `IOLog`。实测 g247 的每行日志只有 255 B：例如第 60 行只存 40/135 个像素。旧 `logs/shot.py` 仍宣称“120/120 行完整”，把剩余像素补零，于是 PNG 的右侧约 70% 成了人工黑区。这个黑区不能用来判断面板或 CA 画布的真实内容。

现在 `PongAdrenoScanout.cpp` 每 32 个采样像素输出一段，格式为 `PONG SHOT <n> <row> <x> <RGB hex>`；每段最多约 220 B，低于已观察到的单行上限。仓库内新增 `tools/zizhan/mac/shot.py`，按行号和列偏移拼接，并检查每个像素是否真的在日志里。旧日志只能恢复左侧 39–40 列：解码器裁到全部行都真实拥有的 39 列，明确报告“old IOLog truncation”，不再补黑。g247 的 6 张本地 PNG 已据此重新生成。遗失的右侧像素无法从旧日志找回。

主机验证：真实 g247 日志被识别为 `120/120 rows, cropped to 39/135 columns`；合成的 135 列分段日志能拼成完整 PNG，右端像素已核对；故意缺一段时裁到 64/135 列并提示。`python3 -m py_compile` 通过；`ci_kernel.sh` 使用 `zizhan_ipados18_gpu_defconfig` 构建成功，见本地 `$ZIZHAN_SRC/ci_kernel-shotfix.log`。已打包但未启动 `$ZIZHAN_SRC/out/boot-shotfix-deferred/pong-xnu-initial.img`，它会自动点天气、抓 6 张新格式截图并自动返回；命令行 953 B。按照本次“验证先攒着”的安排，尚未用新格式做设备截图。

## 另一项改进：布局 watermark

`NBBlockTextureLayout` 之前的 128-bit `watermark` 只包含像素格式、宽、高；相同宽高但 mip 层数、深度、数组层数不同的纹理得到同一个标识，虽然它们的布局大小和偏移不同。现把支持范围内的格式、纹理类型、宽、高、深度、mip 数、slice 数和每块字节数逐字段编码进两字，避免这类碰撞。格式和类型上限也与编码位宽核对。Metal 桥接库在主机构建成功，日志为本地 `$ZIZHAN_SRC/work/gpu/logs/bridge-watermark-host.log`。此改动尚未安装到设备。

## 累积的设备验收

1. 用新内核抓主屏、天气和多任务截图：每张应为 `135/135 columns`、`120/120 rows`；检查画面右侧的真实内容。旧 g247 的右侧黑区不构成设备故障证据。
2. 安装 watermark 版 Metal 桥接库后，重跑 `pong_astc=1` 的天气截图，并抽查其他 App 与多任务画面；留意是否有布局或渲染回归。
3. 人工确认打开/返回动画与触摸体验；GPU fence 停滞仍按独立问题追踪。
