# fairplayd.A2 对照实验（2026-11-03）

## 目的
在 USB 设备不可见期间，通过本地双配置验证 `EXCLUDED18` 移除 fairplayd 对 Battery/Weather 行为的影响。

## 前提
- 主机可独立构建 `pong_ipados18_ci_defconfig` 和 `zizhan_ipados18_gpu_defconfig`（含/不含 fairplayd）。
- fairplayd 上次表现：启动后快速退出，KeepAlive 每 10 s 重启，launchd 阻塞端口。

## 实验设计
| 项目 | A（baseline） | B（对照） |
| --- | --- | --- |
| 服务缓存 | `EXCLUDED18` 包含 fairplayd | `EXCLUDED18` 排除 fairplayd |
| 预期行为 | 电池健康页无转圈提示但健康度可能为空 | fairplayd 正常响应；电池健康度显示数值 |

## 构建与部署
1. 本地构建两套内核与打包，标签分别为 `g-fp-x` 和 `g-fp-ok`。
2. 两轮均用 `ci_boot_ufs.sh` 打包，不改动 rootfs UUID。

## 自动化验证脚本（macos 侧）
- `tools/mac/baseline-fairplayd-run.sh TAG WAIT`：fastboot boot → 等待 launchd 稳定（默认 180 s）→ 抓取日志并统计崩溃次数、端口占用时间。
- 若 B 镜像出现频繁崩溃（>5 次/10 分钟），则回退到 A 策略。

## 成功标准
- B 镜像下 fairplayd 不再频繁崩溃，设置电池页健康度字段有数值或明确“不可用”文案。
- Weather App 能在打开后 1–2 分钟内完成天气数据下载。

## 下一步
USB 恢复后优先执行 g268 Wi‑Fi 轮次；若无设备结果则切换到 fairplayd 对照实验的本地构建与自动化验证流程。
