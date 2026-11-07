# POWER-02：首批无人值守实现

用户授权按[完整方案](../plan/2026-11-07-lockscreen-suspend-design.md)顺序实施并自行闭环，人工验证留到最后。入口为 `6d074ef09e5777cecfa23612e626ce2c3878153e` 加既有 WIP；无占用设备的cycle/installer/waiter或内核构建。动态任务与待验条目统一在 [STATUS.md](STATUS.md)。

本批先做bring-up隔离、原生display power只读观察、电源事务模型、板级输入与固件能力只读查询。面板实际掉电、真正系统睡眠、物理键/Hall及其他人工场景不在本批无人值守验收范围。候选和原OS370分开，未验功能不推广基线。

## 原生UI bring-up隔离

`zizhan_bringup=1` 才允许旧的启动亮屏/300秒防休眠/进入主屏动作；默认不调度 `wake_home`，`pong_skip_setup` 不再隐式授权这些动作。此改动还没有安装到手机，当前OS370行为未改变。

`zizhan-bringup18.h` 对 `SBDisableAutoDim` 使用单独的v1所有权marker，保存原始值或原先不存在状态。退出bring-up只在当前仍为本组件写入的Boolean true时恢复原值；如果原生/其他来源已经修改当前值，保留该修改并释放自己的marker。未知marker不覆盖。持久化失败时回滚两个键，不报告迁移成功或调度亮屏。

旧版本未标记的true默认保留并明确SKIP；显式 `zizhan_autodim_restore=1` 才移除这个旧Boolean true，不清理其他偏好，不把false、字符串或未知值当成旧写入。bring-up与legacy迁移同时请求时跳过，避免模糊所有权。人工批次若使用旧Data，需要显式选择迁移；仅更换代码不能视为旧偏好已恢复。

实际planner和UI应用/回滚函数的6项CoreFoundation主机回放通过：默认保留、精确legacy迁移、原始类型/缺失恢复、重复进入、外部修改、未知marker/冲突以及sync失败回滚。回放使用内存store，不修改宿主SpringBoard偏好。Linux无CoreFoundation时这组测试明确SKIP，不以可移植测试代替Apple框架。

隔离UI dylib已完成arm64 iOS18编译、4K页签名和严格codesign检查，SHA256 `2c4c5fa6297a737febfaadfe9d2cd3ba17a28c235ab14fe0ad7537df56fa0e5d`。这只是独立主机候选，没有写入canonical prepared、OS镜像或手机；自动锁屏/解锁行为仍待人工实际验收。

## 电源事务与只读审计候选

`ZizhanPowerTransaction.h` 是生产候选的纯状态/票据模型：owner注册、依赖拓扑、generation、有限deadline、单个在途票据、成功/拒绝/不确定三类回执、逆序回滚、wake epoch竞态和失败隔离。它不调用硬件、不创建虚假PM能力；7个ASan/UBSan主机场景通过。未知或超时回执会保留in-flight owner并阻止新周期，确保不能把未完成DMA当成已恢复。

`ZizhanPowerQuery.h`/`ZizhanPowerAudit.h` 为默认关闭的PSC​​I/registry只读审计：启动时仅在显式 `zizhan_power_audit=1` 且ADT conduit与已建立PSCI conduit一致时查询 VERSION 和 FEATURES；不调用CPU_SUSPEND/SYSTEM_SUSPEND，不切换OSI/PC、不写TCS、RTC或wake配置。查询feature的负返回如实记录；状态ID、真实PM tree和wake仍为UNQUALIFIED。

MiniIOMobileFramebuffer的 `RequestPowerChange` 现在在兼容成功返回之前调用有界、默认关闭的 `zizhan_power_observe` 观察器；它只记录请求标量/结构长度并输出 `hardware-transition=UNIMPLEMENTED`，使用32条限额，不改变硬件或ABI。没有任何显示掉电资格。

live FDT 已由 `tools/zizhan/mac/power-inventory.py` 离线生成 `work/power02-1107/live-power-inputs.json`：8 CPU、4 idle state、9 domain、2 RSC、1 RTC、1 PDC、PSCI=SMC；物理域/调度域分开，TCS和RTC IRQ的未决父域关系保留为UNQUALIFIED。它是板级输入清单，不是执行计划的授权。

g397 是首个无人值守审计轮次，采用 OS370、完整GPU/Wi-Fi/亮度profile、`zizhan_power_audit=1,zizhan_power_observe=1`，touch/audio/adsp关闭，autoreset300/SB180；仅验证正常启动、fresh日志、查询日志和Android返回。它不尝试锁屏、熄屏或任何系统电源状态。当前状态实时以本机handoff和`logs/g397.*`为准。

### g397 结果

g397 传输/临时启动、228秒后自动返回、1,495,019-byte fresh deadline klog、Android `boot_completed=1` 和无panic通过；OS370未写。审计得到：PSCI 1.1、SMC；CPU_SUSPEND FEATURES raw=3，SYSTEM_SUSPEND raw=0，SET_SUSPEND_MODE raw=0。这里只记录查询返回值，未调用CPU_SUSPEND/SYSTEM_SUSPEND，未切换模式。

IORegistry查询看到root domain和11个目标owner（遍历90项，限额256/16），所有电源契约仍标记UNQUALIFIED。MiniIOMobileFramebuffer收集1个power request，记录 `hardware-transition=UNIMPLEMENTED`；没有执行显示硬件状态变化。`g397-result.json`和脱敏[证据摘录](../evidence/2026-11-07-power-audit.txt)保留完整边界。该轮是 query-only/runtime logging PASS，不是锁屏、息屏、硬件休眠或恢复PASS。

主机构建结果：完整默认与精简CI内核均 `KERNEL_DONE`；新增日用host runner为64项PASS（含bring-up、事务模型、PSCI查询、power inventory），显示/固定输入Wi-Fi回归仍通过。g397之前的审计镜像和当前runtime源身份已分别保存；没有安装、提交、推送或云端CI。

## 其余首批项

后续代码、主机/内核构建和只读设备结果在完成后分别追加。尚未开始的设备轮次不登记RUNNING；模型检查不是硬件PM完成，能力查询成功也不是睡眠支持通过。
