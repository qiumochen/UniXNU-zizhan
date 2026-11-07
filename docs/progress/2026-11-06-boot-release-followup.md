# BOOT-01 与 RELEASE-01 后续准备

## BOOT-01

g384 首次启动时，CS4K 共享缓存签名存储请求 `1949696` bytes 返回 `KERN_NO_SPACE`，随后 initproc 因 dyld cache 不可用退出。现有日志只有请求大小、code limit 和页数，不能区分 kernel_map 地址边界、总空闲空间不足、最大空洞不足或其他 map 约束。

本轮只增加失败路径诊断：调用已有 `vm_map_sizes(kernel_map, ...)`，记录 map 总大小、总空闲和最大连续空洞；保留原始返回值和 `ENOMEM` 错误处理。没有 NOFAIL、签名绕过、伪造空间或修改分配策略。

主机验证：完整精简 CI 配置与完整 GPU/Wi-Fi 默认配置均 `KERNEL_DONE`。设备验证 g393 使用新的完整默认内核，单次有界 `fastboot boot`，OS370 不写入；通过要求 fresh klog 和 Android 完整返回。如果相同 CS4K 分配再次失败，应能提供 kernel_map 总空闲/最大洞；如果未复现，只登记覆盖，不记修复。不得同条件自动重跑。

## RELEASE-01

上游统一入口要求维护仓库提供与模板一致的 `scripts/package.sh`、`board.toml [package]`、`[package.defconfig]`、profile 输入和 `package.yml`。本轮已将上游固定模板复制到 `scripts/package.sh`，并在 `board.toml` 增加 `ipados-18.7.10-22H374` 到 `zizhan_ipados18_gpu_defconfig` 的映射。模板逐字比较通过，TOML 映射及 defconfig 文件存在性通过。

完整接入尚待发布器/构建模型支持：当前本机 `../UniXNU` checkout 为 `0fc0fc1`，没有 `release` 命令；审查用 `e67a79e` checkout 有 `release` 命令，但只包含 17、26、27 profile，`--check --os ipados-18.7.10-22H374 --no-deps` 明确拒绝 22H374。即使补 profile，发布器的固定 build/pack 顺序也会走通用 kernel prelink、payload 和 bootimg，而本设备实际使用 `build-pong-format2.py`、`build-ipados18-prelinked-boot.py` 与 UFS layout。把这些自定义步骤塞进设备 TOML 目前没有被发布器支持的字段。

因此本轮没有添加 `.github/workflows/package.yml`：当前接口无法令其执行真实打包，配置成 check-only 也不满足 package action 的用途。下一步需要上游为 Darwin24 设备提供配置驱动的 kernel/prelink/payload/boot recipe（或等价扩展点），然后补齐本机 `unixnu.local.toml` 映射、22H374 release check、workflow 和完整本地产物清单验证。保留现有 `ci_kernel.sh` / `ci_boot_ufs.sh` 作为当前已验证构建链；没有改变设备、私有输入或现有工作区 WIP。

## g393 轮次设计

- 单变量：只增加 CS4K 分配失败时的 `kernel_map` 总大小/空闲/最大洞摘要；原 OS、extent、完整默认 GPU/Wi-Fi 内核、helper、UI 均保持控制身份。
- 包：`out/boot-g393-boot-map/pong-xnu-initial.img`；`pong_touch=0`、`pong_adsp=0`、300 秒 autoreset、180 秒 SpringBoard deadline。
- 验收：新的 klog、启动退出结果、Android `boot_completed=1`；如果分配失败，需看到新摘要；若未复现不记修复。一次运行，不自动重试。
- 安全：OS/UFS 不写；18 分钟硬截止、cycle 等待420秒、caffeinate、独占设备锁；失败时仅明确可见 fastboot 才允许一次 reboot，无 transport 不发恢复命令。

## g393 结果

- USB-A 枚举、临时启动、autoreset、ramoops 回收和 Android `boot_completed=1` 全部通过；fresh klog 1,830,726 bytes，deadline 收回，期间两次受控端口恢复。OS370/UFS未改。
- 本轮未观察到 `PONG CS4K: storage allocation failed` 或 panic，因此也没有输出新增 map 摘要。结论只是一轮完整profile启动覆盖；BOOT-01 真实故障未复现、根因未确定、未修复。保留日志并停止同条件重跑。
- 日志：仓库外 `logs/g393.supervisor.json`、`logs/g393.cycle.out`、`logs/g393-boot-map.txt`、`logs/g393-boot-map.klog.txt`、`logs/usb-attach-pl16lu_2.json`。
