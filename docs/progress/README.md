# Development notes

Dated notes from the zizhan bring-up, in Chinese, kept as written during the work: each run (k*, u*, t*, g*) with its boot arguments, result and root cause.

Paths in the notes refer to the author's workspace:

- `src.noindex/` or `$ZIZHAN_SRC` is the Mac work directory; its `UniXNU-zizhan/` is this repository.
- `src.noindex/*.sh`, `*.py` and the workspace `tools/mac/` scripts are in `tools/zizhan/mac/` here; `tools/phone/` is `tools/zizhan/phone/`.
- `device/` was a mirror of this repository's `drivers/`, `patches/` and `scripts/`.
- Logs (`logs/*.txt`) and photos other than those in `docs/evidence/` are not published.

## 最新交接

- [2026-11-03 Wi-Fi 首轮准备](2026-11-03-wifi-bringup.md)（三配置已构建；g268 等 USB 恢复，尚未上机）。
- [2026-11-03 Photos 零尺寸断言](2026-11-03-photos-context.md)（最终 g264 已上机）。
- [2026-11-02 GPU：ASTC 布局与 fairplayd 回填](2026-11-02-gpu-astc-and-fairplayd.md)（含六项 GPU 工作状态、未验证项和下一步）。
- [2026-11-02 设置首次启动](2026-11-02-settings-launch.md)。
- [2026-11-01 电池](2026-11-01-battery.md)。

旧 `src.noindex` 路径用于理解历史记录；新 Mac 工作区按根目录 [CLAUDE.md](../../CLAUDE.md) 建立，直接在 clone 内编辑、提交。
