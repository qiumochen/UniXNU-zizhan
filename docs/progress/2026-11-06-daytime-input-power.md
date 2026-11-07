# USB、音量键与电池状态：白天开发及晚间验收

用户选择推荐组合 USB-01、IO-01 音量键、POWER-01 电池原生状态；人工操作统一留到 2026-11-06 晚间。当前任务状态只维护在 [STATUS.md](STATUS.md)。

## 开发顺序与范围

1. USB：检查 Android→fastboot 的稳定枚举、目标身份和恢复证据。每次最多一次重启请求与一次已记录端口恢复；失败停止，不重复相同失败轮次。保留每个阶段的有界命令结果，镜像传输必须在稳定枚举及本机 product 确认之后。
2. 音量键：核对本机 live DT 的 PMIC/PON/GPIO 映射和已有内核→backboardd→SpringBoard 链。补足事件源退出时的释放与可回放验证，准备手动短按、长按、连按验收。
3. 电池：保持原生服务计算健康状态，采集 AppleSmartBattery、IOPS 和 ageAwareMitigationState 的真实输入，定位管理状态缺失；需要实际 ADSP=2。不存在的值记缺失，不替换为健康/正常。

初始源码为 `6d074ef09e5777cecfa23612e626ce2c3878153e` 加既有未提交工作。已有夜间工作完整保留；没有新提交、推送或 CI 结果。初查 Android ADB 和 boot_completed=1，无设备 cycle/installer/waiter。这次准备与主机回归不代表候选已安装或启动。

## 晚间验收设计

| 项目 | 控制与候选变量 | 人工操作 | 接受条件与停止条件 |
| --- | --- | --- | --- |
| USB-01 | 保持后部 USB-A、OS370 和原正常镜像；仅改变主机切换流程 | 必要时观察手机界面、恢复物理连接 | 单次请求有稳定枚举、正确 product、真实传输和返回记录；失败先收证据并停止，不把未启动算内核故障 |
| IO-01 | 独立 UI 候选；其他组件及参数保持控制身份 | 加/减各短按 3 次，长按后松开，交替连按，再在锁屏重复 | 实际 PMIC 事件、转发、原生动作及可见系统音量一致；松开后不继续变化，无幽灵按键；未出声不影响按键事件单独判定 |
| POWER-01 | 先使用相同 UI、只读诊断与实际 ADSP=2；不改健康状态返回值 | 打开电池及健康页面，拔插充电器并观察 | 真实电源读数、IOPS 字典、notify 状态与界面对应；状态缺失或继续转圈仍为未解决，不能凭诊断包构建通过升级资格 |

### 13:20 USB 单轮实验设计

- 任务：USB-01；运行名 `g390-usb-baseline`，既有正常镜像 `out/g376-final-ready/pong-xnu-initial.img`。镜像及内核 SHA256 运行前写入仓库外 supervisor 记录。
- 假设：受控记录端口、USB bootloader 身份稳定观察和 product 检查能降低 Android→fastboot 传输前的误判；镜像传输前稳定等待失败时，最多允许一次记录端口恢复。
- 控制：手机当前 Android 和完整 OS370；后部 USB-A；正常 GPU/Wi-Fi 内核，`pong_adsp=0`、`pong_touch=0`；不改 OS、UI、helper、内核或 UFS。
- 变量：只替换主机 USB attach 流程。后续 autoreset 取得的 `ramoops-dump.img` 沿同一身份检查路径返回；正常最长轮次 300 秒，等待/收日志总时限 18 分钟。
- 通过：Android→fastboot 稳定且产品为 zizhan；g376 实际完成传输、启动及带新 klog 的 autoreset；ramoops dump 正常取回后 Android `sys.boot_completed=1`。前置 USB 阶段失败记 SKIP/FAIL，未发送镜像不可算内核失败。
- 返回/停止：若失败但 fastboot 明确可见，仅发送一次 `fastboot reboot` 并有界等 Android；无可见 transport 时不再发命令，升级为需人工恢复。无重试 kernel boot，无写分区，无同时其他设备任务。
- 当前无后台队列。旧 07:00 截止已过，本轮由新的有限时监督器独立计时，完成后更新设备结论再决定第二项。

每轮使用临时 `fastboot boot`，保留 autoreset、panic reset 和电源键返回；命令行小于 1024 bytes。准备、安装、启动身份分开登记，OS370 为保留的回退基线。候选及原始日志只存仓库外，今晚先单项、后组合验收。

## 白天阶段结果

- USB 主机回放通过：Android、缺失设备、单个稳定 fastboot、多个 fastboot、错误 product、端口占用和传输前 USB 错误均有独立分支；实际接入探测在当前 Android 状态下返回 `SKIP/NO_FASTBOOT`，未发起重启或写入。
- 音量键只读核对通过：当前 Android `getevent -lp` 可见 `gpio-keys` 的 `KEY_VOLUMEUP`、`pmic_resin` 的 `KEY_VOLUMEDOWN`；已有 XNU 日志报告 PMIC arbiter v7、PON/up/down 均可用。物理短按、长按、连按仍未提前宣称通过。
- 电池只读核对通过：当前 Android 报告 USB 在线、99%、4307 mV、26.7 C；历史 XNU ADSP=2 读数和 78% 健康容量证据保留。新增诊断启动参数下的 IOPS 字典观测，记录 `Battery Service State`、服务 flags、容量、BatteryData 和 Serial 的真实存在性；XNU 电池健康页缺口仍是 `getBatteryHealthServiceState=-1` 与 `ageAwareMitigationState` 未发布，未改成伪造状态。

主机验证：`tools/zizhan/tests/usb-attach.py` 通过，诊断 UI 适配器编译、签名和严格校验通过；带正确固定 `../sheng/drivers/Ath12kWifi` 输入的 Wi-Fi 主机回归通过。前一次未指定输入的默认路径失败属于主机路径配置错误，不计入驱动回归结论。

### g390 USB 基线结果

- 13:23:33 开始，使用 g376 包（image SHA256 `5392c2fb7d9cd496fecc825adddf4cf38e8791bad28013a2baf2c3eaab038457`、kernel SHA256 `595734fc692367b89d4e7f2df4a84a9754973f48a9622ae64919076f750f8dab`），OS370 实际 SHA256 `a0187c09ea653be15201b62894cb2247670967da5243ed6e582930d064dc2b0e`，源 dirty 清单和启动参数保存在仓库外 supervisor JSON。
- USB-A Android→fastboot 稳定身份/product 检查通过，100168 KB 临时镜像传输并启动；SpringBoard 后 autoreset 至 fastboot 观测耗时 229 秒，期间监督器通过既有端口保护做了两次有界 VBUS 恢复。ramoops 收集包传输成功，klog reason=deadline，收回 1,443,081 bytes；cycle exit=0，Android `boot_completed=1`。
- klog 有启动配置、CPU 八核完成、PMIC arbiter v7（PON/up/down usable）及背光/HID 服务初始化证据，无 panic。该轮关闭 `pong_touch`、`pong_adsp`，无电池服务、用户态音量路由、实体按键或图像验证；Wi-Fi 仅扫描里程碑，未联网。
- 判定：USB-01 在本轮临时启动/返回流程 `PASS`，不代表已证明长期重试率；IO-01 仅确认按键硬件识别，POWER-01 未测试。设备已回 Android；无安装/OS 写入。原始记录留在仓库外 `logs/g390.supervisor.json`、`logs/g390.cycle.out`、`logs/g390-usb-baseline.txt`、`logs/g390-usb-baseline.klog.txt`、`logs/usb-attach-bgmudr2w.json`。

### g391 输入与电池诊断结果

- g391 使用临时包 image SHA256 `9daa19c5be1bef741e94c5d3bb54bfbf5fcecbf50d970065e11bb155628d91a2`、kernel SHA256 `0dd172facedaddb913ab0874053059f882f85f02f53cbe10654afb11cb2a436f`；与 g376 的二进制差异仅为 Mach-O UUID/PrelinkKCID 构建标识，功能配置和 OS370 未变。
- `pong_adsp=2` 阶段 1–13 全部出现，AppleSmartBattery 发布；初始读数 100%、4323mV、-6mA、28.6°C、满充 3146mAh、设计 4500mAh、908 cycles。日志中 USB 状态经历真实 `0x1f→0x11→0x1b→0x1f` 变化，说明服务读到了电源状态变化；这不是本轮人工拔插验收。
- `Pong keys HID: physical side/volume consumer opened` 和 `Pong volume18: physical volume keys routed ... SBVolumeControl` 均出现；这只证明路由 ready，没有实体按键事件，短按/长按/连按/锁屏仍待人工。
- Settings 自动打开电池健康页并选中健康行；诊断为 `getManagementState=0`、`getBatteryHealthServiceState=-1`、`deviceSupportsCPMS=0`、`maxCapacityPercent=70`，健康页 spinner 持续存在。ADSP、读数和路由启动通过，POWER-01 健康状态仍未解决，未伪造 service state 或 ageAware notify。
- 13:48:20 cycle exit=0、Android `boot_completed=1`，无 panic，OS/UFS 未写入。原始记录留在仓库外 `logs/g391.supervisor.json`、`logs/g391.cycle.out`、`logs/g391-input-power.txt`、`logs/g391-input-power.klog.txt`、`logs/usb-attach-gsir4ukn.json`。

### 13:30 g391 诊断轮设计

- 任务：保持 OS370 和后部 USB-A；源/内核功能身份沿用 g376 构建，仅重新打包 `pong_touch=1 pong_adsp=2 pong_url=15 pong_pane=H`。
- 观测：按键驱动和 `SBVolumeControl` 用户态路由是否启动；ADSP 全阶段和 `AppleSmartBattery` 实时读数；Settings 电池健康原生状态及页面是否继续转圈。不会合成或代替物理按键。
- 安全边界：不写 OS/UFS；重验实际 OS、extent、备份、温度和 USB-A；临时 `fastboot boot`，`pong_autoreset=300`、`pong_autoreset_sb=180`；18 分钟 supervisor。异常停止并保留日志，不换镜像重试。
- 通过边界：运行至 SpringBoard 并收回 Settings/ADSP/电池证据、新 klog 完整、无 panic、Android 完整返回。Battery Service State 或 ageAware 仍缺失/页面继续转圈只说明缺口复核，不是 POWER-01 修复；无实体事件的轮次不算 IO-01 验收。
