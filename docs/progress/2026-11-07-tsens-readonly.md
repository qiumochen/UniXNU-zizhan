# THERM-01：TSENS-v2 有界只读观察（2026-11-07）

用户授权按优先级继续无人值守。入口源码为 `6d074ef09e5777cecfa23612e626ce2c3878153e` 加既有 11-06 WIP；本轮变更仍以 dirty snapshot 标识，不能当作该提交自带的功能。当前队列见 [STATUS.md](STATUS.md)。原 OS370 与完整本地主机候选维持原有资格；本项不改变服务策略、默认热控制或安装状态。

## 寄存器及地址依据

本机已保存的 live FDT 中，TSENS 资源按 TM、SROT 顺序分别为 `0xc263000/0xc222000` 和 `0xc265000/0xc223000`，各 511 bytes、16 sensors。`/soc` 的 address/size cells 为 1/1，根 address cells 为 2；ranges 四个 cells 为 `0,0,0,0xffffffff`。本次明确核对了物理地址恒等变换；缺失或不同 ranges 时不生成可用 MMIO 输入。`thermal-inventory.py` 离线解析并生成该结论，完整原始 FDT 留仓库外。

协议来自固定 Linux v6.12 主源，而非另一块板的温度常量：

- [tsens-v2.c](https://github.com/torvalds/linux/blob/v6.12/drivers/thermal/qcom/tsens-v2.c)：SROT version `0x0`、control `0x4`；major 为 bits 31:28，使能 bit 0、reset bit 1。TM status `0xa0+4*n`，温度 bits 11:0、valid bit 21，TRDY `0xe4` bit 0。
- [tsens.c](https://github.com/torvalds/linux/blob/v6.12/drivers/thermal/qcom/tsens.c)：分离资源的第 0 项为 TM、第 1 项为 SROT；v2 非 ADC 温度以 signed 12-bit deciC 表示，乘 100 得 milliC。
- [sm8450.dtsi](https://github.com/torvalds/linux/blob/v6.12/arch/arm64/boot/dts/qcom/sm8450.dtsi)：与本机两组地址和 `qcom,tsens-v2` 协议一致。这只是第二来源核对，不取代本机 live FDT。

本地取得的 `tsens-v2.c` SHA256 为 `9c6603dc6fff25f4c191e148df28edc08f2b35e0f556bc20dc9a1986759442f9`，`tsens.c` 为 `69598283349ffea15f0264d466ef1478b68655e3d3e2ed844c3861d51e664c88`。未复制 GPL 实现；观察核心仅按已审查协议独立实现。Android debugfs 版本只读查找没有找到可用节点，不能声称已读到本机硬件版本。

## 候选行为与资格

`ZizhanTsensCore.h` 负责输入检查、读取前置和解码；`ZizhanTsensProbe.h` 从 `/chosen/zizhan-tsens-v2` 读取 96-byte v1 资源契约。ADT 生成器从本机板级 JSON 编码该属性；缺失/错误版本、长度、范围、传感器 mask 或资源重叠均在 MMIO 前拒绝。资源存在不代表硬件温度可读。

只有显式 `zizhan_tsens=1` 才创建一个观察线程，默认无硬件读取。等待正常 root 挂载最多 120 秒，然后延迟 5 秒，仅执行一次采样；不是后台温控 daemon。每 bank 先检查 version major=2、enable=1、reset=0 和 ready=1，失败输出 SKIP 并跳过后续寄存器；成功时读取 16 个 status，每次从同一个 status word 判断 valid 和解码温度。读取无重试、无无限轮询；任何 invalid 输出 FAIL，不把缺失值填为零摄氏度。每 bank 最多 19 次 32-bit read，两组映射均释放。

本项不写 TSENS enable/reset/IRQ/threshold，不写 EPSS/频率/时钟，不启动压力负载，也不发布伪造的温控服务。真实 MMIO 异常仍由原内核异常/panic 路径处理。软件循环有界不代表总线异常可由软件超时解除；设备试验必须保留已验证 autoreset/panic 返回链。

验收拆分：

1. 主机：生产解码器全部 4096 种 signed 编码/invalid、读取前置门控、采样次数、资源拒绝、实际 ADT wire 往返；完整默认和精简 CI 内核均须构建。
2. 设备：新日志实际硬件版本/使能/ready、32 项有效温度及正常启动/返回分别记结果。SKIP/FAIL 不升级资格；不重跑不变失败。默认关闭路径另做控制时须说明目的。
3. 后续温控：跨系统传感器/单位核对、持续采样和过期/错误处理、可信热策略输入、真实限频与恢复分别推进。本次没有阈值或热控制资格。

## 结果

初轮主机回归：thermal 12、TSENS 6、policy audit 7、service policy 6，共 31 项通过。生产 C++ 核心运行 AddressSanitizer/UBSan，4096 编码及门控/资源边界均通过。内核构建、设备准备/启动结果将在完成后追加；此段不表示已准备或上机。

后续主机回归为 34 项：新增实际 ranges 正/负控和 BOOT-01 patch forward/reverse roundtrip、损坏格式负控。Display 与固定 Sheng Ath12kWifi 输入的 Wi-Fi 回归通过。首次 Wi-Fi runner 使用了不存在的 `--ath11k` 参数，在解析阶段退出；更正为实际 `--shared` 后通过，原失败留本机，不当作驱动失败。

重复构建暴露既有 WIP `0019` 的三条 context 行缺少 unified-diff 空格前缀。`patch --forward` 首轮接受了它，后续 `git apply --reverse --check` 判 corrupt，继而阻碍 `0001` 的检查。先前生成缓存已保存而非删除；本轮补齐这三处前缀，不改变产生的 C 代码，并加入正反应用与损坏负控。修正后精简 CI、再次完整默认内核均 `KERNEL_DONE`；重复默认构建通过，无需再次重建生成缓存。

### g395 设备设计

- 假设：固件已使能的两组 TSENS-v2 在 XNU 启动后仍可只读观察。控制为 g393 的完整 profile/OS370；只新增 TSENS 观察路径和 ADT 资源 metadata，命令行除 epoch 外仅增加 `zizhan_tsens=1`。
- 源码：`6d074ef09e5777cecfa23612e626ce2c3878153e` 加 dirty snapshot，变更文件 manifest SHA256 `ecd73a85dc855a944d0d361c91228cffcccf42c222073caa33ae319f31b87716`。50 份改动源文件副本留仓库外；构建、源 diff、输入/镜像身份单独留存。
- 临时启动镜像 SHA256 `3c9a53e9e4703bd55afcbf1f70dae3f8a0593e4e52354e16ed42e7dfb875edb5`，实际 stage kernel `c0c57e6c98f5d88d9b8d9ff4a66166ce9fd278be6271edfc1b808b4536a046da`，ADT `a5b069a3e98c0f01f160b6f1e725737c4f187fff3e26b06d3538b400e427e51d`。实际打包 ADT 的资源属性已与 reviewed JSON 比对。
- OS/UI/helper 维持已验证 OS370；live OS 整体、根/klog extent、manifest、备份文件在位、温度及受控 USB-A preflight 已通过。g394 是 patch 格式收口前的主机准备包，未上机、不作为本轮输入。
- 914-byte 命令行，`pong_touch=0,pong_audio=0,pong_adsp=0`；`pong_autoreset=300,pong_autoreset_sb=180`，保持真实 panic/电源键返回。后部 USB-A，仅 temporary boot，不安装/flash OS。
- 单轮 supervisor 最大 18 分钟、15 秒心跳、设备独占锁、caffeinate 及 fresh klog；必要时只允许一次明确可见 fastboot reboot，最多等待 Android 120 秒。无 transport 时不再发命令，不循环重试。
- 验收：2 banks 的实际 version/control/ready、32 个 valid 温度；fresh deadline klog、无 panic、Android `boot_completed=1`。前置失败为 SKIP，invalid/缺失为 FAIL；不延伸为 UI、长期温监、温控或功耗资格。OS370 不变即为回退；失败后保存现场，不重跑不变输入。

09:05:05（UTC+8）单次 supervisor 已实际开始，状态 `RUNNING`，手机入口电池 25.8°C；launchd 单次托管、心跳和 cycle PID 已核对。此时仍在启动流程，尚无 TSENS 或启动/返回结果。

### g395 结果与门控纠正

09:09:52 已收口：临时启动、228 秒后自动返回、1,340,063-byte fresh deadline klog 和 Android `boot_completed=1` 均通过，未发现 panic；设备退出温度 30.0°C，OS370 未写。supervisor 与设备锁正常退出，单次 launchd 已清理。没有截图，因此不新增画面资格。

**温度结果为 SKIP**：两组 version 均为 `0x20080000`（2.8.0）、control `0x9d67fff9`（enable=1、reset=0）、TM_TRDY `0x8`，候选因 bit0=0 跳过全部温度读取。零样本不是温度 PASS，不能推断传感器失效。

上述候选采用了过严的 TRDY 门控。重新逐行核对 Linux v6.12：v2 的 `ops_generic_v2.get_temp` 是 `get_temp_tsens_valid`，该函数只轮询每 sensor VALID 后解码；TRDY 轮询在另一条 `get_temp_common` 的 VER_0 分支，不是 v2 的读取前置。g395 成功验证了硬件版本/使能的只读访问，也暴露这个候选协议解释错误。前文“ready 必须=1”仅描述 g395，现已撤销为后续候选的要求，不静默改写本轮 SKIP。

下一候选保留 version/enable/reset 前置和同-word VALID；TRDY 仅记录原值，不用它拒绝 v2 温度读取。仍只执行一次、有界 32 sensor 读取，invalid 仍失败，不增加使能、阈值、IRQ 或频率写入。新增 raw TRDY=0/8 但 sensor VALID=1 的主机对照；重新构建完整/精简内核后另行冻结输入和上机。不能重用 g395 名称或将其改记温度通过。

### g396 设计（准备态）

纠正后的实际 C++ 核心及全部 34 项 host、精简 CI 和完整默认内核均通过。g396 相对 g395 的唯一协议变量为取消额外 TRDY 门控；其余读取前置、采样次数、资源契约和退出逻辑不变。命令行对照只有 epoch 变化，仍为 914 bytes、`zizhan_tsens=1`、autoreset300/SB180；不增加压力或硬件写入。

完整 source revision 仍为 `6d074ef09e5777cecfa23612e626ce2c3878153e` 加独立 dirty snapshot，manifest SHA256 `cec7359316fd601a2b943d996b7b6fbb30478b80c435e5d5b8dee232b93431fb`。临时镜像 SHA256 `5c31b871ce91d8c9c4d0bd7cd195f5ce4c46ebc5d5381928497e121bc8fde2d5`，实际 stage kernel `b7335f29b25ee84c919a84fedf2d30da77126392d2107da5897dc7438991b6cc`，ADT `f0008d782f387afe0f9be92aaaf240993b32facc739415dce082a4a227c7e9f5`。源副本、diff、kernel/build/input identity 留仓库外。

新 preflight 重新核对 Android、温度、备份清单、root/klog extent、manifest、OS370 全量 live hash 和 USB-A，通过后才启动。单次 supervisor 18 分钟硬截止、15秒心跳、设备锁、caffeinate、fresh deadline klog 和单次可见 fastboot 恢复沿用已验证机制。验收为两 bank/32 valid 温度及无 panic 的正常 Android 返回；若 invalid/skip 则如实记录，不重试不变候选。仍不新增 UI、热策略、持续监控或功耗资格。

09:14:56（UTC+8）g396 单次 supervisor 已开始，入口温度29.4°C；09:15:21 实际发送/临时 boot 完成。心跳正常，RUNNING，等待本轮自动返回和新日志；此时不能登记温度结果。

### g396 结果与最终状态

**一次温度观察 PASS**：两个 bank 的 version 均为 2.8.0、control 使能且不在 reset、raw TRDY 均为 `0x8`；32 个 sensor 各有唯一一条 VALID 温度，summary 为 `PASS samples=32 failed=0 skipped-banks=0 readonly=1 finished=1`。离线独立核对每个原始 word 的 VALID 与 signed 12-bit ×100 解码，无 missing/duplicate/invalid。bank0 范围 38.2–46.9°C，bank1 范围35.5–39.5°C。这是一次有效 last-temperature 寄存器读取，不是光度/温度校准、读数更新新鲜度或持续热管理资格。

09:19:10 自动返回 fastboot（228 秒），09:19:43 取得 1,350,838-byte fresh deadline klog，原始 log SHA256 `b1f9f560255f02ed3f2052b21d9215d3996a3dff03d1cb13710027a4e6136ff6`；没有 panic，未复现 CS4K 分配失败。无截图，不提升画面/应用资格。

**原 supervisor 仍记 FAIL**：cycle rc=0，但它在09:19:43只检查一次 `boot_completed`，Android framework 此时尚未完成。该记录没有覆盖或改绿。后续有界只读复核于09:20:29得到 `boot_completed=1`，距 supervisor finish 46.224秒，小于120秒限制；因此设备返回单独记为 follow-up PASS。09:21:06 再查 ADB=device、boot_completed=1、product=zizhan，电池32.6°C。以后普通返回监督应等待有限 Android 完成窗口，避免把 ADB 刚出现直接当成完整系统就绪；不为这个监督时序问题重跑内核。

两个单次 launchd 已 bootout、设备锁释放，进程表没有 cycle/installer/waiter。当前活动构建导出恢复完整默认 GPU/Wi-Fi/backlight；手机停在 Android，不运行候选内核。OS370 仍是原安装/回退；BASE-01 full candidate 未安装。没有 commit/push 或新云端 CI。

[脱敏摘录](../evidence/2026-11-07-tsens-readonly.txt)保留 g395 SKIP 与 g396 PASS，并区分 supervisor FAIL 和返回复核 PASS。完整原始日志、源副本、manifest、镜像、preflight、逐项资格 JSON 和离线复核脚本只在本机。

下一开发动作：先设计有界多时点采样，明确 last-temperature 更新/过期/无效语义并交叉核对传感器映射；再调查 vendor 热策略的真实解码/控制输入，不能以本轮 35.5–46.9°C、试验前35°C冷却条件或 DT 高温 trip 设置系统阈值。POWER-02 的平台能力查询与 L0/L1 候选可独立按[恢复契约](../plan/2026-11-07-power-resume-contract.md)推进；首次真睡眠的人工/独立返回前置不变。

### 返回监督收口（主机脚本）

g396 收口后新增 `wait-android.py`，`cycle.sh` 在保留日志后、宣告返回前调用它：默认最多120秒，逐次核对 `get-state=device`、`product=zizhan`、`boot_completed=1`，单条ADB超时受全局剩余期限限制；未知/缺失状态继续有界等待，其他产品立即失败。原返回 witness 文件存在时拒绝覆盖，所有操作只读。循环超时退出8，不伪造成功、不发恢复/重启命令。

6项模拟回归覆盖 ADB早于framework、错误产品、缺失transport、命令超时、全局期限后才返回1以及非法时间参数。完整日用host现为 **40项PASS**；shell语法通过。新CLI在当前Android只读实查一次、0.081秒完成PASS。该脚本在g396之后加入，尚未用新的完整设备cycle回归；不改写g396原supervisor FAIL，也不为验证等待脚本重复同一内核。此处只有host脚本/测试变更，内核/ADT输入未变，不额外重建内核。

最终检查使用 CI 同版 `e67a79eb64012e8f30ba04c56ea37753bdb720a2` record checker 的仓库外副本，合并本仓库 Pong component catalog；结构14 areas、包含未跟踪文件的54项改动归属检查通过。相对文件链接、任务ID唯一性、公开个人绝对路径和 `git diff --check` 通过。g396 kernel/ADT/runtime源输入仍匹配冻结 manifest；没有新提交、推送或云端CI。
