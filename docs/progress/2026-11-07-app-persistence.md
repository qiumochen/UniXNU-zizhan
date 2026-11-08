# 系统应用现状与持久化方案初步结果（2026-11-07）

本批对应 APP-01 与 PERSIST-01。设备仍使用 OS370，采用 g376 已验证 kernel/ADT/loader 三件套的私有临时启动包；只执行 `fastboot boot`，没有安装候选、写入启动分区或修改 root/Data 镜像。

## APP-01

### 输入与设备结果

- 设备启动前检查通过：当前 Android、`boot_completed=1`、温度 25.7°C、live OS manifest 与 OS370 hash 一致、root/klog 连续 extent 和完整备份清单通过、后部 USB-A 可控。
- 私有候选只改变启动参数，kernel SHA256 为 `595734fc692367b89d4e7f2df4a84a9754973f48a9622ae64919076f750f8dab`，ADT SHA256 为 `e8e87c2fd99f787bdb2b6bfdbd2a32c0f3915462fee26fe4e66a81de24a687f7`，DarwinBoot24 SHA256 为 `dc607a1df3e6fc2fa98abdb0f5e2d8d471133d339b62d4b1172103811d491dbb`，镜像 SHA256 为 `ba84f019a1198c5658ab56de330036c1a94b78fe6a11d1be70298c6c081d0447`。命令行 1008 字节。
- `app01-weather-v4` 完成临时启动、自动回 fastboot、ramoops/klog 取回和 Android `boot_completed=1` 返回；监督器 PASS 只代表设备往返，不代表应用功能通过。
- 日志中没有有效 `PONG SHOT`，截图完整性结果为 SKIP。出现过一次 `PONG DBUF: canvas slot NONE FREE` 与 `MiniIOSurface: no memory`，因此本轮内容画面不能作为通过证据。

### 应用现状表

| 应用 | 启动 / 前台 | 内容显示 | 核心操作 | 联网 / 输入 | 当前结论 |
| --- | --- | --- | --- | --- | --- |
| Safari | 未执行 | 未执行 | 未执行 | 未执行；没有用网络探针替代浏览器验收 | SKIP |
| 设置 | 历史轮次可自动打开 Settings；本轮未复核 | 本轮未复核 | 电池健康页历史上仍 spinner；本轮未复核 | 本轮未执行 | SKIP |
| 时钟 | 未执行 | 未执行 | 未执行 | 未执行 | SKIP |
| 天气 | `WiFiUsageApplicationSession` 多次报告 `com.apple.weather` resumed，前台会话路径可达 | 无有效截图；另有画布资源不足记录 | 未执行天气页面操作 | 未建立天气数据联网证据 | 启动/前台会话 PASS；整体部分可用 |
| 照片 | 历史 g264 启动并保持运行；本轮未复核 | 历史 g264 有 6 张完整截图；本轮未复核 | 首次提示、相册、搜索和编辑仍未完成 | SharedEvent 监听仍未实现 | 历史启动/显示证据；本轮 SKIP |

本轮产生的原始日志、监督状态、preflight、候选身份和截图解析结果均保留在本机 `logs/` 与 `out/`，没有复制到公开仓库。当前不能据此决定后续应用适配顺序；最小下一项是使用能保留有效截图的独立应用轮次，先完成 Safari 的真实网页加载、地址输入、滚动和跳转，再补 Settings/Photos 的可见内容与核心操作。

## PERSIST-01

### 已核对的启动与存储事实

- 当前 Android 为 13 / `V14.0.15.0.TLRCNXM`，活动槽为 `_b`。设备存在 A/B 的 `abl`、`boot`、`dtbo`、`vendor_boot` 等 Android 启动输入；没有 `init_boot` 分区。
- iPadOS 当前只通过后部 USB-A 的临时 `fastboot boot` 进入。DarwinBoot24、XNU、ADT 和 root 匹配信息都随临时 Android boot image 传入，当前没有独立的 iPadOS 启动分区入口。
- Android `/data/hackios/root.img` 是 20,822,622,208 字节的固定文件，位于 Android userdata 上；当前 pinned root 的实际连续 extent、6 MiB klog 文件、manifest 和相互不重叠关系已核对。Android `/data` 为 f2fs，`/metadata` 为独立 ext4。
- iPadOS root 文件内部的 OS、SystemOS、Data 窗口仍由 `ufs-layout.json` 固定：OS 6,845,104,128 字节、SystemOS 5,368,709,120 字节、Data 8,589,934,592 字节。当前 layout 记录 `partition_writes=false`，安装流程通过 recovery 写 userdata 窗口，不移动 pinned root。
- 已验证的是安装后的 OS370 内容、live manifest 和 host 侧 Data roundtrip；尚未验证 iPadOS 自身正常重启、异常退出后 Data 写回，亦未验证脱离电脑冷启动。

### 方案比较

| 方案 | 结果 | 前置与风险 | 回退 |
| --- | --- | --- | --- |
| 保持临时 `fastboot boot` | 当前可用，已验证 | 依赖电脑和 USB；每次由 Android 回到临时 boot；不能算独立开机 | 保持 OS370、root 文件和 Android 启动链不变 |
| 在 Android `boot_a/b` 或 `vendor_boot_a/b` 接入 UEFI/DarwinBoot 入口 | 可行性待设计，当前禁止实施 | 需要 boot image 格式、A/B 选择、Android 共存、异常回退和完整备份；写错可能失去 Android/fastboot 返回 | 必须有全分区备份、双槽恢复和明确的 fastboot/电源键返回；先做设计审查 |
| 使用 `misc`/槽位元数据选择 iPadOS | 仅为候选，不足以独立启动 | 需要确认 bootloader 是否读取自定义选择字段；误写可能改变 Android 当前槽位 | 只读确认协议后再设计；当前不写 misc |
| 把 root 镜像移动到独立分区或改变 userdata 几何 | 不推荐 | 破坏当前 pinned root、extent、klog 和 Data 窗口假设，风险最大 | 需要全量备份、几何验证和独立恢复介质；当前不考虑 |

当前推荐路径是先保留临时启动作为回退，再单独设计 Android A/B boot image 中的可逆 DarwinBoot 入口。该设计完成前，不改变“不刷写启动分区”的约束。

### 分阶段验证

1. **只读设计阶段（本批完成）**：固定分区、槽位、root/Data/klog layout、启动输入和恢复入口；列出 boot image 格式、A/B 选择和 Android 共存假设。
2. **主机候选阶段**：生成独立的 boot image 设计和 manifest，做格式、签名区域、A/B 对称性、watchdog、返回路径和恢复命令审计；不写设备。
3. **临时启动数据阶段**：继续使用 `fastboot boot`，在 iPadOS Data 中写入可核对 sentinel，执行正常 autoreset 返回 Android，再次临时启动并读取 sentinel；另做有界异常返回，确认没有把旧 klog 或旧 Data 当新结果。
4. **人工恢复阶段**：在完整备份和独立返回路径已确认后，才评估一次 boot 分区候选；先验证 Android 两槽回退，再验证脱离电脑启动，最后验证 iPadOS Data 正常重启/异常退出完整性。

PERSIST-01 的探索交付在主机调查和方案层面完成，状态可记为 `HOST_VERIFIED`；独立开机、Android 共存和 Data 可靠性仍属于后续设备资格，不能从当前镜像已落盘或临时启动成功推断。
