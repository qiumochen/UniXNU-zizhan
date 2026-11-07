# 日用方向第一批无人值守开发

本批从 `6d074ef09e5777cecfa23612e626ce2c3878153e` 加已有 11-06 未提交工作接续。用户授权按[日用路线](../plan/2026-11-06-daily-use-roadmap.md)继续无人值守，人工验收后置。本批主线是 THERM-01 的真实输入与只读观测，BASE-01 独立处理服务目录的构建一致性；当前状态和下一动作只维护在 [STATUS.md](STATUS.md)。

没有新 XNU 启动、手机镜像安装、分区写入、热负载、限频写入或真睡眠。Android 采集和本机只读镜像检查不构成 XNU 温控或整机日用资格。

## THERM-01：采集工具和板级输入

新增 `tools/zizhan/mac/thermal-inventory.py` 与 `tools/zizhan/phone/thermal-readonly.sh`。采集前校验 Android ready、boot completed 和 `ro.product.device=zizhan`；每条 ADB 命令有期限，输出进入新的仓库外目录，保留 `RUNNING/PASS/FAIL/SKIP`、开始/结束时间和部分失败输出。脚本只读取 thermal/cooling/cpufreq/battery sysfs、uptime 与 live FDT。

报告保留原始单位和值：Linux thermal sysfs 的 mC 和 Android battery 的 deciC 分开转换；读取失败、字段缺失、非数值字段分别标记，不以 0 代替。起止 uptime 描述顺序读取窗口，不能当作所有传感器的同时采样，也不能用于无探针功耗结论。

本批最终一次完整采集：

| 项目 | 结果 |
| --- | --- |
| 温区 / 冷却设备 / CPU policy | 81 / 62 / 3 |
| 温度读取失败 | 4 项明确记录 ERROR；没有补值，也没有把本轮说成所有传感器通过 |
| 顺序读取窗口 | 25.70 秒；整个采集约 26.21 秒 |
| 电池温度 | battery 原始 266 deciC；thermal 的 battery 原始 26600 mC，均为 26.6°C |
| CPU 分组 | Android policy 的 related CPUs 为 0–3、4–6、7 |
| 设备侧行为 | 保持 Android，只读；没有写策略、启动负载或重启 |

live FDT 核对的板级资源已生成 `configs/zizhan-thermal-sm8475.json`：

- 两个 `qcom,tsens-v2` provider，各声明 16 个 sensor，整理出 32 个 TSENS zone 引用。其资源为 `0xc263000/0xc222000`、`0xc265000/0xc223000`，每个 DT region 长度为 511 bytes。
- EPSS 的三个 frequency-domain 为 `0x17d91000/0x17d92000/0x17d93000`，各 4096 bytes；三个 pdmem vote 为 `0x17d09804/0x17d09808/0x17d0980c`，各 4 bytes。与当前 `PongCpuPolicy.h` 的地址一致。
- 配置只保留硬件引用及 DT bus address，没有解析总线地址变换、没有校准/寄存器有效性结论，也没有采纳温度阈值；`thermal_policy=null`。
- DT 的 CPU trips 包含 110/115/125°C 等声明，不作为 XNU 的日用热策略。Android 当前 scaling max 也不等于硬件额定上限，未改动既有 XNU cap。

只读采集了 vendor 的 normal/unfold/L18a/map 等配置作为后续输入。`thermal-engine.conf` 是默认空配置；normal/map 文件不是可直接审查的明文策略，本批没有解析其编码或提取可信的限频阈值。`thermald-devices.conf` 能确认 CPU4/CPU7、GPU、背光等控制对象，但不能凭设备名推断温度闭环。原文件和 hash 留本机，不发布。

### 失败记录与工具修正

1. 最初通过 `adb exec-out` 传 shell 脚本，两次均在 45 秒期限退出，未得到有效采集。第二次保留的 11-byte 输出只有 root shell 提示符；这是 stdin 传输错误，不是温度传感器故障。改为有界 `adb shell -T` 后，最小 stdin 对照通过。
2. 第三次已实际读取，但每字段启动独立进程使遍历在 45 秒后仍未完成，保留 59,571-byte 部分输出。本轮不是 PASS。改单行 sysfs 的 shell builtin `read` 后，第四次完整结束。
3. vendor 归档首次混入 tar 的绝对路径提示，归档解码失败；原始字节保留。改用相对路径归档和非 PTY shell 后读回完整文件，不靠剥离未知前缀伪造有效归档。

离线重放同一 snapshot/FDT 可重新生成报告及板级 JSON，生成结果逐字比较；无需再次读取手机。

```sh
python3 tools/zizhan/mac/thermal-inventory.py \
  --collect "$ZIZHAN_SRC/logs/<new-private-capture>" --root --timeout 45
python3 tools/zizhan/mac/thermal-inventory.py \
  --snapshot "$ZIZHAN_SRC/logs/<capture>/snapshot.json" \
  --dtb "$ZIZHAN_SRC/logs/<capture>/live.dtb" \
  --output "$ZIZHAN_SRC/logs/<offline-report>.json" \
  --profile-output "$ZIZHAN_SRC/work/<thermal-inputs>.json"
```

后续 XNU 只读探针仍需核对真实 TSENS 寄存器协议、硬件版本、有效位、温度编码及初始化前置；不能直接把 DT 资源存在当作可读温度。频率写入与热策略另行推进。

## BASE-01：显式服务策略与隔离目录候选

新增 `userland-policy-audit.py`，用仓库自带 APFS 只读解析器检查本地 OS，不挂载或修改候选镜像，也不访问手机。核对 layout 对应的 OS、完整 image hash（可选）、所选服务的 cache/plist、overlay 和 jobs-report，以及 UI/helper 身份。报告明确区分本机准备态与手机实际安装/运行态。

真实 OS370 输入确认 `fairplayd.A2` 存在于 OS 的 cache/plist 和 overlay，而原 `EXCLUDED18` 在重建时排除它。这是服务选择差异，不能靠内核构建通过消除。

审计工具的早期版本还报告了 wifid descriptor 差异；核对后发现只有空 `EnvironmentVariables={}` 与字段缺省的区别。工具现仅对这个空环境字典做等价归一化，保留所有非空环境和其他字段。初始报告留本机；不把该差异称为 wifid 行为故障。原生服务目录另有同 Label、不同 device/variant gate 的条目，工具保留所有变体，不静默覆盖；被选中服务若有多变体仍要求单独解析 gate。

构建端新增：

- `service_policy18.py` 验证固定 22H374 schema 的显式排除策略，并生成稳定 policy hash。
- `build-ipados18.py prepare --service-policy` 记录选择；`refresh_jobs` 未显式指定时继承 build-plan 的已记录策略。旧 plan 没有策略时保留原默认。
- `stage_jobs` 仅生成新的服务目录候选和报告；目标目录必须新建，不编译 runtime、不挂载/改写镜像、不安装。
- `configs/zizhan-22h374-os370-services.json` 是保留 OS370 服务存在性的控制配置，显式选择时不额外排除 fairplayd。原生程序存在性及 variant/feature gate 继续生效；该配置没有成为默认策略。

本机 stock OS/SystemOS 的原 mountpoint 已卸载，首次 staging 因 SystemVersion 文件缺失停止。随后使用独立 mountpoint 只读挂载同一本机 stock 输入，生成两个目录后正常 detach，原始 OS、prepared 镜像和手机未改变。

| 候选 | boot / on-demand / excluded | fairplayd |
| --- | --- | --- |
| legacy-default-exclusions | 68 / 496 / 17 | excluded |
| os370-service-presence-control | 69 / 496 / 16 | boot |

两份输出 cache 的唯一新增 key 是 fairplayd；公共 key 的 descriptor 无差异，没有其他删除。这证明显式策略控制了该目录差异，不证明已完成全量镜像重建、安装或 fairplayd 行为修复。目录 staging 不包含 prepare 后续的 runtime/helper staging。

### 完整本地候选

随后在新建仓库外 `work/daily-use-1106/os370-full-candidate-v3` 采用 `os370-service-presence-control` 单独执行完整 `prepare → apply → finish --verify-images`。stock OS/SystemOS 均只读挂载并在结束时 detach=0；apply 只修改候选副本。原 canonical prepared、OS370 实际安装、UFS 分区均未写。build plan 记录固定 HEAD、27 个实际 userland/APFS build inputs 的 manifest hash、输入 OS/Data hash、prepare 后 OS 与转换后 Data hash、服务策略 hash；apply/finish 在动镜像前检查源码/输入/候选 image 身份。文档和workflow调整不改变 image 的build input identity。

- build-plan 记录 policy SHA256 `934a7444f73de534fe6b61901ce3d61471edc5a517208969013fb71574ba058c`，boot/on-demand/excluded 为 69/496/16。 fairplayd 在 cache 和 plist 中存在。
- `finish --verify-images` 的 native APFS readback 与 Data PackedRAMDisk roundtrip 通过，`device_boot_verified=false`、`phone_written=false`。
- OS SHA256 `1d2f5ceac78f184c3edc8b30ec2dd4bbe9a2d97f04822d5fd2bf9c872c2872a4`；Data SHA256 `e85d0047e3691c067656c0c7d7e9b8a20de624ae6624de74a09d84e1c15674c1`；Packed Data SHA256 `c1011ab65f188edaecb22d755b24046b4c62b68491a97c9cae7b83f3d5f837ec`。实际构建 worktree SHA256 `a40e05c2f63b1515962f78e5b60b7ab9a382e20ae6d434061924aef30c7ea0dd`。完整文件身份只在仓库外 `result.json`/`build-plan.json` 与操作记录，不发布镜像。
- 用独立候选 OS hash layout 对完整镜像运行显式策略审计，24 项均 PASS，runtime UI/wifi-service、服务 cache、plist、overlay 与 plan 一致。另以 legacy 默认策略审计同一候选，按预期 FAIL 一项并指出 fairplayd 被默认重建排除；没有把 mismatch 隐藏掉。
- 两次保留的候选准备失败控制：v1 尚无 build-time source manifest；补充来源身份后，v2 apply 因 Data sparseimage 经 hdiutil 转换，源 hash 与 UDRW 输出 hash 不同而在镜像修改前停止。现改为同时记录源输入与转换后候选 hash，v3 完整流程通过。v1/v2、部分文件和原始日志留本机，不作候选或安装来源。

判定：BASE-01 的完整**主机候选**为 `HOST_VERIFIED`，不是设备已安装或日用通过。人工功能验收继续暂存；在用户明确选择此候选进入设备批次前，实际 OS370 仍是回退基线。

```sh
python3 userland/build-ipados18.py stage_jobs \
  --out "$ZIZHAN_SRC/work/<new-catalog-candidate>" \
  --os-root "$ZIZHAN_SRC/work/<mounted-stock-os>" \
  --system-cryptex "$ZIZHAN_SRC/work/<mounted-stock-systemos>" \
  --service-policy configs/zizhan-22h374-os370-services.json
python3 tools/zizhan/mac/userland-policy-audit.py \
  --prepared "$ZIZHAN_SRC/work/userland-22H374/prepared" \
  --layout "$ZIZHAN_SRC/work/userland-22H374/ufs-metal/ufs-layout.json" \
  --output "$ZIZHAN_SRC/logs/<private-policy-audit>.json" --verify-image
```

默认策略审计应继续报出 fairplayd 不一致；显式选择控制策略可核对内容是否匹配，但不代表控制策略已全量重建或上机。后续应从固定本机输入准备独立完整候选，分别记录 catalog、runtime、image、安装与启动身份，再集中验收。

## 主机回归和发布边界

`python3 tools/zizhan/mac/check-daily-use-host.py` 本地 25 项回归通过：温度/缺失/错误与 FDT 引用/资源宽度、stdin 采集和超时证据、策略差异及实际 job 生成/继承、source/policy/image hash mismatch 在镜像编辑前拒绝。新增 `Daily-use host regressions` workflow，测试无需手机、Apple 私有输入或固件；尚未提交/推送，不能写成云端 CI 已通过。

本批没有修改内核/驱动或启用新的默认运行能力；未重跑内核构建，既有内核证据仍按原 SHA/输入范围理解。最后只读设备复核为 Android `sys.boot_completed=1`、`ro.product.device=zizhan`，无设备 worker/锁；温控 sysfs 采集时 4 个传感器读失败，不能从后续此处未抓取的电池温度复述当前温度。人工验收继续积累，XNU 温度驱动/热策略、候选设备启动、真睡眠及整机日用尚未完成。
