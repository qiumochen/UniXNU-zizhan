# Development notes

Dated notes from the zizhan bring-up, in Chinese, kept as written during the work: each run (k*, u*, t*, g*) with its boot arguments, result and root cause.

Paths in the notes refer to the author's workspace:

- `src.noindex/` or `$ZIZHAN_SRC` is the Mac work directory; its `UniXNU-zizhan/` is this repository.
- `src.noindex/*.sh`, `*.py` and the workspace `tools/mac/` scripts are in `tools/zizhan/mac/` here; `tools/phone/` is `tools/zizhan/phone/`.
- `device/` was a mirror of this repository's `drivers/`, `patches/` and `scripts/`.
- Logs (`logs/*.txt`) and photos other than those in `docs/evidence/` are not published.

## 最新交接

- [2026-11-05 Wi-Fi 连接生命周期](2026-11-05-wifi-lifecycle.md)：修复原生 PrimaryTimeout、dispatcher 内同步通知和 timer 重入遍历；g337 自动入网及短时保持、g341 原生关开后的第二次 DHCP/HTTPS 与清理通过。
- [2026-11-04 Wi-Fi 设置与跨频段重连](2026-11-04-wifi-settings-and-roaming.md)：提前更新管理帧速率后，2.4↔5 GHz 两个方向都通过三次 HTTPS 和断线清理；缩短原生 InterfaceNamer 回退等待后，g309 从真实 Settings 加入完成 WPA2、DHCP、HTTPS。

- [2026-11-04 display-link 设备对照](2026-11-04-displaylink-device.md)：修正实际 UIKit 动画类名，g298 moving 95.3 fps，基线 56.7–58.4；动画尾段间隔改善，首帧约 80 ms 仍待定位。

- [2026-11-04 原生 Wi-Fi 修正与跨频段故障](2026-11-04-wifi-native-repair.md)：原生接口绑定与 manager 端口修补已有设备证据；跨频段 RDDM、Settings 入网未通过，随后转入 GPU 对照。

- [下一轮硬件适配待办](../plan/2026-11-04-next-hardware-work.md)：Wi-Fi 设置页与换网、GPU 首帧与回弹、内屏调光、外屏出图与折叠切换。
- [2026-11-03 display-link 主机核对](2026-11-03-displaylink-host.md)（当前准备镜像含 hook；UI、bridge、plugin 与双配置构建通过，设备帧率待验证）。
- [2026-11-03 音频板级审查](2026-11-03-audio-board-audit.md)（公开 zizhan DT 与 Pong 音频常量对照；未上机）。
- [2026-11-04 Wi-Fi 与 USB](2026-11-04-wifi-usb.md)：默认 Wi-Fi 路径的 DHCP、续租、断开清理和重连 HTTPS 通过；后部 USB-A 自动部署已实测。
- [2026-11-03 Wi-Fi 首轮准备与扫描](2026-11-03-wifi-bringup.md)。
- [2026-11-03 Photos 零尺寸断言](2026-11-03-photos-context.md)（最终 g264 已上机）。
- [2026-11-02 GPU：ASTC 布局与 fairplayd 回填](2026-11-02-gpu-astc-and-fairplayd.md)（含六项 GPU 工作状态、未验证项和下一步）。
- [2026-11-02 设置首次启动](2026-11-02-settings-launch.md)。
- [2026-11-01 电池](2026-11-01-battery.md)。

旧 `src.noindex` 路径用于理解历史记录；新 Mac 工作区按根目录 [CLAUDE.md](../../CLAUDE.md) 建立，直接在 clone 内编辑、提交。
