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

## 自动轮次工具加固

复查发现 `boot.sh` 曾把 `fastboot boot` 的输出接给 `tail`，因此启动命令失败时仍可能打印 `booted` 并让 `cycle.sh` 继续等候；RamoopsDump 启动也有同样的退出码问题。现在用 180 秒有界的 `fastboot-boot.sh` 保留真实退出码，`boot.sh`、`cycle.sh` 和 `cycle-resume.sh` 在传输失败时停止。`adb-visible.sh` 把 Android 枚举检查限制在 6 秒内，只有 `device` 状态才算可见；等待器每 5 分钟记录一次仍在等待，12 小时到期明确报错。

主机模拟覆盖 adb 的可见、未授权、阻塞，以及 fastboot boot 的成功、失败；阻塞 adb 在 6 秒内退出，失败的 fastboot boot 不再产生 `booted` 消息。脚本语法和 `git diff --check` 均通过。约定的 CI、GPU、Wi-Fi 三套配置重新构建通过，本地日志分别为 `$ZIZHAN_SRC/ci_kernel-g268-usbguard-ci.log`、`ci_kernel-g268-usbguard-gpu.log`、`ci_kernel-g268-usbguard-wifi.log`。内核源码未变，设备恢复时仍使用已打包的 g268 镜像；镜像含 `pong_shot=60`、`pong_shot_ms=1000`，便于取得首轮屏幕采样。

按已记录端口执行一次 USB 断电重连后，adb 和 fastboot 仍未枚举。旧等待进程已停止，新版 g268 等待器已启动，首行写入 `$ZIZHAN_SRC/logs/g268-when-back.log`。截至本次记录，没有执行 `fastboot boot`，因此没有 g268 内核日志、截图或自动返回证据。

`porting-record.py check --base origin/main` 对本次增量的 13 个 area 检查通过。直接用当前 `upstream/main` 作基线会因上游后来新增的 `drivers/PongBootCapture/PongBootCapture.h` 在本设备记录里无 owner 而失败；该文件未进入本设备工作树，本次没有将其误记为设备改动。

## 本轮交接状态

本机 `main` 已提交 USB 轮次工具和结构化记录（`890ec93`、`b415a17`），工作树无待提交源码。向 `origin main` 的普通推送失败：当前无交互会话的 macOS Keychain 无法交出 GitHub 凭据；`gh` 与 SSH 也没有可用认证。没有向 upstream 推送，也没有强推。恢复可用 GitHub 认证后，先核对远端再普通推送本机提交。

g268 等待器仍在本机运行，手机若在 12 小时窗口内枚举，会自动用已打包镜像开始轮次。下一次接续先看 `$ZIZHAN_SRC/logs/g268-when-back.log`、USB 枚举和 `logs/g268*`，确认是否真的执行过 `fastboot boot`、是否自动回 Android、是否取得 klog/ramoops 和截图；然后处理 Wi-Fi 的首个失败阶段。若等待已过期且手机仍不可见，按待人工清单恢复实体 USB 连接后再启动有界等待。

## 接续：持久等待与推送恢复

后续接管时发现普通工具会话启动的 g268 等待进程已随会话结束而退出，日志停在 `still waiting for USB`，没有设备轮次。已改由当前用户的 launchd 图形域提交同一个 `when-back.sh g268` 命令；`launchctl list` 显示任务运行，进程父级为 launchd，日志出现新的等待首行。该任务仍有 12 小时上限，不会无限循环。手机继续在 adb、fastboot 两种状态下都不可见。

同一图形域执行的一次性普通 `git push origin main` 成功，远端 `main` 与本机的 `5eaa262` 一致，先前 Keychain 失败的三个提交已全部发布；未推送 upstream。此处只记录认证环境差异，未读取或保存令牌。下次先检查 launchd 任务状态和 g268 日志，再决定是否需要重新启动等待器。仍须以实际 `fastboot boot`、自动返回、klog/ramoops 和截图判定设备结果。

## 固件包与 g268 镜像一致性

离线解析最后一次 Wi-Fi 构建的 `PWF2` 固件包：213 条目录项均有合法偏移和范围，名称无重复；逐项 SHA-256 与本机只读导出的 213 个文件一致。g268 的内核 Mach-O、打包阶段内核和最终启动镜像中均能找到与该固件包逐字节相同的内容，故这轮不会因打包遗漏 BDF 文件而失败。包内有 160 个 `bdwlan.eXX`、11 个 `bdwlang.eXX` 和两种 `.elf` 回退文件，没有高位 board ID 命名的文件。若 QMI 实际返回高位 ID，仍需设备日志确认所请求的精确名称，再查原厂输入；不能根据现有文件列表猜测或替换 board ID。

这只是主机包内容验证。等待器仍在运行，日志继续显示 USB 等待；没有新的 `fastboot boot`、设备日志或截图。

## 取证链防止旧 logfs 冒充本轮

检查 `cycle.sh` 和 `cycle-resume.sh` 时发现，两者曾忽略 Android 侧 `dd` 导出与 `adb pull` 的退出码；如果本机 `/tmp/logfs-$RUN.img` 已存在，拉取失败后仍可能把旧内容当成本轮的 `XNULOG.TXT`。新增 `pull-logfs.sh`：先删除本机同名副本，再清除 Android 临时副本并导出；任一步失败或拉取结果为空，都删除本机副本并停止轮次。两条轮次路径现在共同调用该脚本，避免行为分叉。

主机失败注入覆盖导出失败、拉取中途留下部分文件、空文件和成功四种情况；前三种均返回失败且本机无旧副本，成功仅留下新内容。脚本语法和暂存差异检查通过。此修改只影响自动取证，g268 的内核镜像无需重打包。CI、GPU、Wi-Fi 三配置构建日志分别留在 `$ZIZHAN_SRC/ci_kernel-g268-logfs-guard-ci.log`、`ci_kernel-g268-logfs-guard-gpu.log`、`ci_kernel-g268-logfs-guard-wifi.log`；设备端取证仍需 USB 恢复后验证。

## 下一步

USB 恢复后先确认 Android 或 fastboot 可见，再用 g268 镜像执行临时 `fastboot boot` 轮次。取回 klog/ramoops 并记录 PCIe、供电、MHI、固件、MSI、扫描中的首个失败阶段；随后改动、重新完成双配置构建和上机验证。不要 flash 分区。

## 接续：自动解码截图

g268 镜像已有 `pong_shot=60`，但原轮次脚本只取回文本日志，PNG 需要手动运行 `shot.py`。现在 `cycle.sh` 和 `cycle-resume.sh` 在选择本轮 klog/ramoops 后调用 `decode-shots.sh`，把有效的 `PONG SHOT` 记录解码到本机 `logs/RUN.shotN.png`，同时报告生成数量。成功取回同名轮次的新日志时先清掉旧 PNG；若新日志没有截图记录，输出为 0 张，避免把上次的截图当作本轮画面。PNG 仍留本机，公开前需人工筛选和去除私人内容。

主机用已保存的 g264 日志在临时目录回放：生成 6 张具有 PNG 签名的图片；改为无截图的新日志后重跑，旧 PNG 清零。三脚本语法和 `git diff --check` 通过。该检查只证明自动取证路径，g268 仍无设备截图；截图是每 16 个像素取一个样本并放大，不能代表面板原生清晰度。

约定的 `pong_ipados18_ci_defconfig` 与 `zizhan_ipados18_gpu_defconfig` 再次构建成功，本机日志为 `$ZIZHAN_SRC/ci_kernel-shot-auto-ci.log`、`ci_kernel-shot-auto-gpu.log`，均以 `KERNEL_DONE` 结束。此改动只在 Mac 取证脚本，不改变已打包的 g268 镜像。

## 接续：恢复轮次的 klog 归属

`cycle-resume.sh` 原先默认 `KLOG=1`。如果原轮从 fastboot 启动，`cycle.sh` 不会执行 `klog.py arm`；恢复脚本仍可能把上一次写入、状态为 1 的 pinned klog 当成本轮日志。现在正常轮次启动时先清理同名旧标记，仅在 `klog.py arm` 和 `fastboot boot` 都成功后保存本轮镜像名到本机 `logs/RUN.klog-armed`。恢复脚本只在标记与镜像名匹配时读 klog，否则保留本轮 ramoops；日志取回后删除标记。两条脚本也限制轮次名为字母、数字、下划线或短横线，避免轮次文件路径越界。

主机隔离模拟覆盖：失败的 `boot.sh` 不留下标记；成功启动后有匹配标记；恢复脚本对无标记、其他镜像标记选择 ramoops，对匹配标记选择 klog。模拟分别在预期的等待/启动失败点退出，没有操作手机。脚本语法与差异检查通过。实际设备 klog/ramoops 和截图仍须 g268 上机后核对。

`pong_ipados18_ci_defconfig` 与 `zizhan_ipados18_gpu_defconfig` 均再次构建成功，本机日志为 `$ZIZHAN_SRC/ci_kernel-klog-marker-ci.log`、`ci_kernel-klog-marker-gpu.log`，末尾均为 `KERNEL_DONE`。内核及已打包的 g268 镜像未因这次主机脚本修改而改变。

## 接续：Wi-Fi 首轮日志摘要

`cycle.sh` 与 `cycle-resume.sh` 在取得新日志后运行 `wifi-stage.py`，生成本机 `logs/RUN.wifi-summary.txt`。摘要只列当前驱动实际输出的已观察里程碑（供电请求确认、PCIe/BAR、DMA、MSI 接收器、固件文件读取、MSI 到达、station 启动、扫描结果）及首条错误；若 ramoops 含多次冷启动，只统计最后一次。它不把文件读取、station 启动或扫描结果当成联网、DHCP 或日常可用的证明。摘要生成失败时删除同名旧文件，原始日志仍为判读依据。

主机用五组隔离日志覆盖无 Wi-Fi、供电失败、PCIe 后 ath11k 失败、重复启动只取最后一轮、扫描结果；已有的 g264 非 Wi-Fi 日志输出 `NO_WIFI_LOG`。语法和差异检查通过。g268 仍没有设备日志，此摘要尚未通过真机 Wi-Fi 输出验证。

`pong_ipados18_ci_defconfig` 与 `zizhan_ipados18_gpu_defconfig` 构建通过，本机日志为 `$ZIZHAN_SRC/ci_kernel-wifi-stage-ci.log`、`ci_kernel-wifi-stage-gpu.log`，均以 `KERNEL_DONE` 结束。本次只改 Mac 取证脚本，不改变 g268 内核或镜像。

## 接续：自动启动的 adb 卡住保护

检查 g268 等待器时，手机仍未出现在系统 USB、adb 或 fastboot 列表；`fastboot devices` 在主机上实际阻塞。等待器由 launchd 托管，日志持续只有 `still waiting for USB`，没有执行 `fastboot boot`。已结束本次手动检查中阻塞的进程，等待器继续运行。

`boot.sh` 原先在看不到 fastboot 时直接执行无时限的 `adb reboot bootloader`。现在先确认 Android 为 `device` 状态，校验镜像存在，并将重启命令限制为 30 秒；设备消失、重启失败或超时都会让轮次明确失败。`fastboot-visible.sh` 同时检查命令退出码，避免工具报错却仍输出旧设备文本时误判可见。隔离模拟覆盖无设备、adb 重启失败、adb 阻塞、正常转入 fastboot、缺失镜像，以及 fastboot 非零退出码附带设备文本；结果均符合预期。脚本语法和 `git diff --check` 通过。

约定的 CI、GPU 两配置构建成功，本机日志为 `$ZIZHAN_SRC/ci_kernel-adb-guard-ci.log` 与 `ci_kernel-adb-guard-gpu.log`，均以 `KERNEL_DONE` 结束。这次只改自动轮次工具，不改变 g268 镜像；真实 USB 重连、临时启动、自动返回、klog/ramoops 与截图仍待设备可见后核实。

## 接续：logfs 拉取的断线超时

现有 `pull-logfs.sh` 会先清理同名本机旧镜像，但 Android 侧 `dd` 或随后的 `adb pull` 若在 USB 断线后不返回，整个自动轮次仍可能无限等待。现在导出限制为 60 秒、拉取限制为 120 秒；失败、超时、空文件都会删除本机副本，保留原有的失败退出码。8 MiB logfs 的历史轮次说明这些默认时限足以覆盖正常传输，真实设备耗时仍待下一轮确认。

主机隔离模拟覆盖导出失败/超时、拉取失败/超时、空文件和成功六种情况。前五种均明确失败且没有旧或部分本机镜像；成功只保留新内容。脚本语法和 `git diff --check` 通过。g268 等待器仍只有 USB 等待心跳，未发生设备启动；因此本次没有新的 klog、ramoops 或截图证据。

CI、GPU 两套内核配置再次构建通过，本机日志为 `$ZIZHAN_SRC/ci_kernel-logfs-timeout-ci.log`、`ci_kernel-logfs-timeout-gpu.log`，均以 `KERNEL_DONE` 结束。此项不改变 g268 镜像。

## 接续：zizhan PCI subsystem 识别范围

驱动原先在 QCA6490 主 ID `17cb:1103` 之外，还要求 PCI subsystem ID 等于 Pong 捕获的 `0x010817cb`。现有 zizhan 板级 profile 和保存的输入只证实主 ID，没有证实这个 subsystem 值；把 Pong 的值作为硬门槛可能在真实硬件已经正确枚举时提前退出。两个 PCI 入口现在都保留主 ID 检查，运行时的 `PongWifiPlatform` 还会打印读取到的 subsystem 寄存器值，供首轮日志核对。公开 zizhan DT 的 PCIe0 `int_global_int` 仍映射 SPI140，这项硬编码与当前板级来源一致。

`pong_ipados18_ci_defconfig`、`zizhan_ipados18_gpu_defconfig`、最后构建的 `zizhan_ipados18_wifi_defconfig` 均成功，本机日志为 `$ZIZHAN_SRC/ci_kernel-wifi-subsystem-ci.log`、`ci_kernel-wifi-subsystem-gpu.log`、`ci_kernel-wifi-subsystem-wifi.log`。在 Wi-Fi 配置占用共用构建树时打包的 g269 镜像，命令行 913 字节，含唯一 `pong_wifi=1`、`pong_autoreset=300`、`pong_autoreset_sb=60` 与定时截图参数。只读核对确认新日志标记、PWF2 固件包和 `amss20.bin` 均在 g269 镜像中，且镜像与 g268 不同。

g268 等待器在未启动设备时撤下；g269 等待器已由 launchd 托管，启动日志为 `$ZIZHAN_SRC/logs/g269-when-back.log`。目前手机仍无 USB 枚举，g269 **没有执行 `fastboot boot`**，没有自动返回、内核日志或截图结果。恢复后先核实实际 PCI subsystem 值，再沿供电、PCIe、DMA、MSI、固件、station、扫描找首个失败阶段。

## 接续：PCI subsystem 自动摘要

g269 驱动会打印读到的 PCI subsystem 值，但先前的 `wifi-stage.py` 摘要只列阶段标签。摘要现在提取这一条完整的十六进制值及行号，保留原始日志作为核对依据。用两次冷启动的隔离日志确认只报告最后一次的值；损坏值不被误读，g264 非 Wi-Fi 日志仍输出 `NO_WIFI_LOG`。这项修改只影响回收日志后的主机分析，不改变已打包的 g269 镜像或设备行为。

约定的 CI、GPU 两配置构建成功，本机日志为 `$ZIZHAN_SRC/ci_kernel-wifi-subsystem-summary-ci.log`、`ci_kernel-wifi-subsystem-summary-gpu.log`，末尾均为 `KERNEL_DONE`。g269 等待器继续记录 USB 心跳，仍无设备日志或截图。

再次确认已记录的主机 USB 端口空闲后，执行一次主机侧端口断电重连。随后有界 adb/fastboot 探测仍均不可见，hub 也没有手机枚举；g269 等待器保持运行。这是物理连接状态，不能归因于 g269 内核，因为镜像尚未启动。实体重连操作仍见 `HUMAN_CHECKLIST.md`。
