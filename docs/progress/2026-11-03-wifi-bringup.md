# Wi-Fi QCA6490 首轮准备（2026-11-03）

## 实机输入与隔离

Android live DT 显示 WLAN 为 QCA6490（PCI `17cb:1103`），走 PCIe0；使能 GPIO 80、PERST 94、CLKREQ 95、WAKE 96、XO 204，SID 为 `0x1c00/0x1c01`。Android 运行时链路是 Gen2 x1。固件只读导出到本机 `$ZIZHAN_SRC/work/pong-inputs/pong-wifi-firmware`（213 个文件）；Linux v7.0.14 ath11k 源码留在本机输入目录。二者以及打包后的固件对象均不入仓库。

新增 `zizhan_ipados18_wifi_defconfig`，只在这个试验配置中打开 `CONFIG_XNU_PONG_WIFI=y`。发布默认 GPU 配置仍关闭 Wi-Fi。试验镜像还需 `WIFI=1`，使 `ci_boot_ufs.sh` 写入首个有效的 `pong_wifi=1` boot-arg；后面追加同名参数无效。

构建器在精简 ADT DTB 没有 WLAN 字段时使用已核对的板级 profile；显式 `PONG_WIFI_DTB` 必须是完整 live DT，且校验 GPIO。Wi-Fi 输入路径从本机 `PONG_INPUTS` 派生，固件目录明确传给板级归档构建器。

## 主机构建

| 配置 | 结果 | 本地日志 |
| --- | --- | --- |
| `zizhan_ipados18_wifi_defconfig` | PASS；ath11k 与板级归档、内核链接完成 | `$ZIZHAN_SRC/ci_kernel-wifi-probe.log` |
| `pong_ipados18_ci_defconfig` | PASS | `$ZIZHAN_SRC/ci_kernel-wifi-baseline.log` |
| `zizhan_ipados18_gpu_defconfig` | PASS | `$ZIZHAN_SRC/ci_kernel-wifi-gpu.log` |

`$ZIZHAN_SRC/out/boot-wifi-probe/pong-xnu-initial.img` 已打包，命令行 913 字节，包含 `pong_wifi=1` 与自动返回参数。它只是本机试验产物，不是设备验证结果。

## 设备轮次与 USB

计划中的 g265 **没有启动镜像**：`cycle.sh` 在 `boot.sh` 的 `fastboot devices` 阶段阻塞，未执行 `fastboot boot`。随后 adb 与 fastboot 均不可见，USB 枚举中只有主机 hub。按记录端口执行 `usbkick.sh kick` 后仍未枚举。不能从 g265 推断 PCIe、供电、MHI、固件或扫描状态；当前无截图或内核日志可归于 Wi-Fi 试验。

为避免下一轮无限阻塞，`fastboot-visible.sh` 给探测加 6 秒上限，`boot.sh` 与 `cycle.sh` 均调用它。模拟可见、不可见、阻塞三种 fastboot 响应的主机检查分别返回成功、失败、6 秒超时。实际设备自动回路仍待 USB 恢复后验证。

无人值守恢复脚本 `when-back.sh` 也改用有界探测；设置 `WHEN_BACK_IMAGE` 后，不论手机先回到 Android 还是 fastboot，都会用指定的已打包镜像调用 `cycle.sh`。主机模拟 fastboot 恢复时，g266 选择了正确试验镜像和 420 秒等待参数；设备端结果仍待取回。

本机已为 g266 启动最多 12 小时的 USB 恢复监视，输出留在 `$ZIZHAN_SRC/logs/g266-when-back.log`。当前主机工作根目录也已链接新探测脚本；监视进程存活，adb 和 fastboot 仍不可见。这个等待状态不算 g266 已启动设备。

## 下一步

USB 恢复后先确认 Android 或 fastboot 可见，再用现有试验镜像执行 g266 临时 `fastboot boot` 轮次。取回 klog/ramoops 并记录 PCIe、供电、MHI、固件、MSI、扫描中的首个失败阶段；随后改动、重新完成双配置构建和上机验证。不要 flash 分区。
