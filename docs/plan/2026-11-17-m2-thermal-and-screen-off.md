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
| `$ZIZHAN_SRC/../work-dt/live.dts` | TSENS 资源、温区映射、DT trip 点 | 设备只读观察 + 电脑端解析 |
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

**C. vendor `thermal-*.conf` 在当前手上不可读**

`thermal-normal.conf`、`thermal-normal-unfold.conf`、`thermal-l18a-normal.conf`、`thermal-map.conf` 的 `file` 均为 `data`，前 16 字节相同（`6e8160ae0e20bed702cc43ac7ed8e572`），`strings` 无有意义文本；这是加密/混淆格式。`porting.toml` 也已记录“vendor normal/map policies are not readable as plaintext”。

但 data-a-live 的记录说明它那一代的这类 conf **是可解密的**：它的 `pandora_therm.c:48-53` 明确写阈值来自 `mi_thermald` 的 `thermal-normal.conf`，明文取自 `thermalcheck raw/data/decrypt.txt`。我们目前没有对应的解密工具/明文，所以只能用 DT 硬保护 trip 和 Android 的可观测行为；**不能把 DT 的 125°C 占位当成策略**。解密路径是 zizhan 后续可以立项调查的输入，不是本轮已获得的数据。

### 1.4 同类项目怎么做

- **data-a-live（`7c779bd`，`wsl/`；本文初版漏查，现更正）**：有完整温度链，分三层。
  - **来源**：14 个 NTC 热敏电阻经 PMK8850 的 `vadc@9000`（`qcom,spmi-adc5-gen4`）thermal monitor 的 SDAM 槽读取；通道表在 `pandora_therm.c:55-76`（SDAM0 0x090 slots 1-7，SDAM1 0x091 slots 0-6），启用序列按 Linux `adc_tm5_gen3_configure`（`:167-200`）。NTC 码→温度用 `pandora_ntc.h:15-40`（100 kΩ 上拉、`pandora_ntc_table.h` 的 168 点表，表值单位 1/1024°C）。SoC die/junction 另走 TSENS：`pandora_cpu.c:437-497`。
  - **单位与周期**：内部 millidegree；每 1 s 读一次 SDAM（`PT_PERIOD_MS=1000`，`pandora_therm.c:62,117`），10 s 打一行汇总（`:63`）。HID 服务默认 ReportInterval 1,000,000 µs（`PandoraThermalHID.cpp:63`），thermalmonitord 可通过 `kIOHIDReportIntervalKey` 改成 100–10000 ms（`PandoraThermalHID.cpp:128-143`）。
  - **怎么交给 iOS**：`PandoraThermalHID.cpp:47-61` 发布 `PrimaryUsagePage 0xff00 / PrimaryUsage 5` 的 `IOHIDEventService`，每个 sensor 一个 `PandoraHIDTemperature`；`copyEvent(kIOHIDEventTypeTemperature)` 返回摄氏度 16.16 fixed（`:112-126`）。thermalmonitord 的 HidSensors 按 `Product` 与 `D22AP-Info.plist` 的 `matchName` 配对（`:5-10` 注释）。另有 `AppleVTempDispatcher`（`:188-226`）接收 thermalmonitord 算出的虚拟温度，写回 `pandora_therm_ios_temp` 用于日志对照。
  - **限频动作与阈值**：`pt_rule`（`pandora_therm.c:54-70`）是 mi_thermald 的 body 温度阶梯：cpu0 25–50°C 15 档、cpu6 25–50°C 15 档、gpu 35–50°C 6 档，带 2–3°C 清除滞回；`pt_limit()`（`:83-105`）只在“六个 PMIC 输入 + gauge 都有效”时动作，调 `pandora_cpu_body_cap()`（`pandora_cpu.c:654-669`）和 `pandora_gpu_body_top`（`pandora_gpu.c:3718-3719`，应用点 `:3894`）。阈值来自解密的 `thermal-normal.conf`，不是自定。
  - **失效处理**：SDAM 读失败只记 5 次并继续（`pandora_therm.c:549-555`）；`pt_limit` 只在整组读数有效时动作（`:585-590`）；HID 没值就返回 0、不发布（`PandoraThermalHID.cpp:115-119`）；105/114/118°C junction 规则保留为最后保护（`pandora_cpu.c:524-529`）。没有把缺失读数填成 0°C。
  - **已知副作用**：完整 D22 传感器集会让 thermalmonitord 跑控制轮，并按其 iPhone X plist 把背光压到最低（`pandora_therm.c:382-385` 注释，usb10/usb11/spk7）；所以默认关闭（`pandora_therm.c:385-398`），需要 `pandora_therm=2` + `pandora_therm_ios=1` 才发布。
- **SM-T733（`T733Thermal.cpp`，经 [R1](../research/2026-11-07-device-reference-directions.md) 引用）**：按完整传感器组计算状态；有样本过期时间、热/冷滞回；缺有效读数时**保持限制**而不是解除；GPU consumer 还检查 worker 样本年龄。这是本机最值得借的“失效安全”语义。
- **UniXNU-sheng**：`sheng/package/` 里没有热策略；只有 RPMh level 查询（`QcomRpmh.h`）和 CPU/GPU 探测。可借“level→index”的资源投票方式，不借阈值。

### 1.5 建议的 THERM-01 v1（底线，不是完整曲线）

1. **采样**：沿用 g396 的两 bank 只读路径；周期 1 s，单次最多 32 个 sensor，读取有界；记录 `source sample time`、`consumer read time`、VALID、bank 覆盖。这里借 data-a-live 的“每秒读、10 秒汇总”节奏，但**不借它的 SM8850 地址、NTC 表或 14 路通道**。
2. **新鲜度**：同 SM-T733 语义——样本超过 2 个周期未更新 → 视为 stale，**保持当前封顶**；任何一 bank invalid/缺失 → 不解除封顶；恢复后需连续 3 个新鲜样本才允许降一档。data-a-live 的 `measured && gauge` 门控是同一原则的实例。
3. **动作**（只做底线，不做 mi_thermald 的完整 body 曲线）：
   - CPU：任一 `cpu-1-*` ≥110°C 或任一 `cpu-0-*` ≥110°C → 三个 EPSS/PDMEM 域降到最低可用档；<105°C 且连续新鲜 → 恢复。
   - GPU：`gpuss-0/1` 任一 ≥95°C → GPU 降到最低档（cooling level 12 的方向）；<90°C 恢复。
   - 115°C `reset-mon` 不动，保留内核/硬件最后保护。
4. **不做**：不写 TSENS 阈值/IRQ、不关闭真实 panic、不发布假 thermalmonitord 服务、不用 125°C 占位当策略、不把 Android `scaling_max_freq` 当 XNU 阈值、不照搬 data-a-live 的 SM8850 body 表。
5. **v2（标定后再细化）**：在 Android 上跑一次有界负载，记录“TSENS 温度 → `thermal-cpufreq-*/cur_state`、`gpu/cur_state`”的实际阶梯，形成 zizhan 自己的 table；再用同一 table 做 XNU 的分档封顶。data-a-live 的做法证明“vendor 阶梯 → XNU 限频”是可行的，但它的数值只属于 SM8850。

> 候选核心已落地：`drivers/ArmDTPlatform/ZizhanThermalPolicy.h` + `tools/zizhan/tests/thermal-policy.py`，已在分支 `codex/2026-11-17-m2-m3-prep` 主机验证（Mac `cc` 与 `zizhan-ci:gcc14-clang`，`-Wall -Wextra -Werror -fsanitize=address,undefined`，0 failure）；**未接线、未上机**。

### 1.6 验收

| 级别 | 内容 |
| --- | --- |
| 主机 | 采样门控、stale/invalid/恢复回放、封顶只降不升、无写入路径；精简与默认内核构建 |
| 设备 | 10 分钟有界负载：日志含 ≥1 次真实封顶/恢复，温度不越过 110/95°C，自动返回 Android，无 panic |
| 不算通过 | 只读到温度、只降低频率但没有恢复、或只跑 host 测试 |

### 1.7 THERM-01 接线设计（只写方案，不改内核源码）

#### 1.7.1 CPU：EPSS 三域上限与触摸 boost 的优先级

`drivers/ArmCpu/PongCpuPolicy.h` 已经有一个 8 ms/32 ms 的 EPSS worker：

- `:150-170` 从 EPSS LUT 填 `d.khz[]`/`d.levels`/`d.cap_khz`；不同核心数的 boost 行保留索引但 `khz=0`，选择循环会跳过。
- `:186-201` `pong_cpu_boost_ms`（默认 400 ms）和触摸计数 `pong_touch_activity`；boost 开始对三个域调用 `setTop(d)`。
- `:242` `if(boosted) requested = d.cap_khz`（触摸 boost 直接推到域上限）。
- `:243-247` `cap` 先取 `d.cap_khz`，再用 EPSS `0x30c/4 bit2` 的硬件上限压低。
- `:249-258` 在 `khz[i] <= cap` 的 LUT 行里选 `requested`；最后写 `*d.pdmem=selected` 和 `d.regs[0x320/4]=selected`（perf-lock + 当前档）。
- `:278-285` `pong_cpu_policy` 默认 1，`kernel_thread_start(worker)`。

**接线方案（看过源码 + 推测映射）**：

1. 在 `ZizhanThermalService`（新头文件，或 `ZizhanTsensProbe.h` 扩展）里持有 `ZizhanThermalState`，把 `cpu_level` 以 `volatile uint8_t`/atomic 发布为 `pong_cpu_thermal_level`（0 = 不封顶，20 = 最低）。
2. 在 `PongCpuPolicy.h` 的域循环里加一个 `thermalCapKhz(d, level)`：
   - 收集 `d.khz[i] != 0` 的可用行（排除 boost 行）；
   - `rank = (level * (usable - 1) + 10) / 20`；取第 `rank` 个可用行的 `khz`；
   - `level==0` 返回 0（不封顶）。
3. 在 `:243-247` 的 `cap` 计算之后、`:249` 选择之前：

   ```c
   const unsigned thermal = thermalCapKhz(d, pong_cpu_thermal_level);
   if (thermal && thermal < cap) cap = thermal;
   if (requested > cap) requested = cap;   /* 触摸 boost 不能越过温控 */
   ```

   `:242` 的 boost `requested=d.cap_khz` 保留，但后面的 clamp 必须覆盖它；这样温控永远压过 boost。
4. L3：`l3Mode==1` 在 `:182` 无条件投最高档，会绕过温控；THERM-01 模式应使用 `pong_l3=2`（`:261-274` 跟随最快域）或把 L3 的 `want` 也按温控比例压低。**这是设计选择，未改代码。**

#### 1.7.2 GPU：DCVS 的 top/floor 上限

`drivers/PongAdreno/PongAdrenoPlatform.cpp`：

- `:343` `pong_gpu_level` 是启动档；`:386-390` `pong_gpu_dcvs=1` 默认启动 DCVS 线程。
- `:421` `dcvsMHz(level)=PongAdrenoPowerLevels[13-level].hz`：level 13 = 最高，level 1 = 最低。
- `:424-431` `top`（`pong_gpu_max_level`，默认 13）、`floor`（`pong_gpu_dcvs_min`，默认 3）、`levelFor` 都用 `top`。
- `:503` 触摸 boost `want=top`；`:508-512` 先 clamp 到 `[floor,top]`，再 `PongAdrenoGmuSetLevel(&gmu,want,PongAdrenoPowerLevels[13-want].bus,&error)`。

**接线方案**：

1. 温控核心的 `gpu_level`（0 = 不封顶，12 = 最低）→ `thermalTop = 13 - gpu_level`。
2. `dcvs` 每轮（或每 16 ms）读 `pong_gpu_thermal_level`，令：

   ```c
   unsigned top = min(boot_top, 13 - pong_gpu_thermal_level);
   unsigned floor = min(boot_floor, top);
   ```

   这样 `levelFor`、触摸 boost `want=top` 和最终 clamp 都被温控限制；`:512` 的 `PongAdrenoGmuSetLevel` 不需要新接口。
3. `gpu_level==0` 时 `thermalTop=13`，完全保持现有 DCVS；`gpu_level==12` 时 top=1（最低档）。
4. `pong_gpu_max_level` 仍可作更低的硬上限；温控取两者较小值。

#### 1.7.3 采样：从 g396 的一次性观察改成 1 s 周期

`drivers/ArmDTPlatform/ZizhanTsensProbe.h`：

- `:24-86` `worker`：等 `rootvnode` 最多 120 s，睡 5 s，映射两 bank 的 SROT/TM，调用 `observe()`（`ZizhanTsensCore.h:56-74`），打印 32 路，然后 `thread_terminate`。
- `:88-111` `start`：只认 `zizhan_tsens=1`，校验 `zizhan-tsens-v2` ADT，启动线程。
- `ArmDTPlatform.cpp:235` 在 `pong_cpu_start` 后调用 `zizhan_tsens::start(provider)`。

**接线方案**：

1. 保留 `observe()` 和 ADT 校验；新增 `ZizhanThermalService`（可在同一头文件）持有 `ZizhanThermalState` 和 `ZizhanThermalConfig`。
2. `worker` 在 root 就绪 + 5 s 后进入周期：

   ```c
   for (;;) {
     build_sample();                 // 两 bank -> ZizhanThermalSample
     int status = zizhan_thermal_step(&cfg, &state, &sample);
     if (mode == 2) apply_caps(state.cpu_level, state.gpu_level);
     log_if_changed_or_10s(status, ...);
     IOSleep(1000);
   }
   ```

   `IOSleep` 保证有界、不忙等；退出条件：`rootvnode` 消失、超过 `zizhan_thermal_max_s`（建议默认 86400 = 24 h）或线程被显式停止，退出时 `thread_terminate` + `thread_block`。不是无限 WFI，也不阻止真实 panic。
3. 采样样本：
   - `bank_valid[i] = (out.state == Pass || Invalid)`；`Disabled/InReset/UnsupportedVersion` → 0。
   - `valid_mask[i]` 的 bit n = `out.sensors[n].state == Pass`；`temp_mc = milli_celsius`。
   - `time_us = mach_absolute_time` 转 µs；`period_us = 1,000,000`。
4. 不能把 invalid 填 0°C；policy 的 invalid/stale 语义已经是 fail-safe。

#### 1.7.4 开关、日志与命令行

**建议 boot-arg**：

| 参数 | 行为 |
| --- | --- |
| 缺省 / `zizhan_thermal=0` | 不启动周期采样（保留 `zizhan_tsens=1` 的一次性诊断） |
| `zizhan_thermal=1` | 1 s 周期采样，只记录，不写 CPU/GPU |
| `zizhan_thermal=2` | 1 s 周期采样 + 应用 CPU/GPU 封顶 |

`zizhan_tsens=1` 与 `zizhan_thermal` 同时给时，以 `zizhan_thermal` 为准，避免两个线程同时读 TSENS。

**日志格式（建议，紧凑）**：

```text
ZIZHAN_THERMAL: t=123456789 us mode=2 cpu=20 gpu=0 max0=46900 max1=39500 valid=0xffff/0xffff stale=0 reason=CPU_HOT bank=0 sensor=6
```

字段：时间、mode、CPU/GPU cap 档位、两 bank 最大值（m°C）、两 bank valid mask、stale 状态、触发原因（`CPU_HOT`/`GPU_HOT`/`HOLD_STALE`/`INVALID`/`RELEASE`/`NONE`）、触发的 bank/sensor。只在 cap 变化、stale/invalid 或每 10 s 打一行；32 路全量日志只在 `zizhan_thermal=1` 的调试轮开启。

**命令行长度**：`ci_boot_ufs.sh:36-40` 用 `LEN <= 1020` 检查（DarwinBoot24 拒绝 >1023）。`zizhan_thermal=2` 约 17 字节；如果当前包接近上限，优先去掉已被替代的 `zizhan_tsens=1`（约 14 字节），或使用短别名 `zth=2`（6 字节）。任何参数加入前都要跑这个长度检查；不要用 `ALLOW_LONG_CMDLINE=1` 绕过。

#### 1.7.5 改动清单与验证边界

| 文件 | 改动 | 量级 | 级别 |
| --- | --- | --- | --- |
| `drivers/ArmDTPlatform/ZizhanThermalService.h`（建议新） | 周期线程、build_sample、mode/log、发布 cap 原子 | 150–250 行 | 设计 |
| `ZizhanTsensProbe.h` / `ArmDTPlatform.cpp` | 接入周期模式（保留 one-shot 诊断） | 20–40 行 | 看过源码 |
| `drivers/ArmCpu/PongCpuPolicy.h` | `thermalCapKhz` + cap/requested clamp | 20–30 行 | 看过源码 + 推测映射 |
| `drivers/PongAdreno/PongAdrenoPlatform.cpp` | 读 `pong_gpu_thermal_level`，clamp top/floor/want | 10–20 行 | 看过源码 |
| 启动参数/日志 | `zizhan_thermal`、长度检查、日志行 | 10 行 + 打包检查 | 设计 |

验证边界：先在主机用 g396 真实读数回放（见 `tools/zizhan/tests/thermal-policy.c` 的 `test_real_idle_g396`）和 110°C 单点封顶；接线后先 `zizhan_thermal=1` 只读 60 s，再 `=2` 做有界负载。任何设备轮都需要独立返回路径；本设计不改变真实 panic、不写 TSENS 阈值/IRQ、不伪造 thermal 服务。

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

#### S1 落点（只读源码定位）

**现在的请求处理确实是“直接返回成功”。**

| 位置 | 事实 | 级别 |
| --- | --- | --- |
| `patches/xnu-ipados18/0001-pong-darwin24.patch:5128` | 定义 selector `kFBMethodRequestPowerChange = 12`（已按 22H374 framework 核对） | 看过源码 |
| `patches/xnu-ipados18/0001-pong-darwin24.patch:6512` | `case kFBMethodRequestPowerChange` 与 `SwapSignal/SwapCancel/ChangeFrameInfo` 一起 `return kIOReturnSuccess;`，没有解析、没有状态变化 | 看过源码 |
| `patches/xnu-ipados18/0020-zizhan-display-power-observer.patch`（hunk `@@ -1556,8 +1559,13 @@`） | 在 case 里调用 `zizhan_display_power_observe(args->scalarInputCount, in, args->structureInputSize)`，但注释明确 “hardware-transition=UNIMPLEMENTED”，仍返回成功 | 看过源码 |
| `patches/xnu-ipados18/0021-zizhan-display-owner-accounting.patch`（hunk `@@ -1564,6 +1610,7 @@`） | 再加 `fOwner->observePowerOwner()`（软件 drain 计数），仍无硬件事务 | 看过源码 |
| `drivers/ArmDTPlatform/ZizhanPowerAudit.h:102-121` | observer 只在 `zizhan_power_observe=1` 时记录 `SKIP observe-only=1 … hardware-transition=UNIMPLEMENTED`；通过 `ArmDTPlatform.cpp:26` 的 `#include "ZizhanPowerAudit.h"` 进入构建 | 看过源码 |

**短按电源键现在走到哪里。**

| 位置 | 事实 | 级别 |
| --- | --- | --- |
| `patches/xnu-ipados18/0001-pong-darwin24.patch:117-260` | 新建 `bsd/dev/pong_buttons.c`：读 PMIC PON 与 PM8350 GPIO6，16 ms 轮询；`look()` 在变化时 `pong_touch_post(PONG_BUTTON_DOWN/UP, code, …)` | 看过源码 |
| 同文件 ~`:1223-1276`、`:1297` | `pong_goodix.c` 把按钮事件放进独立队列，暴露 `/dev/pong-buttons`；`pong_buttons_device_ready()` 检查它 | 看过源码 |
| `userland/touch.c:374-460` | `buttons_thread` 读 `/dev/pong-buttons`，finger 0/1/2 映射到 `HID_USAGE_POWER 0x30`、`VOLUME_UP 0xe9`、`VOLUME_DOWN 0xea`（`:59-64`），用 `IOHIDVirtualServiceClientDispatchEvent` 发给 consumer HID 服务；短按因此已经到 SpringBoard/powerd | 看过源码 |
| `patches/xnu-ipados18/0010-pong-power-key-reset.patch:16-21,30-41` | 3 s 长按逻辑：`reset_ms` 默认 3000，按住期间累计 `power_down_at`，到点调用 `pong_button_reset_hook`；**短按不触发**（只有 `held >= reset_ms`） | 看过源码 |
| `drivers/ArmDTPlatform/PongBootReset.h:211-241` | 安装 `buttonReset()`：标记 bootloader、flush klog、warm reset 进 fastboot；这是长按的返回路径，不是短按 | 看过源码 |

**S1 要改的落点（尚未改代码）。**

| 文件 / 函数 | 改动 | 量级 |
| --- | --- | --- |
| 新增 `patches/xnu-ipados18/0023-zizhan-display-power-request.patch`（改 `MiniIOMobileFramebuffer.cpp` 的 `kFBMethodRequestPowerChange` case） | 解析 `scalarInputCount`/`in[]`，调用新的 `zizhan_display_power_request(count, in, size)`；malformed → `kIOReturnBadArgument`，被协调器拒绝 → `kIOReturnNotReady`/`kIOReturnUnsupported`，只有“接受并排队”才返回成功 | 30–60 行 |
| 新增 `drivers/ArmDTPlatform/ZizhanDisplayPower.h`（或扩 `ZizhanPowerAudit.h`） | 只做 S1 的接受/拒绝状态机：sequence、target on/off、bounded queue、三态日志；**不碰 DSI/DPU/背光** | 80–150 行 |
| `drivers/ArmDTPlatform/ArmDTPlatform.cpp` | include + 安装 hook（照 `:26` 的 PowerAudit 方式） | 1–5 行 |
| `userland/touch.c`、`pong_buttons.c`、`0010` | S1 不需要改；短按 HID 与 3 s 长按已经存在 | 0 |

**待确认（推测，不是结论）**

- `kFBMethodRequestPowerChange` 的 `in[0]`/`in[1]` 语义没有解码；observer 只记录 raw 值（`ZizhanPowerAudit.h:115-120`）。需要用 `zizhan_power_observe=1` 的设备日志看短按/off/on 的实际参数，才能确定 target 字段。**推测**。
- “返回成功”表示“请求已接受”还是“硬件已完成”未定义；S1 应明确为**接受**，完成态留给 S2/S3 的显式日志/通知。**推测**。
- 本任务只读源码，没有改补丁、驱动或用户态。

#### 参数含义（DSC 反汇编，2026-11-17）

从 22H374 的 `IOMobileFramebuffer.framework` 提取并反汇编：

| 地址 / 证据 | 事实 | 级别 |
| --- | --- | --- |
| `_IOMobileFramebufferRequestPowerChange` @0x1b6fe1620 | 公共 C 函数，`x0` = framebuffer 对象，`w1` = 调用者的 power 参数；它 `ldr x2,[x0,#0x970]` 后 `br x2`，把参数原样交给下层 | 反汇编确认 |
| `_kern_RequestPowerChange` @0x1b6fe4664 | `mov w8,w1; str x8,[sp,#0x10]`；`ldr w0,[x19,#0x14]`；`add x2,sp,#0x10`；`mov w1,#0xc`；`mov w3,#0x1`；`x4=x5=0`；`bl 0x1bc008f90` | 反汇编确认 |
| 由上一行 | selector **12**、`scalarInputCount = 1`、`in[0]` = 调用者的 32 位参数、**`in[1]` 不传**（结构输入/输出为 0） | 反汇编确认 |
| `w1 == 0` 分支 @0x1b6fe4690-0x1b6fe46c8 | 先发 CoreAnalytics `com.apple.TritiumVSHCompensation.CumulativeOnTime`（type 3），再以 selector 17、scalar 3 调一次；`w1 == 1` 没有这个分支 | 反汇编确认 |
| `libramrod.dylib` / `QuartzCore` | 两者都 import `_IOMobileFramebufferRequestPowerChange`；libramrod 有 `IOMobileFramebufferRequestPowerChange failed: %x` 日志，QuartzCore 有同名符号字符串 | 字符串/符号确认 |
| `CoreBrightness` / `BacklightServices` | CoreBrightness 只 import `IOMobileFramebufferCreateDisplayList/GetBlock/GetMainDisplay/GetServiceObject/OpenByName`，**不 import** RequestPowerChange；BacklightServices 没有该符号 | 符号确认 |
| `CADisplayPowerAssertion` @0x1845a92e0 | 用的是 Mach IPC 到 display state 服务（`_mach_msg_stub`），不是 selector 12 | 反汇编确认 |

**结论**：selector 12 只有一个 scalar，`in[0]` 是调用者参数；`in[1]` 不参与。但**没有找到直接调用方和 0/1 常量**：`libramrod`/`QuartzCore` 只是 import，`CADisplayPowerAssertion` 走 Mach IPC，CoreBrightness/BacklightServices 不 import。`in[0]==1` = on、`0` = off 仍是**推测**；`0` 的 CoreAnalytics 分支只能说明它是有特殊意义的“非 on”值，不能单独证明 off。**分类规则暂不改**，等设备 raw 日志或确认的调用方。

> S1 状态机候选已在分支 `codex/2026-11-17-m2-m3-prep` 主机验证（`drivers/ArmDTPlatform/ZizhanDisplayPower.h` + `tools/zizhan/tests/display-power.py`，Mac `cc` 与 `zizhan-ci:gcc14-clang`，ASan/UBSan，0 failure）；**未接线、未上机**。

### 2.4 风险与回退

- **黑屏但面板仍供电**：MVP 可接受，但必须注明不是低功耗；若要进一步下电，走 rail/reset 序列，单独验收。
- **DSI 短命令失败**：回退到亮屏状态并记录，不把失败当“已关闭”。
- **唤醒后黑屏**：保留最后一帧与 DSI 状态；先恢复 DPU，再重发 `0x11/0x29`；仍失败则回 fastboot（人工返回路径）。
- **触控恢复失败**：把触控线程重启与首帧分开记录，不能因为画面回来就宣称触控通过。

## 3. 电池健康页与热服务（POWER-01 关联）

这一节只写依据和可验证的假设，不把“提供温度”写成“健康页会好”。

### 3.1 已记录的现状

| 证据 | 内容 |
| --- | --- |
| `porting.toml` 的 power findings | “The health spinner follows batteryDataUnavailable: thermalmonitord never publishes com.apple.thermalmonitor.ageAwareMitigationState and powerd lacks Battery Service State.” |
| `docs/progress/2026-11-01-battery.md:92,101` | powerd 从电池的 NominalChargeCapacity、DesignCapacity、CycleCount、Serial、Battery Service Flags 计算 “Battery Service State”；`getManagementState=0` 是在 `deviceSupportsCPMS=0` 时读 `com.apple.thermalmonitor.ageAwareMitigationState`；thermalmonitord pid 37 在跑但从不发布该 key。 |
| `docs/progress/2026-11-06-daytime-input-power.md` | 默认包的 `pong_adsp=0` 不提供 ADSP 电池链；调查健康页前必须先核对实际启动参数；不得注入成功状态。 |

### 3.2 data-a-live 的做法给出的线索

- 它的 HID 温度服务确实能让 thermalmonitord 进入控制轮：在 HID 传感器发布后，thermalmonitord 按 D22 plist 的背光控制把屏幕压到最低（`pandora_therm.c:382-385` 注释，usb10/usb11/spk7）。这说明“有完整传感器集 → thermalmonitord 计算”这条链在 XNU 上是通的。
- 但 thermalmonitord 只在**每个 watchdog 传感器都拿到值**的轮次才跑控制（`pandora_therm.c:344-348` 注释）；缺传感器时它记日志并跳过，不会补零。
- 它没有记录 `ageAwareMitigationState` 是否因此出现；这仍是一个待验证点，不是已知结果。

### 3.3 可验证的假设（按优先级）

| 假设 | 依据 | 最小验证 | 若成立 |
| --- | --- | --- | --- |
| H1：`ageAwareMitigationState` 由 thermalmonitord 在控制轮里发布；只要传感器集完整，它就会出现 | 3.2 的控制轮证据 + zizhan 的“从不发布”现状 | 诊断启动，发布一套真实温度的 HID 服务；用 `notifyutil -g com.apple.thermalmonitor.ageAwareMitigationState` 读 key，并 `ioreg -l` 看 thermal 服务 | 只能说明管理状态来源通了，不等于健康页完成 |
| H2：powerd 的 `"Battery Service State"` 由电池数据独立计算，温度 HID 不会直接产生它 | `2026-11-01-battery.md:92`（powerd 从容量/循环/序列号算） | 同一轮里读 `IOPSCopyPowerSourcesByType` 字典，确认该 key 是否出现 | 健康页仍会转圈；需要单独修 powerd 输入 |
| H3：完整 D22 传感器集有背光副作用（亮度被压到最低） | data-a-live 的 usb10/usb11/spk7 | 诊断轮观察 `CoreBrightness`/背光值；默认关闭，只在 boot arg 下开启 | 必须把 HID 作为默认关闭的诊断能力，不能直接进日用 |
| H4：zizhan 的电池链还需要 `pong_adsp=2`（真实 ADSP 电池数据）才具备 powerd 的输入 | `2026-11-06-daytime-input-power.md`、`2026-11-06-fold-power-audit.md:60` | 对照启动参数与 IOPS/BatteryData 实际存在性 | 温度 HID 单独的收益更有限，需和电池链一起验 |

### 3.4 结论边界

- 提供真实温度来源**有可能**让 thermalmonitord 从“只跑不发布”变成“跑并发布 ageAware”，从而解开 `getManagementState` 这一项；但这是 H1，未验证。
- 健康页还依赖 powerd 的 `"Battery Service State"`（H2），它和温度没有直接关系；因此**不能承诺“有温度就能修好健康页”**。
- 任何验证都只读 notify/IOPS/ioreg，不注入 key、不伪造“正常”；不把 HID 副作用带进默认启动。

## 4. 建议的执行顺序

1. THERM-01 v1 先做主机实现 + 现有 g396 路径扩展（不改默认行为），再做一次有界设备负载；通过后才有“温控底线”。
2. POWER-01 的 H1/H2 验证放在 THERM-01 采样可用之后：先确认 thermalmonitord 能不能收到真实温度，再谈健康页。
3. POWER-02 MVP 在 M1 之后、且有人在场时做首次 off/on（面板命令和返回都需要人看）；无人值守只做主机与只读复核。
4. 三项都不得与 M1 首声测试争用手机；今晚手机保持 Android。

