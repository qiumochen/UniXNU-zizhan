# zizhan 开发交接

本文件是从旧项目记忆整理的公开交接，基础内容更新至 **2026-11-02 09:39（UTC+8）**；2026-11-03 的接续见下段。先读 [AGENTS.md](AGENTS.md)、[porting.toml](porting.toml) 和 [最新进度](docs/progress/README.md)。历史轮次以对应进度文档为准；本文件不保存机器身份或设备标识。

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

2026-11-04 接续：[Wi-Fi 与 USB 记录](docs/progress/2026-11-04-wifi-usb.md)。后部 USB-A 的 10 次完整切换与稳定等待后的 100168 KB 临时启动通过，Mac 未重启。g279–g282 已验证真实 WPA2 和 DHCP；已定位到 DHCP ACK 含网关/DNS、但系统未发布可用默认网络。g282 补偿未生效，公网和 Settings 尚未通过。以下之前的 USB 等待描述仅供回溯。

## 目标与当前状态

Xiaomi MIX Fold 2（zizhan / SM8475）运行 iPadOS 18.7.10 / 22H374，Darwin 24.6.0 / XNU 11417.140.69，Apple 系统输入为 iPad7,11。基于 UniXNU-pong（同 SoC 的 Nothing Phone (2)）和 uefi-pong；保留公共组件身份 **PongPlatform24**，zizhan 的硬件差异留在设备层。

已验证：8 核 AMP、9728 MiB 受管内存、固定根镜像与可写 Data、内屏触摸、GPU 主屏与 App、120 Hz 面板、DPU 零拷贝三缓冲、电源键长按 3 秒回 fastboot、电池读数与充电拔插。典型合成滑动约 54 fps，120 Hz 面板不等于 App 已跑 120 fps。设置 › 电池能打开，最大容量能读到；健康度仍转圈。设置首次首屏 g237/g238 从 6436 ms 降到 1219 ms，见 [启动记录](docs/progress/2026-11-02-settings-launch.md)。

最新六项 GPU 工作按顺序进行：ASTC 布局 WIP 已试跑，正确绘制未验证；display-link 速率拦截 WIP 尚未构建、未上机，中断前未同步到 Mac；nopreamble、内存池 / BO 缓存、动态壁纸、截图全宽问题尚未开始。fairplayd.A2 已从 g213 修改前镜像恢复到当前安装镜像，g245 记录 2560 帧、moving 2453 / 72.0 fps，查找失败为零，但仍 respawn，不能宣称 ASTC / Weather 已修好。**全量构建仍按 EXCLUDED18 排除它**。详细轮次、下一步和待用户验证见最新 GPU 记录。

`c2124d5`、`f3b437b`、`610c0ca`、`67438bc` 已推送；旧记忆中的“等待批准推送”过时，研究报告也已由 `b5fc720` 推送。

## 仓库布局与发布约定

- `board.toml`、`configs/`、`dts/`、`feeds.toml`、`unixnu.lock`：板级选择、OS / SoC / ARM64 层和源码固定版本。以实机 live DT 或 vendor 源码核对 zizhan 常量。
- `drivers/`：PongPlatform24 内核驱动和 PongMetal 前端；`userland/`：iPadOS 18 镜像组装、UI / Settings 适配。
- `patches/xnu-ipados18/series`：有序 XNU 补丁列表；增删补丁时同步 series。构建脚本还有按文件名收集补丁的路径，不能只改列表而留下旧 `.patch`。`patches/metal2vulkan/`、`patches/mesa/`：GPU 用户态补丁。
- `tools/zizhan/mac/`：Mac 构建、部署、轮次和诊断；`tools/zizhan/phone/`：Android 采集、分区与 pinalloc；`tools/zizhan/gen_reserved.py`：live DT 保留区来源。
- `docs/progress/`：中文轮次记录；`docs/research/`：带来源的兄弟项目调研；`docs/plan/`：实验设计；`docs/evidence/`：可公开证据。`migration/` 保存格式 2 与 iPadOS 18 迁移说明。

直接在 git clone 内编辑、提交，更新相关 `porting.toml` findings / evidence / limits / next_action。主机构建和设备结果分开记录；WIP 不能提升为 device-verified。参考 [设备仓库指南](https://unixnu.com/zh/porting/device-repository) 和 [记录指南](https://unixnu.com/zh/porting/records)。

origin 是 `qiumochen/UniXNU-zizhan`，upstream 是 `Chengdong-Yu/UniXNU-pong`。这是保留 Pong 历史的独立仓库，**不是 GitHub fork**；保持 `unixnu` topic，UniXNU 的设备目录同步工作流会读取有效的 board / porting 记录并更新 `devices.toml`。固件另在 [uefi-zizhan](https://github.com/qiumochen/uefi-zizhan)，它是 uefi-pong 的 fork。只推送授权的 origin，禁止强推和改写已发布历史。

内核 / 驱动修改推送前应在 Mac **同时构建** `pong_ipados18_ci_defconfig`（Adreno 关闭）和 `zizhan_ipados18_gpu_defconfig`；GPU 专用符号须置于 `#if XNU_PONG_ADRENO_PLATFORM`，静态链接不能依靠未定义 weak 符号。本次接管保存现有 WIP 时只有 Linux 静态检查，双配置与 plugin / UI 构建尚未执行，不视为通过。

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
- 无人值守等待器如果由临时工具会话直接启动，会随会话结束而退出。要跨会话等待，使用当前用户图形域的 `launchctl submit` 托管 `when-back.sh`，设置 `ZIZHAN_SRC` 和 `WHEN_BACK_IMAGE`，将输出写入本机日志；用 `launchctl list` 和日志心跳核对存活。当前 g269 任务见 [Wi-Fi 记录](docs/progress/2026-11-03-wifi-bringup.md)。
- `klog.py` 把状态保存在工作目录；干净 UFS klog 优先于热机后位翻转严重的 DRAM ramoops。每轮日志保留参数、源码版本、镜像 / plugin / UI 版本、结果和下一步。
- `cycle.sh` 和 `cycle-resume.sh` 经 `pull-logfs.sh` 导出 logfs；导出或拉取失败会清掉本机同名旧副本并停止轮次。只有真实取回的新镜像才能继续提取 `XNULOG.TXT`。

常用参数（内核和打包脚本默认值分开）：

| 参数 | 当前默认 / 用途 |
| --- | --- |
| `pong_autoreset` / `pong_autoreset_sb` | 内核 deadline 默认 300 s；round 未传 deadline 时加 300 / 60。每轮须有可靠回路；勿用 0 关闭 |
| `pong_button_reset` | 默认 3000 ms 长按电源键回 fastboot；保留真实 panic 处理和复位 |
| `pong_touch` / `touch_flip` | 打包默认 touch=0，round 显式 1；翻转默认 2（y 镜像） |
| `ADSP` / `pong_adsp` | ci_boot_ufs 默认 ADSP=0；2 启 DSP + 电池，1 只调查，0 关 |
| `pong_wifi` / `pong_audio` | 打包均为 0，未接入 |
| `pong_panel_hz` | 默认 120，60 回固件速率 |
| `pong_dpu_flip` / `pong_dpu_bufs` / `pong_zc` / `fb_swapwait` | 默认 1 / 2 / 2 / 1；三画布直接 DPU 翻页。`pong_zc_async` 默认关 |
| `pong_gpu_level` / `pong_gpu_deny` | 默认 level 9（710 MHz），默认拒绝 MercuryPosterEx；`-` 放行仅用于受控调查 |
| `pong_ufs_async` | 默认 0，不重复使用已失败的异步路径推断 |
| `pong_fast_pri` / `pong_fast_cpu` / `pong_cpu_boost_ms` | 默认 47 / 7 / 400；UI 优先 X2 和触摸 boost |
| `pong_astc` | 默认保持系统原生回答（CPU 解码）；1 强答支持，2 仅 UI 进程实验，均未验证修复 |
| `pong_dl_full` | 新 WIP 默认拦截指定 link 的低帧率请求；0 保留原速率，未验证 |
| `touch_lat=1` / `pong_gtrace=<s>` | 手势与逐帧诊断；display-link WIP 也由 touch_lat 输出 |
| `gs=<ms>` / `gs_proc` / `gs_all` | 手势采样；必须设置 gs 时长，单写 proc / all 不生效；all=2 包含等待线程 |
| `pong_url=<s>` / `pong_pane` | SpringBoard 延时打开设置，默认 BATTERY_USAGE；H 进入电池健康 |
| `pong_shot=<s>` / `pong_shot_ms` | 内核定时截图；可影响性能，仅作视觉检查 |
| `pong_touch_synth=3 tsa=1118` | 合成点 Weather；坐标需截图确认，旧 5096 实际打开 Spotlight |

命令行严格小于 1024 字节，当前打包器按 ≤1020 检查；省略等于默认值的参数，新增 knob 优先合适默认值。AMP 内核必须有 ADT cluster-type（PONG_ADT_CLUSTERS=1）。诊断 `gsym.py LOG GESTURE [TID…] --bin BIN` 符号化等待栈；`settings-ui.py LOG` 解码设置 UI 打印。旧记录里未纳入仓库的临时分析脚本不能假定 fresh clone 已有。

记录检查按 `.github/workflows/porting-record.yml` 的固定 UniXNU 版本准备检查器，在其 components.toml 加入 `migration/format2/components.toml` 后运行：

```bash
python3 <UniXNU-checkout>/scripts/porting-record.py check --device "$ZIZHAN_SRC/UniXNU-zizhan" --base upstream/main
```

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

## 已知问题与接续优先级

先完成 GPU 记录中的 g245/g239 分析、fairplayd 策略和 ASTC 视觉验证，再测试 display-link，随后依次做 nopreamble、内存、壁纸、截图。当前没有足够证据决定删去 EXCLUDED18。

电池健康缺 thermalmonitord 的 ageAwareMitigationState 和 powerd 的 Battery Service State，不能伪造“正常”；用量图表需累计使用。外屏 / 外屏触摸、亮度、Wi-Fi、音频、蓝牙、传感器、休眠唤醒均未验证可用。动态壁纸被限制 GPU；截图右侧和 HUD 问题未解。待用户验证清单在各进度文档末尾维护，最新项以 GPU 交接为准。
