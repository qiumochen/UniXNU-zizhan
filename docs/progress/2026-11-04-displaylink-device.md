# 2026-11-04：原生动画 display-link 类名修正与设备对照

Wi-Fi [g292–g295](2026-11-04-wifi-native-repair.md) 未完成跨频段和 Settings 入网验收，按用户授权转到 [硬件待办](../plan/2026-11-04-next-hardware-work.md) 的 GPU 部分。本轮没有改 GPU 队列、BO、缓存、渲染器或面板时钟。

## 发现与修正

g296 的实际日志记录 `-[UIKit.InProcessAnimationManager displayLinkFire:]`，随后为该 link 请求 `20..120, preferred 30`。旧代码只匹配 `UIViewInProcessAnimationManager`，导致默认开启 hook 的 g296 也没有标记目标，日志里没有 `(dropped)`。

`42629e5` 增加已在设备出现的类名，保留旧类名和 `pong_dl_full=0` 回退。只调整匹配动画管理器的 display link；其他目标保持原生请求。g298 保留日志中有 **121 次**被过滤的速率请求，证实设备实际加载并执行了修补。

## 对照方法与结果

g296/g297 使用逐字节相同的 GPU 内核，启动参数只差 `pong_dl_full=0` 与打包 epoch。g298 直接复用 g296 的同一个启动镜像，仅将已安装 UI 库换成修正版本；保留同一 OS 的其他内容、Data 和 SystemOS。三轮均关闭 Wi-Fi，用 `pong_touch_synth=2`、`touch_lat=1`、同一 8 秒 trace、相同自动返回参数，无截图采样。

| 轮次 | 实际行为 | 全轮 moving FPS | trace 零拷贝翻页 | trace 上屏间隔 p50 / p90 |
| --- | --- | ---: | ---: | ---: |
| g296 | 旧 hook 默认开启，但类名未匹配 | 58.4 | 399 | 16.59 / 33.26 ms |
| g297 | 显式 `pong_dl_full=0` | 56.7 | 395 | 16.61 / 33.27 ms |
| g298 | 新类名匹配并过滤速率请求 | **95.3** | **631** | **8.33 / 16.64 ms** |

上屏间隔从保留的零拷贝 latch 时间戳计算，只计连续序号和小于 90 ms 的间隔；g298 的控制台环形区覆盖了早期部分，因此不把缺失的 `tl:` 阶段补成 0。全轮 moving FPS 是排除长停顿的指标，不是面板刷新率，也不是包含静止时间的平均 FPS。

g296/g297/g298 的 trace 分别记录 402/397/635 笔提交，窗口内记录的提交全部完成，均 0 lost；完成数多 1 的轮次包含窗口开始前已提交的工作。GPU 在途占比分别约 23%/23%/27%；没有把帧率收益归为 GPU 提频或换渲染器。上机前电池温度分别为 34.9/33.9/34.9 °C，返回 Android 为 36.9/36.3/37.0 °C；没有同时改热管理策略。

## 首帧与动画尾段分别验收

合成 DOWN 前至少 90 ms 无新 swap 时，取 DOWN 后首次 swap 对应的完成帧。样本数分别为 22/22/18，中位数为 **78.9/80.5/75.5 ms**，p90 为 88.5/86.1/89.7 ms；首帧没有明显改善，不能称触摸到首帧延迟已解决。

抬手后前 0–500 ms 的间隔变化很小。收益集中在较晚的动画：抬手后 **1–1.5 秒**，上屏间隔中位数从 **33.16/33.19 ms** 降为 **8.32 ms**，p90 从约 33.33 ms 降为 8.41 ms。合成翻页的动画尾段已验证；真实手指的回弹感受、Settings 滚动、功耗及其他 App 仍需分别对照，不能从本轮推导出全局 120 fps。

[脱敏原始节选与计算口径](../evidence/2026-11-04-g298-displaylink.txt)。三轮全都以 deadline 回 fastboot、取回新 UFS klog 并回 Android；Mac 启动时间未变。

## 当前镜像与回退

UI 库独立编译、ad hoc 签名与严格验证通过。g298 系统副本仅 6 个 OS 块变化；安装前校验固定 extent、manifest 和所有待写块，安装器逐块读回，没有重装 Data/SystemOS。

已把本机 `prepared/pong-ipados18-system.cdr`、`prepared/libpong-ui.dylib`、`ufs-metal/ufs-layout.json` 和默认 `install-pinned.json` 同步到已验证 g298，避免后续 UI 刷新退回旧库。旧准备镜像、库、布局和安装状态均保留 `.pre-g298` 副本。完整 g295 系统和安装状态也保留在本机 `work/userland-22H374/wifi-native-g295/`，g290 的 Wi-Fi 基线在 `wifi-service-g290/`；回退设备前仍须核对当时活动 manifest 与待写块。

后续先定位仍在约 80 ms 的首帧，再做独立 Settings 滚动；BO、IOSurface 批处理或缓存改动必须由新的分段 trace 支持。内屏调光、外屏和折叠切换本轮没有上机，不提升状态。
