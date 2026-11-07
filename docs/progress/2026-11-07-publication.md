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

本条在提交前建立，尚无本次发布 SHA 的云端结论。推送后按精确 SHA 跟踪 Porting record、Display host、Wi-Fi host、Daily-use host 和 macOS build；已通过的历史运行不能替代本次结果。运行 ID 与结论在收口时追加，本机原始日志、源码快照和操作状态继续保留在仓库外。
