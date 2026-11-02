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

g266 的 USB 恢复监视曾启动；随后发现旧镜像引用了 Pong 板级 profile，已主动停止监视。g266 没有执行 `fastboot boot`，旧镜像不得作为 zizhan Wi-Fi 结果。

## 实机板级 profile 校正

重新解析先前只读保存的 zizhan Android live DT 后，发现旧 Pong profile 与本机的 82 条 PHY 表有 6 条值不同；`max-clock-frequency-hz` 的非零项位置不同，MSI/IOMMU map 的控制器引用不同。旧 profile 还包含本机 WLAN 节点没有的 `vdd-wlan-ipa` 供电请求。两块板的 WLAN GPIO、独立 SID、主电源资源名则一致。旧试验内核虽能链接，也不能推断这些板级输入适合 zizhan。

新增 `configs/zizhan-wifi-sm8475.json`，由 zizhan live DT 的 PCIe0/WLAN 节点生成，并只保留实际驱动所需的板级字段。共 6 条 WLAN rail 请求和 2 条 PCIe 模拟电源请求。已有 Pong PMIC PPID 只在资源名和 regulator 名均与本机 DT 对应时保留；驱动运行时仍须读取 PMIC 类型和状态后才能继续。构建器使用 zizhan profile，显式提供完整 live DT 时逐项核验 PCIe、DMA、MSI 和供电请求。将一处 PHY 值改动注入临时 DT 后，构建器按预期拒绝输入。

修正后的试验配置、`pong_ipados18_ci_defconfig` 和 `zizhan_ipados18_gpu_defconfig` 均构建成功，本机日志分别为 `$ZIZHAN_SRC/ci_kernel-wifi-zizhan-final.log`、`ci_kernel-wifi-zizhan-ci.log` 和 `ci_kernel-wifi-zizhan-gpu.log`。生成的内核输入有 82 条 zizhan PHY 表、6 条 WLAN rail 请求，且没有旧 `vdd-wlan-ipa` 请求。新镜像 `$ZIZHAN_SRC/out/boot-g267/pong-xnu-initial.img` 的命令行 912 字节，含唯一的 `pong_wifi=1`、`pong_autoreset=300` 和 `pong_autoreset_sb=60`。它仍没有设备启动或截图证据。

本机已启动最多 12 小时的 g267 USB 恢复监视，输出留在 `$ZIZHAN_SRC/logs/g267-when-back.log`；当前 adb 和 fastboot 均未枚举手机。只有日志显示 `fastboot boot` 执行后，才能把 g267 计为设备轮次。

## QMI 板数据命名修正

离线审查发现 ath11k 的 board-data 选择在 QMI `chip_id` 要求 `bdwlang` 前缀、且 `board_id > 0xff` 时，原生成代码把前缀固定写成了 `bdwlan`。现统一使用实际选出的前缀，仍只按 QMI 返回的 ID 取文件；没有为本机猜测 board ID。g267 的监视在未启动设备时撤下，因此 g267 也没有设备结果。

Wi-Fi 配置和约定的 CI/GPU 两配置重新构建通过；生成的 ath11k `core.c` 已包含高位 board ID 的正确前缀。主机日志为 `$ZIZHAN_SRC/ci_kernel-wifi-bdf-final2.log`、`ci_kernel-wifi-bdf-ci.log`、`ci_kernel-wifi-bdf-gpu.log`。打包器在 GPU 配置占用共用构建树时拒绝打包旧 Wi-Fi 导出；把 Wi-Fi 配置最后重建后，g268 镜像打包成功。`$ZIZHAN_SRC/out/boot-g268/pong-xnu-initial.img` 的命令行 912 字节，Wi-Fi 开关出现一次，两个自动返回参数均在。QMI 实际 ID、所选文件和扫描结果仍需要设备日志。

本机已为 g268 启动最多 12 小时的 USB 恢复监视，日志留在 `$ZIZHAN_SRC/logs/g268-when-back.log`。监视进程存活，手机仍未枚举；这不构成 g268 设备验证。

## 下一步

USB 恢复后先确认 Android 或 fastboot 可见，再用 g268 镜像执行临时 `fastboot boot` 轮次。取回 klog/ramoops 并记录 PCIe、供电、MHI、固件、MSI、扫描中的首个失败阶段；随后改动、重新完成双配置构建和上机验证。不要 flash 分区。
