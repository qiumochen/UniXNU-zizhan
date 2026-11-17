# POWER-01 电池健康页输入调查（2026-11-17）

状态：**只读分析**。从本机 IPSW 提取了 `powerd`、`thermalmonitord` 与 iPad7,11（J171AP）热表；没有设备轮、没有 fastboot、没有安装/写入。提取物只在 `$ZIZHAN_SRC/work/codex-1117/apple/`，不进入 git。

证据级别：

- **反汇编确认**：`ipsw macho disass` / `otool -oV` 读到的指令、地址、分支。
- **字符串推断**：二进制中的 key、日志、类/方法名；调用链未完全反汇编。
- **推测**：需要设备日志或更多反汇编才能定的因果。

## 1. 结论摘要

健康页 spinner 跟随 `batteryDataUnavailable`，它由两项独立输入决定；目前两项都不完整：

1. **powerd 的 `Battery Service State`**：`sub_10000c474` 先读 `Battery Service Flags` 并掩码 `0xC00000`；再读 `NominalChargeCapacity` / `DesignCapacity` 算百分比；还要与持久化的 `Battery Serial` 对得上。我们的 `AppleSmartBattery` 发布了容量、循环数、Serial 和 `BatteryData`，但**没有发布 `Battery Service Flags`、`Battery Service State`、`Battery Serial`、`calibration0`、`Trusted LifeTime Max WRdc`**。没有 `Battery Service Flags` 时，函数会走“电池序列号/清空健康 key”的路径，`Battery Service State` 就不会出现。
   另外 `sub_10000e270` 会在 `flags & 0xC00000` 时打印 `Invalid auth condition` 并清掉这两位；如果这条 sanitizer 运行，即使输入过 flags，健康计算也会被跳过。**这是反汇编确认的代码路径，是否命中需设备日志。**
2. **thermalmonitord 的 `com.apple.thermalmonitor.ageAwareMitigationState`**：它由 `AgingController` 发布，条件是 `UPOCount`、`CurrentWRa` 与 `UPOMitigationsThresholdWRa` 的比较；**不是**直接由温度传感器数组发布。我们的电池驱动不发布 `UPOCount` / `WRa`/`BatteryData` 里的对应项，所以 AgingController 没有真实输入。J171AP 的 33 路传感器（29 路 watchdog）是 thermalmonitord 控制轮的另一条依赖，不能拿 data-a-live 的 D22 传感器集直接替代。

因此：**只补温度 HID 不会自动修好 powerd 的 state；只补 `Battery Service Flags` 也不会让 ageAware 出现。** 两项都要先找到真实输入，不能伪造。

## 2. 方法与材料

| 材料 | 用法 |
| --- | --- |
| `$ZIZHAN_SRC/../apple.noindex/iPad_10.2_18.7.10_22H374_Restore.ipsw` | `ipsw extract --files --pattern` 取 `powerd`、`thermalmonitord`、`J171AP-Info.plist` |
| `ipsw macho disass --vaddr` / `otool -tvV` / `otool -oV` | 函数、方法 IMP、分支 |
| `ipsw class-dump` | `AgingController`、`HidSensors` 的类/方法/ivar |
| `plutil` / `plistlib` | iPad7,11 = J171AP 的 `Sensors`、`generalProductConfig` |
| 仓库源码 | `drivers/PongPower/PongBattery.cpp` 实际发布的属性 |

## 3. powerd：`Battery Service State`

### 3.1 发布函数和分支（反汇编确认）

函数入口 `sub_10000c474`（0x10000c474）。关键顺序：

| 地址 | 行为 |
| --- | --- |
| `0x10000c4cc-0x10000c4e4` | 从输入字典读 `Battery Service Flags`，`intValue`，`and w23, w0, #0xc00000`（bits 22/23） |
| `0x10000c4ec` | 掩码为 0 → 跳到 `0x10000c634`（序列号/清空路径） |
| `0x10000c500-0x10000c548` | 掩码非 0 → 读 `NominalChargeCapacity`、`DesignCapacity` |
| `0x10000c550-0x10000c578` | 两者任一为 0 → `0x10000c748`；否则 `nominal/design*100`，`fcvtps` 取整 |
| `0x10000c57c-0x10000c584` | 百分比必须落在 1..150（`w21-1 < 0x96`）；越界 → `0x10000c768` |
| `0x10000c5d8-0x10000c608` | 用 `CFNumberCreate` 写 `Battery Service State`（值在 `[sp+0x40]`，初值 0）到输出字典 |
| `0x10000c788-0x10000c810` | 复制/写 `Battery Service State`、`Battery Service Flags`、`Maximum Capacity Percent` |
| `0x10000c634-0x10000c728` | 读 `Serial` 与 `Battery Serial`；不匹配/缺失时 `CFDictionaryRemoveValue` 删掉 `Maximum Capacity Percent`、`CycleCount`、`Battery Service State`、`calibration0`、`Trusted LifeTime Max WRdc` |
| `0x10000c85c-0x10000c948` | 没有可用序列号时把 flags 设为 3 并清掉 `0xC00` 位，继续走持久化 |
| `0x10000c8d0-0x10000c8fc` | 健康数据持久化到 `com.apple.batteryhealthdata` / `/var/MobileSoftwareUpdate/Hardware/Battery/` |

另一个函数 `sub_10000e270`（0x10000e270）会先复制电池数据字典，读同一个 `Battery Service Flags`；若 `flags & 0xC00000` 非零，它打印 `Invalid auth condition: recoveredflags:0x%x`，并用 `and w2, w21, #0xff3fffff` **清掉 0xC00000 这两位**后再写回。这说明 `0xC00000` 不是普通容量位，而是 powerd 认为需要“恢复”的认证/服务条件；如果这条 sanitizer 跑了，后面的 `sub_10000c474` 就会看到 0，跳过健康计算。g233 的 `0x800003` 是否走到这条路径，需要设备 `log show` 里的 `Invalid auth condition` 来确认。**反汇编确认代码；因果为推测。**

关键 key（字符串 + 反汇编地址）：

- `Battery Service Flags`（字符串 0x6f7a0；对象地址未单独解析，反汇编按 key 读取）
- `Battery Service State`（字符串 0x6fba5；CFString 对象 0x10008ec50）
- `NominalChargeCapacity`、`DesignCapacity`、`CycleCount`、`Serial`
- `Battery Serial`（字符串 0x70020）、`calibration0`（0x6f7b6）、`Trusted LifeTime Max WRdc`（0x6dcce）
- `BatteryDataCollectionReady`（0x6f313）、`BatteryDataCollectionAgent`（0x84f9c）；日志 `Couldn't read BatteryDataCollectionReady from BTM. Can't start agent %@`（0x7e03a）

`BatteryDataCollectionReady` 这一段是**字符串推断**：powerd 在 BTM/BatteryDataCollection 未 ready 时不启动 agent；该 agent 与 `Battery Service State` 计算的具体调用链尚未逐行反汇编。

### 3.2 我们驱动实际发布的属性（源码）

`drivers/PongPower/PongBattery.cpp:145-167`：

| 已发布 | 值 |
| --- | --- |
| `AppleRawMaxCapacity`、`NominalChargeCapacity` | `full_mah` |
| `DesignCapacity` | `design_mah` |
| `MaximumCapacityPercent` | `full_mah*100/design_mah` |
| `CycleCount` | `s.cycles` |
| `Serial` | 固定 `ZIZHAN-BM4V` |
| `BatteryData` | `DesignCapacity`、`CycleCount`、`NominalChargeCapacity`、`StateOfCharge`、`Voltage` |

**没有**：`Battery Service Flags`、`Battery Service State`、`Battery Serial`、`calibration0`、`Trusted LifeTime Max WRdc`、`ncc`、`BatteryDataCollection*`。

因此按反汇编的分支，`Battery Service Flags` 缺失会让 `0x10000c4ec` 直接跳到序列号/清空路径；即使 `Serial` 存在，`Battery Serial` 也可能没有，健康 key 会被删掉。g233 打印的 `getBatteryHealthServiceFlags=0x800003` 是 powerd/IOPS 输出侧的观测值，**不能**据此认定输入侧已有 `Battery Service Flags`；需要设备 ioreg 核对。

## 4. thermalmonitord：`ageAwareMitigationState`

### 4.1 AgingController（反汇编确认）

`ipsw class-dump` 的 `AgingController`：

- ivar：`_mitigationStateToken`（offset 8）、`_UPOAgingMitigationsThreshold`（0x30）、`_mitigationState`（0x38）、`_defaults`（0x40）。
- `initWithParams:currentWRa:currentUPOCount:`（0x100021798）：
  - 读 `params[UPOMitigationsThresholdWRa]`（字符串 0x6fb7b），再按 `currentWRa` 的字符串 key 取阈值（0x1000217fc-0x100021828）。
  - 缺失时走默认值并打印 `No upo mitigations start threshold found. Falling back to default value`（0x100021858）。
  - 设定 `_UPOAgingMitigationsThreshold`（0x100021894）。
- `takeActionOnUPO:forWRa:`（0x1000220e4）：
  - 读 defaults `BootUUID`（0x100022120）、`UPOCount`（0x100022140）。
  - 同一 boot UUID 只处理一次；`upo > 0` 时比较 `_UPOAgingMitigationsThreshold` 与 `upo`（0x100022214）。
  - `threshold <= upo` → `w2=1`（0x100022284），调 `updateMitigationStateAndNotify:`；否则 `w2=2`（0x1000222b0）。
  - 打印 `Current WRa %d less than threshold %d. Skipping UPO`、`Unchanged upo count %d. Skipping UPO`、`Unchanged boot UUID %@. Skipping UPO` 等。
- `updateMitigationStateAndNotify:`（0x100022378）：
  - `setMitigationState:`（0x1000223e4-0x1000223ec）更新 ivar；
  - `notify_set_state(_mitigationStateToken, [self mitigationState])`（0x100022410-0x100022424）；
  - `notify_post` 的 CFString 对象在 0x100084c10，指向 `com.apple.thermalmonitor.ageAwareMitigationState`（字符串 0x6fc38）；
  - 日志 `%s Notification posted for state %d`（0x10007409d 附近）。
- `shouldEnableAgeAwarenessForTableDrivenControllers`（0x10002235c）只是 `return [self mitigationState] == 1`。
- `notifyPPMOfMitigationState:` 与 `CPMSHelper` 是另一条 PPM/CPMS 路径；上面的 notify 发布不依赖它（CPMS 的耦合在 PPM 通知侧，需单独验证）。

**结论**：ageAware 的发布条件不是“温度传感器齐全”，而是 **AgingController 拿到了非零 `UPOCount` 和 `CurrentWRa`，并按 `UPOMitigationsThresholdWRa` 得到阈值**。有 UPO/WRa 时，state=1/2 会写入 notify；没有这些输入时，state 保持 0/不发布，`getManagementState` 就是 0。

### 4.2 输入来源（字符串推断）

| 字符串 / 常量 | 含义 |
| --- | --- |
| `UPOCount`（0x?）/ `kBatteryUPOCountKey`（0x6f264） | 电池数据里的 UPO 计数 |
| `CurrentWRa`（0x6fb70）/ `kBatteryWRaKey`（0x6f212） | 电池的加权 Ra |
| `IOPMUBootUPOCounter`（0x711b7） | 启动 UPO 计数 |
| `getIntValueForKeyFromBatteryData(const NSString *)`（0x710fe） | 从 `BatteryData` 取整数的访问器 |
| `batteryUPOCountRef NULL`（0x77d73） | UPO 引用缺失的错误 |
| `BatteryData`（0x710f2） | 电池数据子字典 |
| `com.apple.system.batteryHealth.UPOAware` | 相关健康状态 |

`getIntValueForKeyFromBatteryData`（0x10005201c）在 key/value 缺失时返回 0 并打印 `%s failed`；`getBatteryWeightedRaValue`（0x100052154）和 `batteryRaTableRawData`（0x1000521e4）会分别打印 `WeightedRa value NULL`、`WeightedRa array NULL`、`batteryRaTableRawData bad size`。这确认了 **WRa 来自电池数据表，缺失时函数返回空/0**；`UPOCount` 走同一个 `getIntValueForKeyFromBatteryData` 访问器。精确 key 名仍是字符串推断。我们的 `BatteryData` 只有 5 个键，**没有 UPOCount/WRa**，所以 AgingController 没有可用的真实输入。

### 4.3 iPad7,11（J171AP）热表：33 路传感器

`System/Library/ThermalMonitor/J171AP-Info.plist`：

- `Sensors` 共 **33** 项；`requiresWatchDog` 默认 true，只有 4 个 `synthSensor` 项为 false。即 **29 路 watchdog**。
- `generalProductConfig`：`shouldDispatchVirtualTemp=true`、`needsFastDieControl=true`、`monitorsCameraSensors=true`、`needsLifetimeServo=true`、`solarContextType=1`、`needsPushingTSFDtoDisplayDriver=true`。

| 组 | 4CC / matchName | watchdog |
| --- | --- | --- |
| 电池 | `TG0B` / `gas gauge battery` | 是 |
| PMU | `TP1d`-`TP8d` / `PMU tdev1`-`PMU tdev8` | 是 |
| PMU 校准/结温 | `TP0Z` / `PMU tcal`；`TP0j` / `PMU tjunc` | 是 |
| SoC die 0-2 | `Th0i/a/x`、`Th1i/a/x`、`Th2i/a/x` / `PMGR SOC Die Temp Sensor0-2`（原始/平均/最大） | 是 |
| ACC 0-2 | `Tc0i/a/x`、`Tc1i/a/x`、`Tc2i/a/x` / `ACC Temp Sensor0-2`（原始/平均/最大） | 是 |
| 合成 | `SS0F`/`FrontCameraDie`、`SS0C`/`RearCameraDie`、`TSFD`/`CGCenterSkin`、`TSBH`/`BCH7Skin` | 否 |

`HidSensors`（class-dump）相关方法：`copySensorArray`、`getHIDSensorDict`、`temperatureForKey:`、`sensorWatchdogMask`、`isSensorDataValid`、`sendVirtualTemp:temperature:`；错误字符串 `Could not discover all sensors in HID`（0x732b2）、`partial sensors: count %d, current %llx, mask %llx, expected %llx`（0x77017）。所以 J171AP 的控制轮要求这 29 路 watchdog 都有值；缺一路会报 partial，控制轮不会完整。

### 4.4 与 data-a-live D22 的对比

`data-a-live/wsl/PandoraThermalHID.cpp` 的 HID 服务是 `PrimaryUsagePage 0xff00 / Usage 5`，默认 ReportInterval 1 s；`pandora_therm.c` 的 `pt_hid` 名称是 **D22（iPhone X）** 的集合：

- 有：PMU tdev1-5、tcal、tjunc、TG0B/TG0V、NAND、PMGR SOC Die 0-3、ACC 0-2、mic1/mic2。
- 没有：J171AP 需要的 **PMU tdev6-8**；名称/分组也不同（D22 的 die 分组与 J171AP 的 Th0i/a/x、Th1i/a/x、Th2i/a/x 不是同一套）。

因此 data-a-live 的 D22 表**不能直接当 J171AP 用**；要提供的是 J171AP 的 `matchName` 集合。`PongBattery` 的 `TG0B` 可由 gauge 温度提供，但其余 PMU/SoC/ACC 需要真实 HID 服务。

## 5. 已有 / 缺失对照

| 输入 | powerd / thermalmonitord 用途 | 我们当前 | 结论 |
| --- | --- | --- | --- |
| `Battery Service Flags` | powerd 的 `0xC00000` 门控 | **无** | 首要缺口；g233 的 0x800003 是输出侧观测量，需 ioreg 确认输入 |
| `Battery Service State` | powerd 输出 | **无** | 由 powerd 计算，不能自己伪造 |
| `Battery Serial` | 与 `Serial` 比对，缺失会清空健康 key | **无** | 需持久化/真实序列号路径 |
| `calibration0`、`Trusted LifeTime Max WRdc` | powerd 健康字典 | **无** | 是否必需取决于分支；先观察 |
| `BatteryData` 的 UPO/WRa | AgingController 的 `UPOCount`/`CurrentWRa` | **无**（只有 5 键） | ageAware 的直接缺口 |
| `BatteryDataCollectionReady` | powerd 启动 BatteryDataCollectionAgent | 未知（字符串推断） | 用 ioreg/log 确认 BTM 是否 ready |
| J171AP 29 路 watchdog 传感器 | thermalmonitord 控制轮 | **无 HID 温度服务** | 与 ageAware 发布路径分开；控制轮需要 |
| `BootUUID`/`UPOCount` defaults | AgingController 每 boot 去重 | 由 defaults 产生，不来自电池 | 不是电池属性 |

## 6. 真实来源对照（Android 只读，2026-11-17）

只读采集：`/sys/class/power_supply/{battery,usb,wireless,...}`（`su -c cat`）与 `dumpsys battery`。本文只记录属性名、是否存在、单位和值域/格式；**不记录序列号等唯一标识**（实际属性里也没有 serial/manufacturer 属性）。

### 6.1 Android 侧真实属性

| 属性 | 是否存在 | 格式/说明 |
| --- | --- | --- |
| `capacity` | 存在 | 百分比 |
| `charge_full` | 存在 | µAh（满充容量，随老化变化） |
| `charge_full_design` | 存在 | µAh（设计容量） |
| `charge_counter` | 存在 | 电荷计数 |
| `cycle_count` | 存在 | 整数循环数 |
| `health` | 存在 | 字符串枚举（Android 健康，不是 Apple `Battery Service Flags`） |
| `model_name` | 存在 | 型号字符串（不是唯一序列号） |
| `technology` | 存在 | 化学体系字符串 |
| `status` | 存在 | 充电状态字符串 |
| `temp` | 存在 | 0.1°C |
| `voltage_now` / `voltage_max` / `voltage_ocv` | 存在 | µV |
| `present` | 存在 | 0/1 |
| `charge_type` | 存在 | 字符串（本次为 N/A） |

`usb` 有 `online`、`voltage_now`、`current_now`、`input_current_limit`、`type`、`usb_type`；`wireless` 有 `online`、`present`、`voltage_now`、`current_now`。`dumpsys battery` 有 health、level、voltage、temperature、technology 字段；没有 `serial`、`manufacturer`、`manufacture date`、`authentic`/`auth`、`Battery Service Flags` 或 `UPOCount`/`WRa` 属性。

### 6.2 battmgr 属性编号（源码）

`drivers/PongPower/ShengBattmgr.c:16-49` 的 `kFields` 与文件头注释（Xiaomi 编号，12 起后移一位）：

| battmgr 字段 | opcode / property | 当前是否请求 |
| --- | --- | --- |
| `BmStatus` | BATTERY_GET 0 | 是 |
| `BmCapacity` | BATTERY_GET 4 | 是 |
| `BmVoltage` | BATTERY_GET 7 | 是 |
| `BmCurrent` | BATTERY_GET 9 | 是 |
| `BmTemperature` | BATTERY_GET 13 | 是 |
| `BmCharge` | BATTERY_GET 15 | 是 |
| `BmCycles` | BATTERY_GET 16 | 是 |
| `BmDesign` | BATTERY_GET 17 | 是（慢速） |
| `BmFull` | BATTERY_GET 18 | 是（慢速） |
| `BmTimeToFull` / `BmTimeToEmpty` | BATTERY_GET 20 / 21 | 是 |
| `BmAuthentic` | BOARD_GET 0x03 | 是（慢速） |
| `BmSlaveAuthentic` | BOARD_GET 0xa5 | 是（慢速） |
| `BmPdVerified` | BOARD_GET 0x18 | 是 |
| 文件头注释中的 health 1、SOH 5、model 19 | BATTERY_GET 1 / 5 / 19 | **否**；固件编号里有，驱动未请求 |

### 6.3 对照结论

| powerd / thermalmonitord 需要的键 | Android 真实值 | battmgr 可取 | 结论 |
| --- | --- | --- | --- |
| `NominalChargeCapacity` | `charge_full` 有值 | `BmFull`（18），已请求 | **可真实提供**（当前已发布） |
| `DesignCapacity` | `charge_full_design` 有值 | `BmDesign`（17），已请求 | **可真实提供**（当前已发布） |
| `CycleCount` | `cycle_count` 有值 | `BmCycles`（16），已请求 | **可真实提供**（当前已发布） |
| `Voltage` / `StateOfCharge` | `voltage_now` / `capacity` 有值 | 7 / 4，已请求 | **可真实提供**（当前已发布） |
| `BatteryData` 的 `UPOCount` / `WRa` | 无对应属性 | 当前表里没有；未见 UPO/WRa 的 property | **不能提供**；需要新的 gauge/board 属性或 vendor 文档 |
| `Battery Serial` | 无 serial 属性；`model_name` 只是型号 | 当前表里没有 | **不能提供**；不能用型号冒充唯一序列号 |
| `calibration0` / `Trusted LifeTime Max WRdc` | 无对应属性 | 无 | **只能留空**（或由 powerd/电池数据收集内部计算） |
| `BatteryDataCollectionReady` | 不是电池属性，是系统/notify 服务 | 无 | **不是 battmgr 能补的**；需 BTM/BatteryDataCollection 服务真实 ready |
| `health`（Android） | `Good` | 固件号 1（未请求） | 可作为**参考输入**，但不是 Apple `Battery Service Flags`；需要新请求并确认返回值格式 |
| `SOH`（若需要） | Android 未直接给出 | 固件号 5（未请求） | 需新请求 + 设备确认 |
| `Battery Service Flags` | **无对应属性** | 只有 `BmAuthentic`/`BmSlaveAuthentic` 两个布尔 auth 位（0x03/0xa5） | **不能直接提供**；见 6.4 |

### 6.4 `Battery Service Flags` 的语义判断

从 powerd 反汇编看，它不是电量/循环计数，而是一个**状态/条件位字**：

- `sub_10000c474` 用 `flags & 0xC00000` 作为是否计算健康状态的**门控**。
- `sub_10000e270` 当这两位为 1 时打印 `Invalid auth condition: recoveredflags:0x%x`，并用 `0xff3fffff` **清掉**这两位。这说明它们与认证/服务条件有关，powerd 会主动“恢复”/清除不合法的值。
- battmgr 里能读到的是 `BmAuthentic`（board 0x03）和 `BmSlaveAuthentic`（0xa5）两个布尔，不是同一个 flags word；Android sysfs 也没有对应属性。
- g233 观测到的 `getBatteryHealthServiceFlags=0x800003` 是 IOPS/powerd 输出侧的 flags，**不能**据此认定输入侧有 `Battery Service Flags`。

结论：**`Battery Service Flags` 不能由我们“给出一个非零值”来补齐。** 没有证据表明它等于某个 battmgr 原始属性；它更像 powerd/电池数据收集根据真实认证/健康状态算出的条件字。若真实认证条件不成立（本机电池没有 Apple 认证芯片，`ui_compat18.c` 也报告 `AppleAuthCPRelay` 不适用），健康页可能按设计不可用；不能用猜测值绕过。

### 6.5 证据级别

- Android 属性名/值：**设备只读**（本次实际查询）。
- battmgr property 编号/是否请求：**源码**（`ShengBattmgr.c`/`.h`）。
- `Battery Service Flags` 的门控/清除逻辑：**反汇编确认**（`sub_10000c474` / `sub_10000e270`）。
- `Battery Service Flags` 是否等于认证结果、以及 `Battery Serial`/UPO/WRa 的最终来源：**推测/未确认**。

## 7. 最小可验证的下一步（只读、默认关闭）

1. 加一个默认关闭的诊断开关（未来实现，不在本轮）：
   - `zizhan_batt_health_diag=1`：在 `AppleSmartBattery` 发布后打印 `BatteryData` 的每个 key、类型、值；打印 `AppleSmartBattery` 的全部属性名（不打印序列号/IP）。
   - 只读，不修改任何值。
2. 设备上（有人在场、只读）看：
   - `ioreg -l -c AppleSmartBattery`、`ioreg -l -n AppleSmartBattery`
   - `ioreg -l -p IODeviceTree | grep -i battery`
   - `notifyutil -g com.apple.thermalmonitor.ageAwareMitigationState`（期望 0/1/2 或 absent）
   - `log show --last 2m --predicate 'process == "powerd" OR process == "thermalmonitord"'`，找 `Battery serial number changed`、`No upo mitigations start threshold`、`Updated Battery Health: State`、`Notification posted for state`
   - `defaults read /var/MobileSoftwareUpdate/Hardware/Battery/com.apple.batteryhealthdata`（只读）
3. 判定：
   - 如果 `Battery Service Flags` 不在 IOPS/电池字典：先找 gauge/Android 的真实 health flag 来源；找不到就保持缺失，不填 0/1。
   - 如果 flags 在但 `Battery Service State` 不出现：看 `Battery Serial` 与 `BatteryDataCollectionReady` 两条分支。
   - 如果 `UPOCount`/`CurrentWRa` 不在 `BatteryData`：找 fuel gauge/PMIC 的真实来源；没有就不发布 ageAware。
4. 温度 HID：只有在 J171AP 的 29 路 watchdog 能提供真实读数时，才谈 thermalmonitord 控制轮；D22 表不能复用。

## 8. 明确不做

- 不在 Settings/用户态伪造 `Battery Service State`、`Battery Service Flags`、`UPOCount`、`WRa`、`ageAwareMitigationState`。
- 不用 `notify_post`/`notify_set_state` 手动发布健康状态。
- 不修改/重签 Apple 二进制；不把 IPSW 提取物提交到仓库。
- 不把 `getBatteryHealthServiceFlags=0x800003` 当成“已有 Battery Service Flags 输入”的证据；它需要设备 ioreg 对照。

## 9. 未决 / 推测

- `BatteryDataCollectionReady` 的生产者（BTM）和它到 `Battery Service State` 的调用链：**字符串推断**，未逐行反汇编。
- `AgingController` 从 `BatteryData` 取 UPO/WRa 的精确 key 名（`UPOCount`/`WRa` vs `kBattery...`）与缺失时的返回值（0/-1）：**字符串 + 方法名推断**，需要诊断日志确认。
- ageAware 是否与 thermal 传感器完整性直接耦合：AgingController 的发布路径看起来独立；控制轮需要 29 路 watchdog。这个分离关系需要设备日志验证，不能先写成结论。
- `deviceSupportsCPMS=0` 与 `CPMSHelper` 的关系：CPMS 影响 PPM 通知/缓解，是否影响 ageAware 的 notify 发布未证实。
