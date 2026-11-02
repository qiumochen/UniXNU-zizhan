# GPU fence 停滞诊断（2026-11-02）

## 原判断和复查原因

g239–g252 的截止日志经常显示 `PONG FENCE at the deadline`：最新发出的 fence 比 GPU 已写入的 fence 大 1，看起来像永久停滞。ASTC 开、关两组都有这个现象，而且扫描输出的零拷贝 fence 超时一直是 0。为确认究竟有没有长时间未完成的 GPU 工作，本轮增加了两个诊断点：

- `pong_gtrace_delay` 控制时间线在 SpringBoard 后多少秒启动，默认 20 秒，保持原有行为；测试可把记录窗口移到自动复位之前。
- deadline 日志记录最新 GPU 提交的 fence 和时间，并同时打印最后四笔 IB 的提交进程。

## 设备结果

g253 在 SpringBoard 后 48 秒启动 8 秒时间线，并保留逐 IB 时间戳：

- 592 笔提交全部完成，576 次零拷贝翻页继续推进，时间线没有丢事件。
- 零拷贝翻页的 fence 超时为 0。
- deadline 快照为 GPU 已写 2884、最新发出 2885。

g254 在 SpringBoard 后 53 秒启动 6 秒时间线，并输出 deadline fence 年龄：

- 403 笔提交全部完成，383 次零拷贝翻页继续推进，时间线没有丢事件。
- 零拷贝翻页的 fence 超时为 0。
- deadline 快照为 GPU 已写 2602、最新发出 2603；最新提交发生在截止前 **26 ms**，最后四笔都是 backboardd。

## 结论

目前没有证据表明这些轮次发生了持续数秒的 GPU fence 停滞。自动复位发生时，backboardd 的下一帧已经正常提交但尚未完成，因此 deadline 总会自然看到“差 1 个 fence”。以后判断真实停滞必须看提交年龄、最后一次成功扫描输出距今多久，以及界面是否停止更新，不能只看 deadline 的 submitted/retired 差值。

2026-10-30 记录过的整机 GPU 丢失是另一组更早有直接证据的现象：扫描输出拷贝等待超过 2 秒、worker 报失败、界面冻结且出现 `DeviceLost`。那类故障仍需保留现有 poison 和 stuck-copy 检测；本轮只是撤掉由 deadline 快照产生的错误归因。

## 验证

- `zizhan_ipados18_gpu_defconfig` 构建成功，日志在本地 `$ZIZHAN_SRC/ci_kernel-fence-age.log`。
- `pong_ipados18_ci_defconfig` 构建成功，日志在本地 `$ZIZHAN_SRC/ci_kernel-fence-age-ci.log`。
- g253、g254 都用临时启动镜像自动运行并返回 Android；日志留在本地 `$ZIZHAN_SRC/logs/g253.txt`、`g254.txt`。

## 后续

照片 App 的 `PXPixelSizeAreaIsZero` 崩溃与 SharedEvent 监听失败仍待处理。真实 GPU 停顿时再用提交年龄和最后扫描输出时间确认，不把自动复位截断的正常在途帧算作故障。
