# M2：温控底线与熄屏/亮屏最小方案（2026-11-17）

状态：**只读采集 + 方案设计**。没有上机、没有写任何电源/热寄存器、没有设备轮。
本文给 M2 的两条工作流各一个可执行的最小增量：THERM-01 的“底线”策略，和 POWER-02 的“短按电源键熄屏/再按亮屏可触控”MVP。

证据分级：`看过源码` / `电脑端验证` / `设备只读观察` / `推测`。本文所有温度、频率、冷却状态均为**设备只读观察**；所有策略动作均为**方案**，不是已实现或已验证。

## 1. 温控（THERM-01）

### 1.1 本次只读采集

| 来源 | 内容 | 级别 |
| --- | --- | --- |
| `/sys/class/thermal/thermal_zone*/{type,temp}` | 81 个温区；TSENS 温区当前 30.6–33.9°C | 设备只读观察 |
| `/sys/class/thermal/cooling_device*/{type,max_state,cur_state}` | CPU/GPU/pause/hotplug 冷却设备 | 设备只读观察 |
| `/sys/class/thermal/thermal_message/*` | `board_sensor_temp=29738`（29.738°C）、`cpu_nolimit_temp` 等 vendor HAL 状态 | 设备只读观察 |
| `/sys/devices/system/cpu/cpufreq/policy*` | policy0 hw 2.016 GHz；policy4 max 2.5728 GHz（hw 2.7456）；policy7 max 2.2464 GHz（hw 3.1872） | 设备只读观察 |
| `/vendor/etc/thermal-*.conf` | **加密/非明文**，无法直接取阈值 | 设备只读观察 |
| `$ZIZHAN_SRC/work-dt/live.dts` | TSENS 资源、温区映射、DT trip 点 | 设备只读观察 + 电脑端解析 |
| `docs/progress/2026-11-07-tsens-readonly.md` | g396：两 bank 各 16 路 VALID，32 个有效读数（bank0 38.2–46.9°C，bank1 35.5–39.5°C） | 设备只读观察（历史） |
| `configs/zizhan-thermal-sm8475.json` | TSENS 资源/地址/中断输入；明确“无热控制资格” | 电脑端验证 |

### 1.2 TSENS 通道 ↔ 温区（来自 live DT，非推测）

`live.dts:19469-19489`：两个 `qcom,tsens-v2`：

- `tsens0` = `/soc/thermal-sensor@c263000`，reg `0xc263000/0xc222000`，16 sensors，phandle `0x5d`。
- `tsens1` = `/soc/thermal-sensor@c265000`，reg `0xc265000/0xc223000`，16 sensors，phandle `0x6b`。

`/soc/thermal-zones/<zone>/thermal-sensors = <phandle index>` 的对应关系：

| tsens0 index | 温区 | tsens1 index | 温区 |
| --- | --- | --- | --- |
| 0 | aoss-0 | 0 | aoss-1 |
| 1–4 | cpuss-0…3 | 1–4 | cpu-0-0…3（little） |
| 5–13 | cpu-1-0…8（gold/prime） | 5–7 | nspss-0…2 |
| 14–15 | gpuss-0/1 | 8 | video |
|  |  | 9 | ddr |
|  |  | 10–13 | mdmss-0…3 |
|  |  | 14–15 | camera-0/1 |

另有 `pa`/`pa1`/`mmw*`/`sub1*` 等 22 个 modem 温区走 `qcom,qmi-sensors`（phandle `0x5c`），**不是** TSENS；不要把它们与 32 路 TSENS 混在一张表里。

对照 g396：bank0 最高 46.9°C、bank1 最高 39.5°C，与“gold/prime 在 tsens0、little 在 tsens1”的映射一致；但这只是一次 last-temperature 读数，不是标定。

### 1.3 可依据的阈值：只有两类是一手数字

**A. DT 里的硬件保护 trip（`live.dts` `/soc/thermal-zones`）**

| trip | 温度 | 性质 |
| --- | --- | --- |
| `tj_cfg`（gpuss-0/1） | 95°C | GPU junction，passive |
| `cpu*-emerg*-cfg`（cpu-1-0…8、cpu-0-0…3） | 110°C | CPU emergency，passive |
| `reset-mon-cfg`（全部 CPU/GPU/aoss 温区） | 115°C | 复位监控 |
| `thermal-engine-config` / `thermal-hal-config` | 125°C | vendor 策略占位，不是实际节流点 |

这些是**硬保护**，不是“舒适”阈值；用它们做底线有依据，但只能作为最后一道，不应把 95/110°C 当成日常目标。

**B. Android 观测到的封顶结果（不是阈值）**

- `thermal-cpufreq-0/1/2` 的 `max_state` 为 15/19/20（三档 CPU 频率域）；
- `gpu` 冷却设备 `max_state=12`；
- policy4 `scaling_max_freq=2,572,800` < `cpuinfo_max_freq=2,745,600`；policy7 `2,246,400` < `3,187,200`。说明 vendor 热/性能栈已经在限制，但**限制发生的温度点**在加密配置里。

**C. vendor `thermal-*.conf` 不可读**

`thermal-normal.conf`、`thermal-normal-unfold.conf`、`thermal-l18a-normal.conf`、`thermal-map.conf` 的 `file` 均为 `data`，前 16 字节相同（`6e8160ae0e20bed702cc43ac7ed8e572`），`strings` 无有意义文本；这是加密/混淆格式。`porting.toml` 也已记录“vendor normal/map policies are not readable as plaintext”。**因此不能从这些文件抄阈值，也不能把 DT 的 125°C 占位当成策略。**

### 1.4 同类项目怎么做

- **SM-T733（`T733Thermal.cpp`，经 [R1](../research/2026-11-07-device-reference-directions.md) 引用）**：按完整传感器组计算状态；有样本过期时间、热/冷滞回；缺有效读数时**保持限制**而不是解除；GPU consumer 还检查 worker 样本年龄。这是本机最值得借的“失效安全”语义。
- **data-a-live**：本次 clone 的 `7c779bd` 里没有独立温度驱动（`bootmgr/`、`xnu/`、`docs/` 均无 thermal/tsens 源码）；它把热问题留给 Android，不能作为 zizhan 策略来源。
- **UniXNU-sheng**：`sheng/package/` 里没有热策略；只有 RPMh level 查询（`QcomRpmh.h`）和 CPU/GPU 探测。可借“level→index”的资源投票方式，不借阈值。

### 1.5 建议的 THERM-01 v1（底线，不是完整曲线）

1. **采样**：沿用 g396 的两 bank 只读路径；周期 1 s，单次最多 32 个 sensor，读取有界；记录 `source sample time`、`consumer read time`、VALID、bank 覆盖。
2. **新鲜度**：同 SM-T733 语义——样本超过 2 个周期未更新 → 视为 stale，**保持当前封顶**；任何一 bank invalid/缺失 → 不解除封顶；恢复后需连续 3 个新鲜样本才允许降一档。
3. **动作**：
   - CPU：任一 `cpu-1-*` ≥110°C 或任一 `cpu-0-*` ≥110°C → 三个 EPSS/PDMEM 域降到最低可用档；<105°C 且连续新鲜 → 恢复。
   - GPU：`gpuss-0/1` 任一 ≥95°C → GPU 降到最低档（cooling level 12 的方向）；<90°C 恢复。
   - 115°C `reset-mon` 不动，保留内核/硬件最后保护。
4. **不做**：不写 TSENS 阈值/IRQ、不关闭真实 panic、不发布假 thermalmonitord 服务、不用 125°C 占位当策略、不把 Android `scaling_max_freq` 当 XNU 阈值。
5. **v2（标定后再细化）**：在 Android 上跑一次有界负载，记录“TSENS 温度 → `thermal-cpufreq-*/cur_state`、`gpu/cur_state`”的实际阶梯，形成 zizhan 自己的 table；再用同一 table 做 XNU 的分档封顶。这样阈值来自本机 vendor 行为，不是凭空数字。

### 1.6 验收

| 级别 | 内容 |
| --- | --- |
| 主机 | 采样门控、stale/invalid/恢复回放、封顶只降不升、无写入路径；精简与默认内核构建 |
| 设备 | 10 分钟有界负载：日志含 ≥1 次真实封顶/恢复，温度不越过 110/95°C，自动返回 Android，无 panic |
| 不算通过 | 只读到温度、只降低频率但没有恢复、或只跑 host 测试 |

## 2. 熄屏/亮屏（POWER-02 MVP）

### 2.1 两个旧“解阻条件”是否必要

STATUS 归档对 POWER-02 记过两个条件（[archive](../progress/2026-11-17-status-archive.md) 第 234 行）：

1. 启动日志出现 `ZIZHAN_DSI_DMA: buffer ready`；
2. 128 MiB 布局下显示域仍 `IdentityObserved`。

逐条判断：

| 条件 | 对 MVP 是否必要 | 理由 |
| --- | --- | --- |
| `DSI_DMA: buffer ready` | **不是无条件前置**，只在走“长包 DSI 命令 DMA”时需要 | 11-07 的失败是长包需要 IOVA、而当时用了物理地址；短包 FIFO 路径已有 8 包 461µs PASS。MVP 只需要 DCS `0x28`（display off）/`0x10`（sleep in）/`0x29`/`0x11` 这类短命令，可走已验证的 FIFO；若某一步必须长包，则该步回到条件 1。 |
| 128 MiB 下 `IdentityObserved` | **对“再按亮屏”必要，但不是开工前置** | 若显示域映射不是 identity，off→on 后 DPU/DSI 可能恢复失败。当前显示正常；若现行布局就是 11-08 起的 128 MiB 默认，则该条件大概率已满足，但每次 boot 必须重新确认（ADT/显示域状态），不能拿历史结论代替当次。 |

结论：MVP 可以先做“短命令 + DPU 停扫”的熄屏；把长包 DMA 和 identity 复核作为**该步的验收门**，而不是整个 M2 的阻塞条件。

### 2.2 最小版本范围

只做：短按电源键 → 内屏真实关闭（无光、无扫描）、触控暂停 → 再按 → 亮屏、首帧可见、触控恢复。
不做：系统级 suspend、CPU idle 深驻留、RPMh sleep/wake、Wi-Fi 唤醒、锁屏认证、面板 rail 全下电。

这样做的代价要说清楚：如果只关 DCS/DPU 而保留面板 rail，息屏功耗不会接近 Android；MVP 验收的是“屏幕真的黑、触摸真的停、再按真的能回来”，不是待机功耗。

### 2.3 实现步骤与每步设备验证

| 步 | 改动 | 设备验证（只写方法，不上机） |
| --- | --- | --- |
| S1 请求通路 | 电源键短按 → powerd/SpringBoard → `kFBMethodRequestPowerChange` 不再直接返回成功，进入真实事务 | 日志出现请求/接受/完成三态；按键 1 次只产生 1 个事务；不亮屏时无事务 |
| S2 停止呈现 | 停 VSync/presenter/周期 timer；保留最后一帧 | 日志显示 VSync 停止；帧时间线不再增长；截图仍为最后画面 |
| S3 面板关 | 短 DCS `0x28` → `0x10`；DPU 停扫；背光 0（已有真实写回） | 屏幕无光（相机拍照/肉眼）；DSI 短命令 ACK；背光读回 0；无 DSI timeout |
| S4 触控暂停 | 停 8 ms 触控线程；保留唤醒源 | 熄屏后触摸日志为 0 事件；不产生误唤醒 |
| S5 唤醒 | 电源键 → `0x11` → `0x29`；DPU 重新使能；恢复首帧；触控线程重启 | 首帧可见（截图与熄屏前对比）；触摸坐标日志恢复；无 panic |
| S6 循环 | 重复 10 次 | 10/10 通过；任一次失败保留现场，不自动重试 |

每步都必须有“失败回安卓/回亮屏”的路径；不允许用无限 WFI、halt 或 panic 表示“已熄屏”。

### 2.4 风险与回退

- **黑屏但面板仍供电**：MVP 可接受，但必须注明不是低功耗；若要进一步下电，走 rail/reset 序列，单独验收。
- **DSI 短命令失败**：回退到亮屏状态并记录，不把失败当“已关闭”。
- **唤醒后黑屏**：保留最后一帧与 DSI 状态；先恢复 DPU，再重发 `0x11/0x29`；仍失败则回 fastboot（人工返回路径）。
- **触控恢复失败**：把触控线程重启与首帧分开记录，不能因为画面回来就宣称触控通过。

## 3. 建议的执行顺序

1. THERM-01 v1 先做主机实现 + 现有 g396 路径扩展（不改默认行为），再做一次有界设备负载；通过后才有“温控底线”。
2. POWER-02 MVP 在 M1 之后、且有人在场时做首次 off/on（面板命令和返回都需要人看）；无人值守只做主机与只读复核。
3. 两项都不得与 M1 首声测试争用手机；今晚手机保持 Android。

