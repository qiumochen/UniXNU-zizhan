# 蜂窝与基带可行性（2026-11-19，只调研，未上机）

## 别人做到了哪里

- **UniXNU/QcomBaseband**（BSD-2-Clause，作者 HoshinoYumeka，最近推送 2026-11-09）在 **SC7180 Chromebook coachz + iOS 17（21H461）** 上：
  - 高通 modem 跑自己的原厂固件（MBA/MPSS + 原厂 FSG/EFS/NV）。
  - QMI 走 GLINK/QRTR，桥接层向 iOS 发布苹果的 PCIe 基带类（`AppleBaseband`、`AppleBasebandPCIControl`、各通道 `AppleBasebandPCIInterface`）。ABM、BBUpdater、CommCenter 以为自己在和苹果 PCIe 基带说话。
  - 苹果的 `.bbfw` 启动交互由 SAHARA 端点吞掉；苹果私有 QMI 请求回 `QMI_ERR_NOT_SUPPORTED`，激活和卡槽状态由桥接层应答。
  - 数据面走 IPA v4.2 + GSI，QMAP mux 1–4 对应 `pdp_ip0..3`。
  - 实机结果（据其文档）：IMEI/ICCID 显示，LTE 注册，状态栏显示运营商和信号格，蜂窝 IPv4 网页加载通过；CommCenter 需要一个经哈希核对的补丁副本（缺 Apple postponement ticket）；EFS 写入只留在内存。IPv6、APN 编辑、长连接、eSIM 开通还待验证。
- 上游指南：UniXNU `docs/zh/porting/baseband.md`（按层拆分：SoC 启动 → QMI 控制桥 → iOS 接口 → 数据通路）。

## 本机已有的积木

| 需要 | 本机现状 |
| --- | --- |
| 用 SCM PAS 加载并启动远端处理器 | 有：`PongAdsp.cpp` 自己加载 `adsp.mdt` + DTB 镜像，PAS_INIT_IMAGE / MEM_SETUP / AUTH_AND_RESET / SHUTDOWN 都已实机用过 |
| SMEM、SMP2P、IPCC、AOSS | ADSP 路径已用（音频 GPR/PRM 跑在上面） |
| QRTR / QMI | Wi-Fi 已有 QRTR（走 MHI），QMI 编解码在 ath11k 路径里 |
| MPSS 保留内存 | ADT 里有 `mpss_region@8bc00000`（`configs/pong-memory-sm8475.json`） |
| modem 固件 | Android 的 `/vendor/firmware_mnt`（modem.mdt 等），需按 ADSP 的方式拉到本机 |

## 还缺什么（工作量从小到大）

1. **MPSS 启动**：PAS 加载 modem 固件（具体 PAS id、是否带 DTB 镜像，要按 sm8475 的 Android 设备树核实）；SMP2P 就绪握手；看门狗与崩溃（SSR）后的安全关停。
2. **rmtfs**：modem 启动时会通过 QMI 要 EFS（modemst1/modemst2/fsg）。可以从 UFS **只读**取这些分区，写回只留在内存（coachz 也是这么做的），不写分区。
3. **GLINK 上的 QRTR 通道**（modem 侧），然后用 QMI 列服务：DMS、NAS、UIM、WDS、WDA、DPM。
4. **CommCenter 桥**：移植 QcomBaseband 的控制桥；按 iPadOS 18（22H374，iPad7,12 蜂窝版）重新核对 UserClient 类名、方法号和 ADT 身份（coachz 用的是 iOS 17 与 J121AP/mav16）。
5. **数据面 IPA**：sm8475 这一代的 IPA 版本比 v4.2 新（具体版本待核实），GSI 与寄存器布局要重做，是最大的一块。

## 建议的阶段与边界

- **阶段 1（可无人值守、静音、不开射频）**：启动 MPSS，rmtfs 只读，建立 QRTR，列出 QMI 服务，用 DMS 读固件版本，并保持 **低功耗模式（LPM，不发射）**；最后 PAS 关停 modem。验收：服务列表与版本号；返回 Android 后基带正常。
- **阶段 2（需要你决定）**：让 modem 上线（会用你的 SIM 卡注册网络，即射频发射）、接 CommCenter、做数据面。开始前需要你确认：用哪张 SIM、能否接受注册网络。
- 不做：改 IMEI/身份、伪造运营商或苹果激活凭证；不写 modem 分区。

## 风险

- MPSS 崩溃会触发看门狗重启：回到 fastboot/Android，和其他内核实验一样可恢复。
- EFS 只读意味着 modem 无法持久化校准或网络缓存；每次启动都从原厂数据开始（coachz 同样如此）。

## 实测进度（2026-11-19 深夜）

- 阶段 1a 已完成：固件与 EFS 在 iPadOS 下只读可达（见[晚间记录](../progress/2026-11-19-evening-daily-default.md)）。
- 阶段 1b：基带已启动到 ready + handover（modem-6）。关键点是 PAS 元数据必须在 4 GiB 以下（Android 用 `linux,cma`）；随后的显示路径 panic 待补 `rproc_ddr` 带宽投票后复测。
- 2026-11-20 凌晨：阶段 1 完成。GLINK + QRTR + pd-mapper + 内存影子 rmtfs 后基带完整初始化，公布 45 个 QMI 服务；只读 DMS 读出固件版本，工作模式 OFFLINE。详见晚间记录“凌晨”一节。

