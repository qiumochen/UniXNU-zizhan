# D 系统音频服务后续迭代计划（DRAFT，待审核）

状态：**DRAFT / 待用户审核 / 未启动**。本文件只列清单与迭代顺序，不改变当前台账；审核通过后再启动 I1，并把本节链接进 STATUS。基线为 dirty main `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加现有工作树；实际安装 `statusbar-work-v1`（OS `5c0912...`、UI `f2d639...`、helper `14653f8d...`）。

## 1. 已确认基线（不是待办）

| 能力 | 证据级别 | 边界 |
| --- | --- | --- |
| 单图 `WR_SHMEM 48k/2ch/24bps → MFC 0x07001015 → SAL → TDM 0x0700100e` 生命周期 | DEVICE_VERIFIED（mode35，限定） | 12 图命令 status0、真实 MAP 句柄、STOP/CLOSE/UNMAP/native-idle/PRM 清理 |
| 4×2880B 零数据与已知 48k 非零数据 | DEVICE_VERIFIED（mode36/37，限定） | 各 4 真实 V2DONE/1920 帧；AMP/BST/TX/数据脚/输出关闭 |
| v2 控制页 + 生产者水位 + Ready 门控 + ring→24-bit + 4 批运行时入口 | DEVICE_VERIFIED（mode38，限定） | 12 ACK、Ready 0→1、欠载门、4 真实 DONE、stop 后 drain、Ready 清除、映射恢复；**内核受限生产者，未接 HAL/服务，未出声** |
| 48k ring 取数 / 24-bit 打包 / 有限批次 / 服务门 | HOST_VERIFIED | ASan/UBSan + 日用 host；不是设备资格 |

## 2. 剩余清单

### A. 音频主线（建议作为唯一主线）

| ID | 目标 | 类型 | 依赖 | 当前状态 |
| --- | --- | --- | --- | --- |
| A1 | 独立 `ZizhanAudio` IOKit 服务/用户客户端，默认关闭，控制页共享，仅显式请求才建图 | 主机+双 kernel | mode38 核心 | TODO |
| A2 | 服务默认关闭与“仅服务、不建图”两轮静音设备资格 | 设备（静默） | A1 | TODO |
| A3 | `ZizhanAudio.driver` HAL bundle 参数化（类名/UID/bundle），默认不安装 | 主机 | A1、现有 plugin | TODO |
| A4 | HAL `DoIOOperation` 真实 ring 生产者：写完再发布 `out_written`，处理回绕，不伪造数据 | 主机+双 kernel | A3 | TODO |
| A5 | HAL bundle 的 OS-only COW 安装候选与回退链 | 主机+安装 | A3、fresh preflight | TODO（需授权） |
| A6 | HAL 安装后的静音设备轮：真实水位→Ready→有限批次→stop/drain→清理 | 设备 | A4、A5 | TODO（需授权） |
| A7 | 仅在 A6 暴露具体停止/超时/欠载问题时做一轮 E | 设备 | A6 | 条件性 |
| A8 | 服务生命周期硬化：clientClose/terminate/重复 start-stop/错误保留 | 主机 | A1、A6 | TODO |
| A9 | 首声现场候选与检查表 | 用户在场 | A6 | 封存，不自动执行 |

### B. 可并行主机/只读工作（不碰手机）

| ID | 目标 | 类型 | 依赖 | 当前状态 |
| --- | --- | --- | --- | --- |
| B1 | 音频服务/HAL 代码审查、简化与 porting/证据对齐 | 主机 | A1、A3 | TODO |
| B2 | 传感器解阻只读复核（25 分钟上限） | 只读 | 仅当有新输入/新假设 | BLOCKED |
| B3 | UI 剩余项：电池健康服务（thermalmonitord/powerd）与全分辨率/细字验收设计 | 主机设计 | 现有 UI 基线 | TODO |
| B4 | 文档、porting、CI 检查；为最终发布准备干净 diff | 主机 | 各批完成 | 持续 |
| B5 | 每日 host / display / wifi 回归（有变化时） | 主机 | 源码变化 | 按需 |

### C. 需要用户在场或单独授权

| ID | 事项 | 原因 |
| --- | --- | --- |
| C1 | 首声/实际扬声器输出、AMP/BST/TX、GPIO124 数据脚 | 需听测与现场安全判断 |
| C2 | 外屏、外触控、自动切屏/Hall 路由 | 需物理折叠与观察 |
| C3 | GNSS 现场（权限、室外、真实 fix） | 需用户操作与环境 |
| C4 | 整机 OS/Data/固件安装、分区写 | 超出当前无人值守授权 |
| C5 | 任何 `zizhan_audio_attended=1` / PRM34 / 音量搜索 | 首声边界 |

### D. 明确不在本轮

- 把 Android 播放、图 ACK、测试 DONE 或 Ready 当作 App 播放/首声资格。
- 无同 boot 可信生产者的六项带宽票、SLPI 资源写、PAS12/remote3 初始化。
- 清理受保护工件、移动 pinned root、commit/push。

## 3. 迭代顺序与验收

### I1（主机，约 2–3h）服务骨架 + 默认关闭

- 交付：`ZizhanAudioService.cpp`（类 `ZizhanAudio`、用户客户端、独立 `zizhan_audio_memory/request`），构建脚本与 `PongPower.cpp` 探针接线；boot arg `zizhan_audio_service=1`，默认不启动；与 `pong_audio`/`zizhan_audio_prm` 冲突时跳过。
- 行为：启动后只暴露 v2 控制页与 ring，**不建图、不发布 Ready**；首次 Start 请求后才进入已验证图序列。
- 验收：主机编译/负控制、精简+完整 kernel、默认关闭静态检查；无设备轮。
- 失败处理：服务注册/符号冲突无法收敛时，保留 mode38 探针路径，停止 A1 并转 B1/B3，不用假服务绕过。

### I2（设备，两轮静默，每轮 ≤15m preflight + 30m 监督）默认关闭与仅服务

- I2a：不带 `zizhan_audio_service`，确认无 `ZizhanAudio` 服务、无 Ready、无图命令、无 ADSP 图活动。
- I2b：带 `zizhan_audio_service=1` 但无 HAL/无 Start 请求，确认服务存在、控制页可映射，Ready=0、无图 START、无输出。
- 验收：新鲜 deadline 日志、Android 完成、无 panic；失败即停服务设备路径。
- 预算：每轮只改一个变量；不合并 I2a/I2b。

### I3（主机，约 2–3h）HAL bundle 参数化

- 交付：`ZizhanAudio.driver` bundle（类名 `ZizhanAudio`、独立 UID/bundle ID），复用现有 plugin 逻辑但使用 `ZizhanAudioControl` v2；默认不安装，只有 `enable_audio_hal` 时进入 HAL 目录。
- 验收：插件可编译、默认安装面不变、PongAudio 回退仍在；host 侧水位发布/回绕测试。
- 失败处理：若参数化导致 PongAudio 回退风险，保留副本并单独 bundle，不改现有 Pong 路径。

### I4（主机，约 1–2h）真实 ring 生产者

- 交付：`DoIOOperation` 按 sample time 写 48k/2ch int16 ring；数据复制完成后才发布 `out_written`；回绕、短写、停止时不发布；欠载不在 HAL 侧伪造媒体。
- 验收：host 逻辑测试、插件构建、双 kernel；不发布 Ready。
- 关键负控制：先发布后写、越过 ring、停止后发布、奇数序读、重复/迟到 DONE 均拒绝。

### I5（安装 + 设备，约 2h）HAL bundle 静音轮

- 前置：fresh `app-preflight`（实际安装 `statusbar-work-v1`）、wholeOS/manifest/extent、rollback-resume、备份清单；HAL bundle 只写已验证的 OS 差异块，保留回退。
- 轮次：`zizhan_audio_service=1` + HAL 插件；观察真实 `out_written`、Ready 0→1、有限四批 DONE、stop/drain、clientClose 清理；AMP/BST/TX/数据脚/输出关闭。
- 验收：真实 HAL 水位、4 DONE、清理、Android 完成、无 panic；失败保留负结果并回退到 `statusbar-work-v1`。
- **需用户先授权 A5/A6 的 OS-only COW 安装。**

### I6（收尾/硬化，主机+证据）

- 服务生命周期、错误保留、porting/STATUS/dated 更新；可选 E 轮只在 I5 暴露具体停止/超时/欠载问题时执行。
- 首声候选封存，等待用户在场；首声检查表另列，不在无人值守中执行。

## 4. 设备轮预算与门控

- 一次只有一个手机 owner；每轮 15m fresh preflight、30m 监督上限、15m review；后部 USB-A、临时 `fastboot boot`、完整回退与 Android 完成见证。
- 每轮只改一个变量；I2a/I2b/I5 各算一轮；任一轮失败保留原始 status/reason，不用同输入重跑。
- A 类音频路径若在 I2 或 I5 被服务/IOKit 层拒绝，停止该设备路径，转主机 B1/B3；不用旧分离图或旧 DONE 补位。
- T+18h/20h 的旧硬截止已由用户 open-ended 授权替代；但每次仍以“无活跃进程/lock、设备可回退”为启动前提。

## 5. 需要用户审核的四个点

1. **是否授权 I5 的 HAL bundle OS-only COW 安装**（新内容类，非整机安装；保留 `statusbar-work-v1` 回退）。若不授权，计划停在 I4，设备侧继续只用内核 mode38，不接真实 HAL。
2. **是否保持音频为唯一主线**，把 UI 电池健康/热控/网络长稳放到并行主机或后续窗口；或指定其它优先级。
3. **传感器是否只做“有新输入才复核”**（当前 BLOCKED，不建议为填时间重复采表）。
4. **发布策略**：是否继续不 commit/push，直到你审核里程碑；还是允许在 I1/I3 后做一次本地提交候选（不推送）。

## 6. 停止条件

- 设备未返回、无新鲜 deadline 日志、无 Android 完成：该轮 FAIL，停止后续设备轮，先恢复设备与镜像状态。
- 图命令拒绝、MAP 句柄为零、服务/插件符号冲突、panic/fault/温度异常：停止音频服务路径，保留证据。
- 任何需要听测、外屏、折叠/触控、权限选择或室外的动作：停在默认关闭候选，报告现场边界。
- 不删除受保护工件；不安装整机 OS/Data/固件；不 commit/push（除非用户另行授权）。