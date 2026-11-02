# fairplayd.A2 对照实验（2026-11-03）

## 目标与现有输入

确认 fairplayd.A2 在当前移植中是否仍快速退出，以及服务缓存的有无如何影响 Settings 电池页与 Weather。内核构建不改变 `EXCLUDED18`；该选项只在重新生成 OS 服务缓存时生效。对照轮次应使用同一内核和启动参数，只改变 OS 镜像中这个 job 的缓存项与 plist。

本机对三个已有 OS 镜像做了只读挂载，并解析 `/System/Library/xpc/launchd.plist`：当前 `pong-ipados18-system.cdr` 和 `g213` 备份均有一条 `com.apple.fairplayd.A2`，对应 plist 存在，`KeepAlive=true`，程序为 `/usr/sbin/fairplayd.A2`；`f245` 备份没有缓存项和 plist。三个镜像均已正常卸载。这证明现有输入的服务状态，不证明它们其他内容一致，也不证明 fairplayd 工作。当前源码 `EXCLUDED18` 仍排除该 job；全量重建会再次移除它。

## 设备轮次设计

| 阶段 | OS 服务状态 | 操作与证据 |
| --- | --- | --- |
| B1 | 当前镜像含 fairplayd | g269 Wi-Fi 轮次结束并恢复连接后，使用同一内核启动，收集 launchd/fairplayd 退出频率、Settings 与 Weather 的日志和截图。 |
| A | 从 B1 当前镜像仅移除 fairplayd 的缓存项和 plist | 在 Android/adb 可用且根镜像备份已核对时，用新标签运行 `jobs-refresh.sh TAG com.apple.fairplayd.A2`；自动返回后收集同样证据。 |
| B2 | 恢复 fairplayd 的缓存项和 plist | 用 `job-restore.sh TAG g213 com.apple.fairplayd.A2` 恢复同一项，再重复采集，排除时间和缓存状态的影响。 |

`f245` 可作为历史参照，不能直接充当 A：它与当前镜像还可能有其他差异。天气数据下载还依赖网络；只有网络已实测可用时才将 Weather 在线结果用于判断。电池健康度依赖 thermalmonitord 的管理状态及 powerd 的服务状态，fairplayd 单项恢复不应预设能产生健康度数值。

## 主机安全检查

`jobs-refresh.sh` 现在先校验参数、备份标签和 Android adb 可见，再复制与修改 OS 镜像；不存在的 label 会报错，避免无变化却重写手机 OS 窗口。挂载和编辑失败时执行卸载清理。主机检查包括脚本语法、无设备时的提前拒绝、备份标签冲突拒绝，以及用临时 plist 验证缺失 label 不写缓存。设备写入、启动行为、日志和截图仍待 USB 枚举恢复，不能记为已验证。

`pong_ipados18_ci_defconfig` 和 `zizhan_ipados18_gpu_defconfig` 均完成 `ci_kernel.sh` 构建，日志分别在本机 `$ZIZHAN_SRC/ci_kernel-fairplay-guard-ci.log` 与 `ci_kernel-fairplay-guard-gpu.log`，末尾均为 `KERNEL_DONE`。本轮没有修改内核；构建结果只验证工作树仍可构建。g268 等待器继续由 launchd 托管，截至本次记录 adb/fastboot 无枚举，没有本轮设备日志或截图。

B2 使用的 `job-restore.sh` 也在复制当前 OS 镜像前检查 Android adb。无设备的主机检查提前拒绝，未创建新标签备份；脚本语法通过。实际恢复服务的效果仍需设备轮次验证。

## 下一步

优先完成 g269 的临时 Wi-Fi 启动、自动返回和日志/截图取证。随后按 B1–A–B2 顺序执行 fairplayd 对照，分别核对 OS layout 与安装结果；依据实际退出、同步请求与 UI 证据决定是否修改 `EXCLUDED18`。
