# 11-06/07 开发内容整理与发布

本次按用户要求整理既有工作区并提交、推送至 `origin/main`。入口提交为 `6d074ef09e5777cecfa23612e626ce2c3878153e`；本轮不启动设备实验，不安装或推广候选 OS。当前任务与后续顺序仍见 [STATUS.md](STATUS.md)。

## 纳入范围

- 启动与采集：稳定 USB-A 枚举和目标核对、有界 Android 完整返回见证、CS4K 分配失败时的 kernel_map 空间诊断。
- 显示与网络：同批首帧分段和 GPU trace 覆盖分析、截图原始像素一致性验证、默认关闭的原生 HTTPS 保持见证。保留旧残图误判的撤回记录，不包含已拒绝的截图驱动诊断或提前 CA flush。
- 日用构建：显式服务存在性策略、源码/镜像身份检查、隔离 catalog staging 和候选审计；统一打包模板与板级映射保留为接口准备，RELEASE-01 仍受上游 22H374 recipe 支持阻塞。
- THERM-01：本机 TSENS/EPSS 输入和默认关闭的一次只读探针。g395 SKIP、g396 的 32 路有效读数、原 supervisor FAIL 与后续 Android 返回 PASS 分别保留。
- POWER-02：完整设计、bring-up 偏好所有权隔离、有限事务模型、PSCI/registry/display 请求只读审计及主机回放。g397 仅通过查询与正常返回；真实锁屏、息屏、系统休眠、恢复和功耗资格保持未验。
- 对应计划、日期记录、脱敏证据、能力记录及 Daily-use host regressions 工作流。

## 发布前核对

2026-11-07 本机重新运行：

```sh
python3 tools/zizhan/mac/check-daily-use-host.py
python3 tools/zizhan/mac/check-display-host.py --cc clang
python3 tools/zizhan/mac/check-wifi-host.py --cc clang --shared "$ZIZHAN_SRC/sheng/drivers/Ath12kWifi"
python3 tools/zizhan/tests/usb-attach.py
```

四项均通过；日用主机回归为 64 项，包含实际 CoreFoundation 回放。Python 语法、shell 语法、公共相对链接、私有路径/凭据模式扫描及普通文件 diff 空白检查通过；补丁上下文的空格前缀单独按 patch 格式检查，保留其合法语法。固定版本 Porting record 检查器的结构和相对入口提交的 changed-area 检查均通过，覆盖 14 areas。

发布运行时代码逐文件核对 g397 冻结源码清单一致。已有该输入的精简 CI 与完整默认 GPU/Wi-Fi/亮度内核 `KERNEL_DONE`；本次仅整理文档，不重复构建相同内核。两种配置的本机构建与云端 CI 分开记录。

发布前进程检查未发现 cycle、安装器或 waiter。本次未查询手机，因此实时设备状态未知；最后设备观察为 g397 返回 Android，实际 OS 最后核验仍为 OS370，BASE-01 与隔离 UI 候选未安装。

## 发布与云端验证

主提交 `45c7037b5d42c203b2a624f83437bf49f29792a0` 已正常推送至 `origin/main`。该 SHA 的首轮结果：

| 工作流 | 运行 ID | 当前结论 |
| --- | --- | --- |
| Porting record | 42345399532 | SUCCESS |
| Display host regressions | 42345399483 | SUCCESS |
| Wi-Fi host regressions | 42345399554 | SUCCESS |
| Daily-use host regressions | 42345399573 | FAIL，电源清单测试依赖本机私有 DTB |
| zizhan macOS build | 42345399626 | RUNNING，待精确 SHA 收口 |

### 干净 checkout 回归修正

首轮 Daily-use host 暴露 `tests/power-inventory.py` 的测试输入错误：四项回放直接读取仓库外 `logs/daily-use-1106/entry-live.dtb`，本机存在而云端不存在。原运行 FAIL 保留；这是回归可移植性问题，不是新的硬件故障。

修正为代码构造的最小 FDT，以人工地址、名称和时序覆盖八核、四 idle state、域依赖、TCS 顺序及未决 RTC IRQ 形状，不发布原始 DTB。增加外来板型、缺失 CPU 和循环域拒绝控制。隔离临时 checkout 中，旧测试因缺少私有文件按预期失败，修正后七项全部通过；完整本机日用回归增加为 67 项并通过。内核运行时代码未改，不重建相同内核。

修正提交各自独立跟踪五个工作流；不能继承其他 SHA 的 SUCCESS。Linux 上缺少 CoreFoundation 的六项测试明确 SKIP，本机 macOS 的实际 CF 回放已通过。本机原始日志、源码快照和操作状态保留在仓库外。

修正提交 `314744f33cb10303edc78e9cbb66b4aa4409c949` 已推送：Daily-use host `42345638369`、Display `42345638340`、Wi-Fi `42345638381` 均 SUCCESS；macOS `42345638365` 后被下一次同分支提交取消。Porting record `42345638342` FAIL，原因是旧 network area 用 `tools/zizhan/tests/**` 认领了所有测试，电源测试修改因而要求同时更新网络记录。

本次进一步把 network 的测试范围收窄为 `wifi-*`，将电源清单及 `power-*` 测试归入 power area，并同步两个 area 的 findings。旧 checker 按前后 ownership 的并集检查，因此迁移说明也保留在 network；没有为无关测试伪造网络功能结论。主机预检改为显式对比当前内容与基准提交的 changed paths，提交后再运行真实 `--base` 检查；此前单独 `--base HEAD` 只覆盖已提交内容，不能证明 dirty diff 的 area 同步。

发布修正 `477417e2d8daf9efbb74e78dc98f40a55f8fbe96` 已推送，五项 CI 全部 SUCCESS：Porting record `42345816118`、Daily-use host `42345816142`、Display host `42345816064`、Wi-Fi host `42345816069`、macOS build `42345816283`。macOS 磁盘检查、精简内核编译和 boot image 阶段均完成。该 SHA 的最终状态快照位于本机 `logs/publish-20261107/477417e-ci-runs.json`；先前 SHA 的 CI 结论不继承。
