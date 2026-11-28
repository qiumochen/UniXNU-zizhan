# 2026-11-25 夜间无人值守收口：外屏口袋防误触与四胶囊快捷栏、CoreAudio 全双工录音采集环（`sens-hal-v4`）、电池 SOH 读出与 M3 独立双启动预检

本轮在用户离场期间执行 10 小时无人值守工程与实机验证（全程强制静音 `zizhan_pulse_dry=1`、`audio_output=0`；零引导分区刷写；按用户“麦克风如果内核实机通了允许增量更新 HAL 插件”的条件授权，在 `mic-kern-4` 纯内核实机闭环通过后对 `/data` 执行单次增量试验更新 `sens-hal-v4` 并完成完整 SHA256 读回校验，`sens-hal-v3` 完整保留可随时回退，`main` 保持 `dac504b` 不变）。

## 1. M2-POCKET + M2-BAR + M2-DIM：SLPI 距离传感器防误触、外屏底部四胶囊快捷栏与环境光联动调暗（`c6462da`）

- **改动**：
  - `ZizhanSlpiSensors.h` 与 `ZizhanSlpi.cpp`：接入第 7 路 SLPI 传感器 `proximity`（`sns_proximity.proto` `msg_id=769` `prox_event_type` 与连续回退 `msg_id=768`），合盖时保持距离传感器在线（`prox_on=1`），内屏熄屏且非合盖时仍完全停流，并向 `/dev/zizhan-sensors` 投递包类型 `22`（`ZIZHAN_SENSOR_PROXIMITY`）。
  - `ZizhanOuterTouchPlan.h` 与 `ZizhanOuterProbe.h`：
    1. **合盖口袋防误触（`zizhan_outer_pocket=1` 默认开启）**：合盖时钟态下若距离传感器判近（`near=1`），自动拦截外屏双击唤醒并递增 `pocket_blocks`，避免放进口袋或包内误亮外屏桌面；拿出口袋（`near=0`）后双击 `41 ms` 即刻上屏。
    2. **外屏实时桌面底部四胶囊快捷栏**：将底部黑边单胶囊升级为 `VOL−`（`x=60..252`）、`HOME`（`x=276..504`，独立手指槽 `finger=9` 注入底边上滑手势，零干扰主触控）、`CLOCK`（`x=528..756`，一键回切低功耗外屏时钟）与 `VOL+`（`x=780..996`，复用 `0044` 单格 `3.00 dB` 步进），位于主桌面 `y≤1835` 下方 `y=1880..1980` 无冲突。
    3. **外屏 SWAR 夜间自动压暗（`zizhan_outer_dim=1` 默认开启）**：读取 SLPI 环境光将外屏时钟与 2:1 桌面像素按 `scale_q8`（`<2 lux → 112/256`、`2..15 lux → 160/256`、`15..60 lux → 208/256`、`≥60 lux → 256/256`）无分支缩放，暗光下不刺眼。
- **实机闭环（`outer-pocket-bar-1`，`play178` `6d9d5f78ddd0`）**：
  - 7 路 SLPI 传感器同跑（`prox=2(near=0)`，`lux_milli=1000..2000`），`zizhan_outer_touch_synth=3` 依次验证：口袋遮挡 `near=1` 双击拦截（`blocks=1 active_view=1`）→ 解除遮挡 `near=0` 双击唤醒 2:1 桌面（`first_frame wake_to_blit_ms=41 scale_q8=144`）→ 依次点按 `VOL− / HOME / VOL+`（`bar_actions=3 vol_clicks=2 home_swipes=1`，`distinct_misrs=28`）→ 点按 `CLOCK` 回切低功耗时钟并展开清黑（`restored=1`），`0` panic。

## 2. M1-MIC：内核 128 KiB 采集环、`PRM 42` TX 硬件周期流与 `sens-hal-v4` CoreAudio 全双工闭环（`a2a1b07`）

- **根因定位与修复**：
  - 实机对照 `CODEC_DMA_SOURCE`（`LPAIF_RXTX` `CODEC_TX3`，`0xb0000033`）与 `ZizhanCapSg`（`0x0700100f` Tertiary TDM TX，共享 `0x204` `12.288 MHz` 位时钟）：前者在缺少 WCD9385 SoundWire（`SWRM`）与 `TX_MACRO` 驱动时抽取器无采样选通脉冲（`reads=2, read_dones=0`，保留为 `zizhan_audio_cap_src=2` 供后续 WCD9385 联调）；后者存在两处精确协议偏差：
    1. `ShengAudioReach.c` `zizhan_cap_config` 发送声道映射 `0x0800108a = {2, 0x30, 0x31}`，与下游 `PCM_CNV` 期望的 `{1, 2}`（`FL/FR`）不匹配导致样点被丢空（`data_bytes=0`），且 `RD_SHMEM` `0x08001007` 应按 ACDB 设为 `{0, 0}`（满周期 `3,840 B` / `10.0 ms`）。
    2. Qualcomm SPF `AR_OP_READ_DONE`（56 字节 `data_cmd_rsp_rd_sh_mem_ep_data_buffer_done_v2_t`）中 `payload + 20` 为 `timestamp_lsw`（非缓冲区偏移，PCM 数据始终从映射槽偏移 `0` 开始）。
  - 修正后，`ZizhanAudioService`（`zizhan_audio_capture=1`，`ZizhanAudioMemoryCapture = 2` 暴露 128 KiB 共享采集环）与 `ZizhanAudioPrm` 在 `PRM 42` 守护期内以 2 槽乒乓 `RD_SHMEM` 每 `10.0 ms` 稳定收回 `3,840 B` `96 kHz` 双声道周期并 2:1 抽取写入 `48 kHz` 单声道采集环。
- **纯内核实机门禁（`mic-kern-4`，`play182` `46b0fdcf1b7b`，`sens-hal-v3` 零 `/data` 写入）**：
  - `ZIZHAN_AUDIO_CAP_SELFTEST: PASS periods=169 copied_frames=81120 cap_written=81120 requests=170 overflows=0 underruns=0 waited_ms=10`。
  - `ZIZHAN_AUDIO_CAPTURE: first_done payload=56 status=0 data_bytes=3840 ts_us=47554668 addr_ok=1`；两轮会话共采集 `287,040` 帧（`598` 次 `READ_DONE`），`ZIZHAN_AUDIO_PRM: PASS`，`audio_output=0`，`0` panic。
- **授权增量更新 `sens-hal-v4` 与 CoreAudio 全双工实机验证（`mic-hal-1`，`play182` + `sens-hal-v4`）**：
  - 通过 `os-trial-edit.py` 由 `sens-hal-v3` 派生并增量写入 `5 / 102` 个变更块（OS SHA256 `43ab28fbe310…`、UI SHA256 `c798a30c1abf…`、helper `14653f8d151c…`，整窗读回 `PASS`，`sens-hal-v3` 原样保留）。
  - `audiomxd` 加载 `ZizhanAudio.driver` 连接双工服务（`present, capture=1`）；`ui_compat18.c` 依次运行 `AudioQueueNewOutput`（`440 Hz` / `660 Hz`，各 `callbacks=14`）与 **`AudioQueueNewInput`（`48 kHz` 单声道 `Float32`：`Pong audio18: capture session=1 new=0 start=0 callbacks=22 frames=104419 max_abs=0.00000 stop=0`）**，全轮 4 次会话共完成 `814,560` 帧播放与 `887,520` 帧采集（`1,849` 次 `READ_DONE`），`overflows=0`、`underruns=0`、`ZIZHAN_AUDIO_PRM: PASS`、`audio_output=0`、`0` panic。
  - **物理边界说明**：CoreAudio 应用层（语音备忘录等 `AudioQueue` / `AVAudioEngine`）↔ `ZizhanAudio.driver` ↔ 内核共享采集环 ↔ ADSP `RD_SHMEM` 硬件 48 kHz 节拍现已全链路贯通；真实环境声波拾音仍需后续补齐 WCD9385 SoundWire（`SWRM`）与 `TX_MACRO` 模拟麦克风编解码器驱动（当前采集环收到的是未挂接模拟前端的数字静音流）。

## 3. POWER-HEALTH：非伪造电池健康度 / SOH 读出（`6887139`）

- **改动**：`ShengBattmgr.{h,c}` 与 `PongBattery.cpp` 新增标准 `BATTERY_GET` 属性 `1`（`BmHealth`）与 `5`（`BmSoh`）读取，向 `AppleSmartBattery` 发布真实 `AppleRawBatteryVoltage`、`BootVoltage`、`PermanentFailureStatus` 与扩展 `BatteryData`（含固件真实上报的 `StateOfHealth`），严格遵守不伪造 `Battery Service State`、`ageAwareMitigationState` 或 `AppleAuthCPRelay` 规则；同时为 `zizhan_batt_board` 双电芯 `BOARD_GET` 增加 `500..4000 mAh` 合理范围门控（SM8475 上 `0x8a/0x8b/0xa6/0xa7` 返回非 mAh 寄存器值时自动回退 `BmCapacity`，避免污染百分比），并加入 `sheng-battmgr.py` ASan/UBSan 单测。
- **实机闭环（`mic-kern-4`）**：`ZIZHAN_BATT_DATA_KV: DesignCapacity=4500 NominalChargeCapacity=3379 CycleCount=916 StateOfCharge=100 Voltage=4445 Temperature=3440 InstantAmperage=482 StateOfHealth=80`，`Health=1`（Good）、`SOH=80%`、`timeouts=0`、`errors=0`。

## 4. M3-PREP：独立双启动离线预检工具与 `m3-standalone-1125` 刷新（`6887139`）

- **改动与验证**：新增纯主机离线校验器 `tools/zizhan/mac/check-m3-standalone.py` 与单元测试 `tools/zizhan/tests/m3-standalone-preflight.py`（纳入 `check-daily-use-host.py`），逐项核验 `192 MiB` 分区几何、Android boot v4 头、BootShim 符号表、`PONGFAT1` CRC32、3 MiB UEFI FD、内嵌原厂 Android `ARMd` Image、`AVBf` 尾标及原厂 `boot_b.img.gz`（`754e6bd04ab9…`）SHA256。
- 基于最新干净内核 `build-play182`（`46b0fdcf1b7b…`）生成无自动复位的独立包 `out/m3-ios-1125` 并构建不复位直跳版（`direct=1` `AndroidDirect`、`layout=pongfat-first`、`default_entry=android`）双启动镜像 `work/m3-standalone-1125/pong-xnu-aprime.img`（SHA256 `30427cc3cd72…`，`144.53 MiB`），离线预检 `M3_STANDALONE_PREFLIGHT: PASS`，并输出带 `ZIZHAN_USER_PRESENT=yes` 硬门禁的 `run-m3-ram-test.sh` 与 `rollback-boot-b.sh`（全程零分区写入、无人值守未启动）。

## 5. 全子系统静默浸泡 `soak-1125-night-a` 与收尾状态

- **30 分钟全子系统静默浸泡 `soak-1125-night-a`（`play182` `46b0fdcf1b7b` + `sens-hal-v4`，镜像 `59eb57c86a82`，`1,800 s`）**：
  - 同时运行日常温控/电源策略、`PRM 42` 4.34 s 快启双功放静音守护 + TX 采集环 + CoreAudio 输入/输出测试、3 轮合盖口袋遮挡拦截 → 双击唤醒外屏桌面 → 四胶囊快捷栏（`VOL−/HOME/VOL+/CLOCK`）→ 展开循环（`zizhan_outer_fold=1 zizhan_outer_touch_synth=3`）、7 路 SLPI 传感器与自动亮度、`zizhan_batt_health_diag=1`、`zizhan_standby_bw=3` 及每 15 s 应用点开/上滑主屏循环：
  - **7 路传感器与待机联动**：累计 `78,941` 次加速度、`78,940` 次陀螺仪、`203` 次环境光、`7,593` 次磁力计、`7,588` 次气压计（`99,671 Pa`）、转轴角度 `177.0°`、距离传感器 **`prox=4(near=0)`**，跨 3 轮折叠状态切换共完成 **`6/6` 次自动停流与恢复**（`quiesced=0(6/6)`）。
  - **网络、显存与温控**：Wi-Fi 连续 ping **`1,480 / 1,495 s`**（`>2 s` 断档 **0 次**，中位 RTT `106.3 ms`）；768 MiB GPU BO 池峰值 `712 MiB`（`fell back +0, scattered +0`）；CPU 最高 `79.7 °C`、GPU 最高 `61.5 °C`（限频窗口 `0/52`），电量 `100% -> 100%`。
  - **零异常**：`0` panic、`0` `SIGABRT`、`0` `SIGSEGV`、`0` `PastBuffer`、`0` `NONE FREE`，到期平稳回 Android（`WAIT_ANDROID: PASS`，`35.2 °C`）。
- **次轮传输前 `NO_FASTBOOT` 停机说明**：`soak-1125-night-b`（`fold=2` 对照包 `84e3fc607ede`）在 Android 下完成 `app-preflight.py` 整机校验 `PASS`（`35.2 °C`、`100%` 电量、`sens-hal-v4` 活体哈希一致），随后 `adb reboot bootloader` 切换时遇到已知偶发 `NO_FASTBOOT`（后部 USB-A 端口 `0x0100 power` 仅供电未枚举，未发送任何镜像）。按无人值守安全规范立即停止后续设备轮次，保持 USB 供电维持 `100%` 低温停放；在场脚本 `run-video-1.sh`（`c2c7b31a0633`）与 `run-fold-desk-1.sh`（`a897cfd8374c`）均已重打包就绪。
