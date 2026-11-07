# zizhan 开发交接

本文件保存开发环境、工作流和历史教训。**当前基线、任务优先级和验证边界统一见 [当前台账](docs/progress/STATUS.md)**；先读 [AGENTS.md](AGENTS.md) 的进度管理规则，再读台账和相关证据。[porting.toml](porting.toml) 是能力记录；本文件内的日期段落按当时状态理解，不用历史“当前”覆盖台账。本文件不保存机器身份或设备标识。

当前接续按 2026-11-06 用户的[日用路线](docs/plan/2026-11-06-daily-use-roadmap.md)：先补温控、电源恢复、网络与常用应用，再推进声音与折叠，最终手机能力；人工验收积累后集中进行。11-07 已按新授权继续：THERM-01 [默认关闭的一次只读 XNU 候选](docs/progress/2026-11-07-tsens-readonly.md)与主机/内核构建已完成，设备结果单独登记；BASE-01 完整主机候选保留，未安装。POWER-02 有[分阶段恢复契约](docs/plan/2026-11-07-power-resume-contract.md)，尚未实现系统睡眠。状态与下一动作统一以台账为准。

11-07 后续已形成[完整锁屏、熄屏与硬件休眠方案](docs/plan/2026-11-07-lockscreen-suspend-design.md)，并按授权完成[首批实现](docs/progress/2026-11-07-power-first-implementation.md)：bring-up隔离UI仅为未安装候选；电源事务模型、板级清单和默认关闭的PSCI/display请求观察已主机验证，g397仅查询审计及正常返回通过。真实显示掉电、系统睡眠和恢复尚未实现验收；后续接续以POWER-02台账为准。

2026-11-05 交接基线：默认 GPU、Wi-Fi、内屏亮度同时开启；原生 Wi-Fi 基础链、手动亮度及正常 60/120 Hz 已有真机证据。功能基线 `de0d977d654f054da22896de41596f4ba5610a89` 的四项 CI 全通过。主屏约 80 ms 首帧延迟仍保留调查，执行顺序已由上述日用路线调整；[硬件适配计划](docs/plan/2026-11-04-next-hardware-work.md) 保留历史场景设计。

## 2026-11-05 显示、首帧与内屏亮度

最新以 [显示记录](docs/progress/2026-11-05-display-and-backlight.md) 为准。面板池画布改为复用显示初始化时建立的真实缓存映射，失败槽不提供给客户端；g346 记录实际 KVA 复用。历史 g342 的罕见映射失败仍未稳定复现；其“未完成 fence”来自旧等待快照，不能当作实际硬件时间戳。汇总现区分等待快照与直接读取的 CP 完成值，不能宣称所有 GPU 长停已修复。

首帧已拆出 HID、原生 pan gesture、swap 和面板时间。主屏静止首帧 g363 中位数 79.76 ms；提前提交候选 g365 退步至 301.68 ms，已从源码完全撤销。有效 display-link 修复保留，约 80 ms 首帧仍未解决，不能从有新诊断就提升性能状态。

内屏使用自己的 version 2 ADT：DSI0、VC0、最大 2047、非零最小 4。实际读回 307，非零 64/512/307 恢复通过；原生滑条 25%/65%/50% 对应硬件 512/1331/1024，真实 HID 拖动到 1986 再恢复 1024 也通过。22H374 CoreBrightness 空映射初始化崩溃已按本机实际调用栈、映像 UUID 和真实背光服务限定修正，不假造光度校准。DSI 刷新率/亮度通过 owner lease 串行化；仍忙的失败 DMA 不复用。

默认 GPU 配置现编入并启动亮度服务，`pong_backlight=0` 可回退固件亮度；测试探针、滑条动作及延迟观测默认关闭。最终 g374/g375 正常 120/60 Hz 均有完整截图、HTTPS 和直接 CP 完成值；准备 OS/UI/helper、overlay、layout、描述符与默认安装 state 已同步实际干净 trial，旧版本保留。安装前仍核对实际 manifest。新增 Display host regressions，发布后须跟完四个工作流。

## 2026-11-04 晚间 Wi-Fi 接续

最新结果以 [Wi-Fi 设置与跨频段记录](docs/progress/2026-11-04-wifi-settings-and-roaming.md) 为准。`0336379` 在释放临时内核输入后再申请显示池，解决较大 Wi-Fi 内核使 96 MiB 低地址池申请失败的问题。`9cc3938` 在 channel assign 前按目标 AP 更新基础速率，g304/g306 两个方向 2.4↔5 GHz 的首次、主动续租、不同 BSSID 重连三次 HTTPS 200，清理通过且无 RDDM。

`92ef6d2` 从 SpringBoard/wifid/configd 栈确认了原生 InterfaceNamer 的 240 秒等待，镜像配置使用它已有的 30/10 秒 stack/quiet 超时，由 configd 实际发布 Timeout/Complete；没有伪造 Quiet。g309 使用 Settings 的真实选网行、secure text input 和导航栏 Join，收到 900 字节原生 Apple80211 请求，WPA2、DHCP、HTTPS 200 通过。内核自动关联关闭，测试 UI 仅在 `pong_wifi_ui_test=1` 且存在本地权限受限文件时运行；默认不启用。测试文件不提交，默认准备镜像推广前必须移除。

11 月 5 日凌晨接续：`718f73c` 补原生扫描时间戳、RSSI_BOUNDS 和真实授权后的 RSN 通知。g312 证明无测试密码的保存凭据复用；g331/g332 证明系统自动关联和 HTTPS 200，但当时仍周期主动断开。后续进度以 [连接生命周期记录](docs/progress/2026-11-05-wifi-lifecycle.md) 为准：`601f361` 为真实 en0 的 ACK 补偿会话补齐 Setup Interface，解决 wifid 的 PrimaryTimeout；`8f1c86f` 在 firmware dispatcher 外通过有界 FIFO 派发网络通知。g337 的原始 wifid、无 fixture、无设置页启动轮次自动加入后保持至约 309 秒，无 link loss。默认 DMA 改为真实 64 MiB，显式 128 MiB 仍保留失败回退。

g338–g340 的原生开关完成首次 HTTPS 和断开清理，第二次密钥安装后却卡住。`959b00b` 修复共享 timer pass 在递归 pump 摘除后继节点后沿自指节点空转的问题，主机重放复现旧挂住并验证修正；g341 随后取得两组不同 DHCP REQUEST/ACK、两次 HTTPS 200，原生关开和断开清理闭环通过，worker 持续推进。干净 OS 已移除本机开关测试；g342 无 Settings 的网络保持通过，但显示画布失败、早期日志被覆盖，不能记完整 UI 通过。g343 在同一干净 OS 打开原生 Wi-Fi 页，自动入网、HTTPS、零 link loss 和三张完整截图通过。实际干净 OS/UI/helper、overlay、layout、描述符及默认安装 state 已同步，旧准备态完整保留；手机已回 Android。后续安装仍须核对实际 manifest。

按用户后续要求，[设备默认配置已统一开启 GPU 与 Wi-Fi](docs/progress/2026-11-05-default-gpu-wifi.md)。默认构建与打包不再要求 `DEFCONFIG`、`PROFILE` 或 `WIFI=1`；旧 Wi-Fi 配置名只是默认配置的兼容链接。`WIFI=0` 留作关闭无线排障，云端 CI 明确选择精简配置。不同 SSID/子网、自然 T1、长时间稳定性及真实 GIC MSI 尚未通过。下文 g292–g295 的 RDDM/Settings 失败是历史证据，不能作为当前结论。每次安装以实际已写入的 manifest/state 为准，不把旧准备 OS 直接当手机当前版本。

## 2026-11-03 接续

Photos 零尺寸断言修复已在 g262–g264 上机验证，g264 的截图、双配置构建和自动返回记录见 [Photos 记录](docs/progress/2026-11-03-photos-context.md)。GPU fence g253/g254 的时间线全部完成；deadline 最后一笔仅在途 26 ms，见 [fence 记录](docs/progress/2026-11-02-fence-diagnostics.md)。下方 11-02 的 GPU WIP 描述保留为历史状态。

display-link 拦截的当前本机准备镜像已含 hook；UI 库、Metal bridge/plugin 和双配置内核完成主机构建，设备加载与帧率仍未验证。`ui-refresh.sh` 的 Photos 路径与设备前置检查已修正；详见 [主机核对](docs/progress/2026-11-03-displaylink-host.md)。

Wi-Fi 试验配置 `zizhan_ipados18_wifi_defconfig` 已构建，约定的 CI/GPU 两配置也已构建；试验镜像含 `pong_wifi=1` 和自动返回。g265 因主机 USB 无枚举，**没有进入 fastboot boot**，不能作为设备结果。设备连接恢复后从 PCIe、供电、MHI、固件、MSI、扫描逐段验证。细节见 [Wi-Fi 记录](docs/progress/2026-11-03-wifi-bringup.md)，需物理恢复连接的事项见 [待人工清单](docs/progress/HUMAN_CHECKLIST.md)。

后续核对本机 live DT，发现旧试验镜像使用的 Pong PHY 表和一条额外 WLAN 供电请求不适合 zizhan；g266 的 USB 监视已撤下，设备未启动。`configs/zizhan-wifi-sm8475.json` 和 g267 试验镜像已按 zizhan 数据重建，双配置回归成功；仍需 USB 恢复后的自动设备轮次。旧镜像不得当作 Wi-Fi 验证输入。

后续修正了 ath11k 在高位 QMI board ID 分支丢失 `bdwlang` 前缀的问题；g267 未启动即撤下，g268 已重新完成三配置构建与镜像打包。实际 QMI board ID 尚无本机 iPadOS 日志，功能状态保持未验证。

g268 自动轮次工具已补上有界 adb 探测和 `fastboot boot` 的真实退出码传递；约定三配置再次构建通过。已重启 12 小时 USB 恢复等待器，手机仍未枚举，g268 尚无设备日志、截图或自动返回结果。连接恢复后的第一轮仍以 [Wi-Fi 记录](docs/progress/2026-11-03-wifi-bringup.md) 为准。

随后修正自动轮次的 adb 重启和 logfs 拉取无时限问题，并去掉未由 zizhan 输入证实的 Pong PCI subsystem 硬门槛；CI/GPU/Wi-Fi 三配置构建及 g269 镜像检查通过。g268 未启动即撤下，当前 launchd 等待器针对 g269。手机仍未枚举，g269 没有设备日志、截图或返回结果；下次先看 g269 等待器，再按 [Wi-Fi 记录](docs/progress/2026-11-03-wifi-bringup.md) 接续。

并行的音频板级审查从公开 zizhan Android DT 确认 CS35L41 所在 SE15 I²C 与 Pong TFA9873 路径的总线、引脚、地址均不同；只能作为待本机 live DT 复核的主机输入，默认音频仍关闭。见 [音频审查](docs/progress/2026-11-03-audio-board-audit.md)。

22:55 最新设备接续：用户重插后 USB 恢复，原先未启动的 g272 镜像已以 `g272-retry` 临时启动，107 秒后自动回 fastboot，取得新 UFS klog 并返回 Android。供电和 PCIe link/BAR 成功，subsystem 为 `0x010817cb`；Wi-Fi 停在 SMMU context `StateMismatch`（status=7），SID 未接通，没有扫描或联网证据。电脑没有重启；本次只运行原有含本地 WIP 的镜像，没有修改驱动。下一步逐项核对 context 读回，详见 [Wi-Fi 记录](docs/progress/2026-11-03-wifi-bringup.md)。上述 g269 等待与 USB 不可见描述保留为历史状态；当前没有运行中的 USB 等待器。

23:18 Wi-Fi 继续：g273/g274 已完成设备轮次；显式 TBI 和共享 ASID 策略使 g274 通过 SMMU，实际固件/QMI 已启动，board ID=0xff 选择 bdwlang.elf。新失败为 WMI_INIT 返回 -11；已复现 shim 等待宏在成功后重复发送的问题，g275 使用安全等待宏，三配置构建通过，正在上机。当前细节与原始证据见 [Wi-Fi 记录](docs/progress/2026-11-03-wifi-bringup.md)，仍未验证扫描或联网。

23:33 Wi-Fi 最新：g275 已在真机完成扫描（15 个网络），WMI 修正有效；关联、DHCP、公网和真实 GIC MSI 尚未验证。g276 在进入 fastboot 时 USB 未枚举，没有启动。g277 关联测试镜像和三配置构建已准备，当前等待用户重插直连 USB；电脑不重启。私有网络配置只留仓库外，试验输入默认关闭。恢复后继续 [Wi-Fi 记录](docs/progress/2026-11-03-wifi-bringup.md) 的 g277 轮次。

2026-11-04 接续：[Wi-Fi 与 USB 记录](docs/progress/2026-11-04-wifi-usb.md)。后部 USB-A 的 10 次完整切换与稳定等待后的 100168 KB 临时启动通过，Mac 未重启。g279–g282 已验证真实 WPA2 和 DHCP；已定位到 DHCP ACK 含网关/DNS、但系统未发布可用默认网络。g290 的默认 Wi-Fi 路径使用独立租约补偿会话，真机首次联网、原生续租、断线清理与重连三次 HTTPS 均返回 200；Settings 和实际 MSI 仍待验证。g290 无凭据启动及扫描通过；g291 切换到不同 BSSID 后认证成功但无 DHCP 回复，跨 AP 联网未通过。同镜像重跑出现固件 RDDM/MHI 恢复失败，重复日志覆盖前半段，不能确认初始故障阶段；手机已回 Android。下一步先保留首次故障并定位 worker/固件恢复路径。以下之前的 USB 等待描述仅供回溯。

## 历史目标与状态（截至 g298，已被上述 11-05 记录取代）

GPU 最新完成：[g296–g298 设备对照](docs/progress/2026-11-04-displaylink-device.md)，源码 `42629e5`。g298 与 g296 使用同一个启动镜像，只改已安装 UI 库，moving 58.4 → 95.3 fps，抬手后 1–1.5 秒帧间隔约 33.2 → 8.3 ms；首帧仍约 75–80 ms。准备 OS/UI、布局和默认安装状态已同步为已验证 g298，并保留 `.pre-g298` 回退副本；完整 g295/g290 副本也在本机。当前没有设备轮次或 USB 等待器运行，手机已回 Android。

2026-11-04 无人值守接续见 [原生 Wi-Fi 修正记录](docs/progress/2026-11-04-wifi-native-repair.md)。`fbac256` 补原生 IO80211 user client/22H374 加入 ABI、持续忙碌时的有界等待、首次 MHI 故障与原生 wifid manager 端口接线。g292 首次/续租两次 HTTPS 200；跨频段仍 RDDM，g294 的无效 vdev 实验已撤销。g293/g295 原生绑定查询成功，但 Settings 仍 accepted=0，不能宣称 Wi-Fi 全面完成。按用户要求转入 GPU；g296 实际日志发现 UIKit 动画类名与旧 hook 不符，后续以新的 GPU 对照记录为准。

Xiaomi MIX Fold 2（zizhan / SM8475）运行 iPadOS 18.7.10 / 22H374，Darwin 24.6.0 / XNU 11417.140.69，Apple 系统输入为 iPad7,11。基于 UniXNU-pong（同 SoC 的 Nothing Phone (2)）和 uefi-pong；保留公共组件身份 **PongPlatform24**，zizhan 的硬件差异留在设备层。

已验证：8 核 AMP、9728 MiB 受管内存、固定根镜像与可写 Data、内屏触摸、GPU 主屏与 App、120 Hz 面板、DPU 零拷贝三缓冲、电源键长按 3 秒回 fastboot、电池读数与充电拔插。典型合成滑动约 54 fps，120 Hz 面板不等于 App 已跑 120 fps。设置 › 电池能打开，最大容量能读到；健康度仍转圈。设置首次首屏 g237/g238 从 6436 ms 降到 1219 ms，见 [启动记录](docs/progress/2026-11-02-settings-launch.md)。

最新六项 GPU 工作按顺序进行：ASTC 的后续内容覆盖仍待验证；display-link 已完成 g296–g298 设备对照，修正实际 UIKit 动画类名后合成滑动 moving 从约 57 fps 升到 95.3 fps，动画尾段间隔改善，首帧仍约 80 ms；nopreamble、BO 缓存和动态壁纸保持独立后续，截图全宽已有 g248/g249 证据。fairplayd.A2 已从 g213 修改前镜像恢复到当前安装镜像，g245 记录 2560 帧、moving 2453 / 72.0 fps，查找失败为零，但仍 respawn，不能宣称 ASTC / Weather 已修好。**全量构建仍按 EXCLUDED18 排除它**。详细轮次、下一步和待用户验证见最新 GPU 记录。

`c2124d5`、`f3b437b`、`610c0ca`、`67438bc` 已推送；旧记忆中的“等待批准推送”过时，研究报告也已由 `b5fc720` 推送。

## 仓库布局与发布约定

- `board.toml`、`configs/`、`dts/`、`feeds.toml`、`unixnu.lock`：板级选择、OS / SoC / ARM64 层和源码固定版本。以实机 live DT 或 vendor 源码核对 zizhan 常量。
- `drivers/`：PongPlatform24 内核驱动和 PongMetal 前端；`userland/`：iPadOS 18 镜像组装、UI / Settings 适配。
- `patches/xnu-ipados18/series`：有序 XNU 补丁列表；增删补丁时同步 series。构建脚本还有按文件名收集补丁的路径，不能只改列表而留下旧 `.patch`。`patches/metal2vulkan/`、`patches/mesa/`：GPU 用户态补丁。
- `tools/zizhan/mac/`：Mac 构建、部署、轮次和诊断；`tools/zizhan/phone/`：Android 采集、分区与 pinalloc；`tools/zizhan/gen_reserved.py`：live DT 保留区来源。
- `docs/progress/`：中文轮次记录；`docs/research/`：带来源的兄弟项目调研；`docs/plan/`：实验设计；`docs/evidence/`：可公开证据。`migration/` 保存格式 2 与 iPadOS 18 迁移说明。

直接在 git clone 内编辑、提交，更新相关 `porting.toml` findings / evidence / limits / next_action。主机构建和设备结果分开记录；WIP 不能提升为 device-verified。参考 [设备仓库指南](https://unixnu.com/zh/porting/device-repository) 和 [记录指南](https://unixnu.com/zh/porting/records)。

origin 是 `qiumochen/UniXNU-zizhan`，upstream 是 `Chengdong-Yu/UniXNU-pong`。这是保留 Pong 历史的独立仓库，**不是 GitHub fork**；保持 `unixnu` topic，UniXNU 的设备目录同步工作流会读取有效的 board / porting 记录并更新 `devices.toml`。固件另在 [uefi-zizhan](https://github.com/qiumochen/uefi-zizhan)，它是 uefi-pong 的 fork。只推送授权的 origin，禁止强推和改写已发布历史。

内核 / 驱动修改推送前应在 Mac **同时构建** `pong_ipados18_ci_defconfig`（Adreno 关闭）和 `zizhan_ipados18_gpu_defconfig`；GPU 专用符号须置于 `#if XNU_PONG_ADRENO_PLATFORM`，静态链接不能依靠未定义 weak 符号。早期接管只有 Linux 静态检查的限制已被后续 Mac 构建取代；具体提交的通过范围以台账和对应证据为准。

GitHub 的 `Porting record` 继续自动检查每次 push / PR；显示、Wi-Fi、日用回归按相关路径自动触发，公共配置、设备树、格式 2 层、源码固定版本、`scripts/` 和 XNU 补丁变化触发三组。`macos-build.yml` 仅手动触发：`task=kernel` 构建，`task=disk-probe` 单独排查环境。阶段性发布、工具链 / 构建脚本变动或干净环境重建调查时运行云端内核；普通提交不再等待未触发的云端构建，也不能把未运行记为 PASS。跟踪实际触发工作流的精确 SHA；本机双配置要求不变。详见 [CI 效率调整](docs/progress/2026-11-07-ci-efficiency.md)。

发布前审查完整暂存 diff：不带本机路径、主机身份、设备唯一标识或私有输入；照片去 EXIF。只发布源码、配置、文档与脚本；Apple 二进制、手机固件、根镜像、分区备份、凭据和编译产物留本机。git 身份由开发者在本地自行配置。

## 在 Mac Studio 上从仓库继续开发

Mac 是主开发机，直接编辑和构建 clone，无需 Linux 镜像同步。旧 `$HOME/projects/hackios_mixfold2` 非 git 项目及 `src.noindex/` 的 mirror / pre-publish 布局已废弃；历史文档中的 `device/`、`tools/mac/` 指向的内容现已纳入此仓库。旧 `mac-tree.sh` 只服务跨主机工作树同步，包含旧布局假设，不再移植；新环境用正常 git 工作流。

工作根目录与仓库目录是两层：脚本要求 `$ZIZHAN_SRC/UniXNU-zizhan`，**ZIZHAN_SRC 不要指向仓库自身**。

```bash
export PATH=/opt/homebrew/bin:$PATH
export ZIZHAN_SRC="$HOME/zizhan"
mkdir -p "$ZIZHAN_SRC"
cd "$ZIZHAN_SRC"
git clone https://github.com/qiumochen/UniXNU-zizhan.git UniXNU-zizhan
git -C UniXNU-zizhan remote add upstream https://github.com/Chengdong-Yu/UniXNU-pong.git
git clone --recurse-submodules https://github.com/qiumochen/uefi-zizhan.git uefi-zizhan
mkdir -p work out logs
# 脚本相互调用时从工作根目录找同名文件；链接到 clone 中的受控版本。
for tool in "$ZIZHAN_SRC"/UniXNU-zizhan/tools/zizhan/mac/*; do
  [ "${tool##*/}" = ci-env.sh ] && continue
  ln -s "$tool" "$ZIZHAN_SRC/${tool##*/}"
done
# ci_prep.sh 会生成此文件，必须是本地普通文件，不能链接回仓库。
cp UniXNU-zizhan/tools/zizhan/mac/ci-env.sh ./ci-env.sh
```

已有工作区先检查同名文件；上面的链接安装适用于全新目录，遇到冲突先保留旧文件。保留 Homebrew PATH，系统自带旧 bash 的语法限制要考虑。大仓库曾出现 Xcode git Bus error，可用 Homebrew git。准备完整 Xcode、Python 3.12（需 tomllib）、llvm@18、LLVM / lld、dtc、acpica、nasm、lzfse、mtools、Android platform-tools（adb / fastboot）、uhubctl；具体构建依赖以脚本和 CI 为准。

已记录可用主机是 Apple silicon **Xcode 26.6.0**；托管 CI 是 **Xcode 16.4**。这两项是历史验证版本，不表示任意新版 Xcode 均已通过。`PONG_DEVELOPER_DIR` 指向本地完整 Xcode Developer 目录，检查 `xcode-select -p`；新增 `-Wno-*` 前放 `-Wno-unknown-warning-option`，兼顾旧 clang。

依赖与本地文件布局：

| 位置 / 变量 | 用途 |
| --- | --- |
| `$ZIZHAN_SRC/UniXNU-pin` | UniXNU 固定 `0fc0fc1f9ddd0fbd4787738afef38667df5e0a95`，`build_fw.sh` 拉取 |
| `$ZIZHAN_SRC/sheng` | `ci_prep.sh` 拉取固定版参考 ABI / package |
| `$ZIZHAN_SRC/venv`、`work/pong-work`、`work/pong-inputs` | 依赖、XNU 树、输入和构建缓存，可重建 |
| 仓库 `apple/`、`refer/`、`mac-build/`、`out/zizhan/…` | 本地 Apple 组件、开源依赖、DarwinBoot24 和内核产物，git 忽略 |
| `$ZIZHAN_SRC/work/firmware/adreno730` | 本机手机提取的 GMU / SQE / ZAP，`PONG_ADRENO_FIRMWARE` 指向这里 |
| `$ZIZHAN_SRC/work/userland-22H374/prepared`、`ufs-metal` | 本地 OS / SystemOS / Data 镜像及真实 UFS layout，需迁移或从 restore 重建 |
| `$ZIZHAN_SRC/work/gpu`、`out`、`logs` | Metal plugin / bridge、启动镜像、实验日志，可重建；日志先审查再公开 |
| `ZIZHAN_ROOT_4K` | install_pinned.py / 实际连续 extent 给出的根镜像起始 4K 块号；必须按本机核实，不沿用脚本中的旧默认值 |
| `ZIZHAN_DSC` | 本地提取的 22H374 arm64 dyld shared cache 完整文件路径，供 gsym.py 符号化 |
| `ZIZHAN_ASK_FILE` | 可选，本地提示文本文件；boot / cycle 写入待观察提示。不使用时不设置 |

IPSW / restore、shared cache、设备固件、原始 DTB、分区备份和手机上的 pinned root 都**不随 clone 提供**。来源和放置要求见 [proprietary-files.txt](proprietary-files.txt)、[README](README.md) 和 [用户态迁移指南](migration/ipados18/README.zh.md)。ADSP `adsp.mdt` + bNN 通过 `tools/zizhan/mdt2mbn.py` 合并成 adsp.mbn，再用 fw-refresh 部署。完整备份须另外妥善保留；全新 clone 不能证明备份仍在。不要移动或整理 Android `/data` 上的 pinned root / spacer / klog 文件。

建议顺序：

```bash
cd "$ZIZHAN_SRC"
bash build_fw.sh
bash ci_prep.sh
DEFCONFIG=pong_ipados18_ci_defconfig bash ci_kernel.sh
DEFCONFIG=zizhan_ipados18_gpu_defconfig bash ci_kernel.sh
```

`ci_prep.sh` 会覆盖本地 `ci-env.sh`，并可能生成仅供 CI 构建的 placeholder volume layout；**placeholder 不能用于实机**。实机用镜像组装生成的真实 volume UUID / layout，核对本地 ci-env.sh 中的 PONG_VOLUME_LAYOUT、PONG_BUILD_TREE、PONG_INPUTS、PONG_ADRENO_FIRMWARE 等；生成的环境文件可能未包含 firmware 变量，需在本地补上。切换补丁集时必要时清理派生 XNU cache 后重建，不把旧构建树当源码。

## 构建、部署与测试闭环

- `ci_kernel.sh` 编内核；`PROFILE=zizhan_ipados18_gpu bash ci_boot_ufs.sh NAME <root-4K-block>` 预链接和打包，不能当成构建或部署 Metal / UI 的替代。
- `metal-refresh.sh TAG` 构建 bridge + plugin、刷新 OS、重算 layout 并安装 pinned root 的 OS 窗口；`ui-refresh.sh TAG` 更新 UI / Settings 适配；`fw-refresh.sh TAG` 更新固件。它们会写手机根镜像，先确认备份和本地镜像完整，TAG 用新名字保留回退点。
- `jobs-refresh.sh TAG LABEL…` 移除 job；`job-restore.sh TAG FROMTAG LABEL…` 从保留镜像恢复 job / plist（泛化版尚未在 Mac 运行）。它们不改变源码 EXCLUDED18；本地镜像状态需单独记录。
- `ADSP=2 bash round.sh RUN [args…]` 构建 GPU 内核、打包、运行 cycle；首次先核实 ZIZHAN_ROOT_4K、klog 状态和真实 volume layout。
- `cycle.sh IMG RUN [WAIT]`：临时 fastboot boot → deadline 回 fastboot → RamoopsDump 把 ramoops 写到已备份的 logfs → Android / adb 取日志。它涉及日志写入；不是无写入测试。禁止 fastboot flash。
- 中断时用 `cycle-resume.sh IMG RUN` 接后半段；USB 掉线时 `when-back.sh RUN [args…]` 等恢复再跑。`usbkick.sh remember` 在手机可见时记端口；换口后重记。先判断端口枚举，不能把 USB 不可见直接当内核冻结。
- 无人值守等待器如果由临时工具会话直接启动，会随会话结束而退出。要跨会话等待，使用当前用户图形域的 `launchctl submit` 托管 `when-back.sh`，设置 `ZIZHAN_SRC` 和 `WHEN_BACK_IMAGE`，将输出写入本机日志；用 `launchctl list` 和日志心跳核对存活。g269 是[历史示例](docs/progress/2026-11-03-wifi-bringup.md)，不代表有等待器运行；活跃进程写入本机 handoff 并实时复查。
- `klog.py` 把状态保存在工作目录；干净 UFS klog 优先于热机后位翻转严重的 DRAM ramoops。每轮日志保留参数、源码版本、镜像 / plugin / UI 版本、结果和下一步。
- `cycle.sh` 和 `cycle-resume.sh` 经 `pull-logfs.sh` 导出 logfs；导出或拉取失败会清掉本机同名旧副本并停止轮次。只有真实取回的新镜像才能继续提取 `XNULOG.TXT`。

常用参数（内核和打包脚本默认值分开）：

| 参数 | 当前默认 / 用途 |
| --- | --- |
| `pong_autoreset` / `pong_autoreset_sb` | 内核 deadline 默认 300 s；round 未传 deadline 时加 300 / 60。每轮须有可靠回路；勿用 0 关闭 |
| `pong_button_reset` | 默认 3000 ms 长按电源键回 fastboot；保留真实 panic 处理和复位 |
| `pong_touch` / `touch_flip` | 打包默认 touch=0，round 显式 1；翻转默认 2（y 镜像） |
| `ADSP` / `pong_adsp` | ci_boot_ufs 默认 ADSP=0；2 启 DSP + 电池，1 只调查，0 关 |
| `pong_wifi` | 随实际编译驱动选择；设备默认内核为 1，精简 CI 为 0；`WIFI=0` 可显式关闭 |
| `pong_backlight` | 默认完整设备内核为 1；0 保留固件亮度；探针及原生滑条自动操作默认关闭 |
| `pong_audio` | 本地打包默认 0，音频仍单独验证 |
| `pong_panel_hz` | 默认 120，60 回固件速率 |
| `pong_dpu_flip` / `pong_dpu_bufs` / `pong_zc` / `fb_swapwait` | 默认 1 / 2 / 2 / 1；三画布直接 DPU 翻页。`pong_zc_async` 默认关 |
| `pong_gpu_level` / `pong_gpu_deny` | 默认 level 9（710 MHz），默认拒绝 MercuryPosterEx；`-` 放行仅用于受控调查 |
| `pong_ufs_async` | 默认 0，不重复使用已失败的异步路径推断 |
| `pong_fast_pri` / `pong_fast_cpu` / `pong_cpu_boost_ms` | 默认 47 / 7 / 400；UI 优先 X2 和触摸 boost |
| `pong_astc` | 默认保持系统原生回答（CPU 解码）；1 强答支持，2 仅 UI 进程实验，均未验证修复 |
| `pong_dl_full` | 默认拦截指定 link 的低帧率请求；0 保留原速率；g298 已验证对应类名修复，非所有场景帧率保证 |
| `touch_lat=1` / `pong_gtrace=<s>` | 手势与逐帧诊断；display-link WIP 也由 touch_lat 输出 |
| `gs=<ms>` / `gs_proc` / `gs_all` | 手势采样；必须设置 gs 时长，单写 proc / all 不生效；all=2 包含等待线程 |
| `pong_url=<s>` / `pong_pane` | SpringBoard 延时打开设置，默认 BATTERY_USAGE；H 进入电池健康 |
| `pong_shot=<s>` / `pong_shot_ms` | 内核定时截图；可影响性能，仅作视觉检查 |
| `pong_touch_synth=3 tsa=1118` | 合成点 Weather；坐标需截图确认，旧 5096 实际打开 Spotlight |

命令行严格小于 1024 字节，当前打包器按 ≤1020 检查；省略等于默认值的参数，新增 knob 优先合适默认值。AMP 内核必须有 ADT cluster-type（PONG_ADT_CLUSTERS=1）。诊断 `gsym.py LOG GESTURE [TID…] --bin BIN` 符号化等待栈；`settings-ui.py LOG` 解码设置 UI 打印。旧记录里未纳入仓库的临时分析脚本不能假定 fresh clone 已有。

记录检查按 `.github/workflows/porting-record.yml` 的固定 UniXNU 版本准备检查器，在其 components.toml 加入 `migration/format2/components.toml` 后运行：

```bash
python3 <UniXNU-checkout>/scripts/porting-record.py check --device "$ZIZHAN_SRC/UniXNU-zizhan"
python3 <UniXNU-checkout>/scripts/porting-record.py check --device "$ZIZHAN_SRC/UniXNU-zizhan" --base <本轮改动前的已发布提交>
```

`--base` 只比较已提交代码。开发分支可用 `origin/main`；main 已推送后必须使用推送前 SHA，不能用已经等于 HEAD 的 `origin/main` 得到空比较，也不使用兄弟项目 `upstream/main` 代替本仓库增量基准。CI 自动使用 push-before 或 PR-base SHA。2026-11-04 审核见 [上游与 CI 核对](docs/progress/2026-11-04-upstream-ci-audit.md)：记录检查器与构建工具分别固定版本；最新统一 package 接口尚未迁移，不能宣称最新仓库规范全部完成。

## 工作方式与教训

先读兄弟项目的一手代码、提交和日志，再设计方案并引用：Pong、sheng、dada、hakios / hakios-metal、AdrenoMetal、iOSPlus10、Linux / KGSL。已有 [调研](docs/research/2026-11-02-sibling-gpu-update.md) 是起点；别把其他 SoC 或 OS 的收益直接记成本机结果。自行推进可完成的工作；需要用户看屏幕、拍照、插线或按键的事项汇总成进度文档的 **待用户验证**，不要逐项阻塞提问。

- 11-01 ASTC “大幅提速”实际是 App 全黑；先 pong_shot 验内容，再量无截图性能。日志无错误不是 PASS。
- g152 系列漏 autoreset 曾被误判总线冻结，之后已撤回该结论。检查完整 cmdline 和复位条件后再归因。
- MDSS 显示 AHB 时钟可能关着，即使屏幕仍扫描；先按阶段验证时钟和资源再 MMIO。不能读越过显示组的 hypervisor SMMU 寄存器；写 SMMU 全局寄存器曾挂住。
- loader panel pool 位于 XNU 管理 RAM 之外；映射须用 copyback cache 属性，device-like 映射上的 bzero / dc zva 曾冻结。不要照搬别板地址。
- m2v 64 MiB 块反复 wire / free 曾造成每手势 >150 ms 卡顿；keep-two-empty-blocks 已缓解。GPU 固频、GMEM 对旧测量未带来收益，先有新的测量再重试。
- fairplayd、IOPlatformUUID 和 MobileGestalt 的等待要沿同步 XPC / 内核栈查，不能靠伪造服务绕过。ADT 种子用于软件身份；本公开交接不复制任何值。
- GPU 客户端退出的用户映射生命周期修复已有 unmap / release 与 pmap_disconnect 兜底；更完整的 VM-object BO 方案尚待研究，不能靠禁用真实 panic 掩盖错误。
- 修改已应用的 XNU 补丁先处理旧补丁 / 缓存，再重建；应用应使用 forward，防止重叠补丁被反向撤销。
- 连续轮次发热会污染 ramoops；cycle 先冷却到 35 °C。尚无完整温控，不能把长时间稳定运行当作已验证。

## 已知问题与接续入口

任务优先级统一维护在 [当前台账](docs/progress/STATUS.md)，不在本文件复制旧队列。首帧约 80 ms、罕见映射失败回归、网络长稳/换网及未接通硬件的边界分别列出。Wi-Fi 和内屏手动亮度已有真机闭环；旧截图右侧缺失已证明是日志缺列，不能继续列作屏幕黑区故障。

电池健康仍缺 thermalmonitord 的 ageAwareMitigationState 和 powerd 的 Battery Service State，不能伪造“正常”；用量图表需累计使用。动态壁纸仍受 GPU 限制；fairplayd 的当前安装与全量构建 EXCLUDED18 策略差异要单独验证。每次交接同时更新台账与仓库外本机 handoff，历史记录保留原条件和负面证据。
