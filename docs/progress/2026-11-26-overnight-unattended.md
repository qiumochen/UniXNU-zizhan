# 2026-11-26 凌晨无人值守收口：WCD9385 物理主麦克风真声拾音、QCA6490 3 Mbps 蓝牙串口、XNU 0045 动画快路径与全子系统浸泡

本轮在用户离场期间按 5 阶段计划推进硬件突破与性能收口（全程强制静音 `zizhan_pulse_dry=1`、`audio_output=0`；零分区刷写；已安装的 `sens-hal-v4` 无需任何 `/data` 改动即直接跑通物理麦克风真声输入；`main` 保持 `dac504b` 不变）。

## 1. INFRA-USB：USB 枚举失联双脉冲有界自愈（`055a790`）

- **改动**：增强 `tools/zizhan/mac/usb-attach.py` 与 `tools/zizhan/tests/usb-attach.py`：当 `adb reboot bootloader` 后首轮等待遇到偶发 `NO_FASTBOOT`（端口仅 `0x0100 power` 未完成 USB 枚举）时，自动执行最多 2 次有界 `usbkick.sh kick` 端口重枚举并复核序列号归属，消除人工拔插依赖。

## 2. M1-MIC-HW：WCD9385 SoundWire DP1 + `VA/TX_MACRO` 物理主麦克风真声拾音（`5747d3e`）

- **硬件拓扑逆向与根因定位**：
  - 反汇编原厂 `swr_ctrl_dlkm.ko`、`wcd938x_dlkm.ko`、`lpass_cdc_va_macro_dlkm.ko`、`lpass_cdc_tx_macro_dlkm.ko` 并核对实机设备树 `qcom,tx_swr_ch_map` 首项 `<0x00 0x12 0x01 0x00 0x22>`：
    - Mix Fold 2 底部主模拟麦克风 `AMIC1` 经 `WCD9385` `MICBIAS1`（`2.75 V`，`0x3022=0x63`）与 `ADC1`（`0x300e=0x2c`，`+12 dB`；`0x3405=0x11` 映射 `SWRM_TX2_CH1`）送入 `wcd938x-tx-slave`（`dev=1`，`0x0d:0x01170223`）**Slave DP1（`slv_port_id=0` → 基址 `0x0100`：`0x0120/0x0122/0x0128/0x0105`）**，而非 DP2（`0x0200`）；同时需向广播地址 `dev=0x0f` 写入 `SDW_SCP_BUSCLOCK_SCALE`（`0x00e0/0x00f0=0x01`）并置位 `va_swr_master`（`0x33b0000`，SWRM v1.7）`SWRM_COMP_CFG`（`0x0010=0x041e`）及 **Master Port 2**（`0x1224/0x1264=0x01000001`）。
    - 数字侧经 `VA_MACRO`（`0x33f0000`，`+0xc4=0x0f`、`+0xc8=0x50`、`+0xdc=0x06`）与 `TX_MACRO`（`0x3220000`，`+0x88=0x60`，`DEC0` `+0x100=0x05` `SWR_MIC4`、`+0x400=0x24`、`+0x418=0x01`）接入 `CODEC_TX3`（`0xb0000033`，单声道 `channel_mask=0x01`）→ ADSP `CODEC_DMA_SOURCE` → `RD_SHMEM`（`480` 帧 / `960 B` 每 `10 ms`），并将 `ZizhanAudioPrm.cpp` 默认 `cap_src` 切换为 `2`（保留 `zizhan_audio_cap_src=1` 回退）。
- **实机端到端验证（`mic-hw-4` `play186` `6c57499a69e9` 与 `bt-3m-2` `play188` `8fc313647abd`）**：
  - `ZIZHAN_MIC_CODEC: PASS stage=10 swrm_ver=1070000 clk=100001 slaves=4 dev=1 id=d:1170223 devid=223 efuse=0 bank=1 dec=0 mask=1 gain=12 timeouts=0 audio_output=0`。
  - 4/4 轮会话全部 `PASS`：累计 `1,818` 次 `READ_DONE`（`871,104` 帧 `48 kHz`），其中 **`856,139` 帧（`98.3%`）为真实非零物理 MEMS 底噪/环境声样点**（`min=-519`、`max=425`、`max_abs=519`、**`mean_dc=0`** 零直流偏置且无削顶，`overflows=0`、`underruns=0`）；用户态 CoreAudio `AudioQueueNewInput`（`sens-hal-v4`，零 `/data` 写入）直接录得真实非零波形 **`Pong audio18: capture session=1 new=0 start=0 callbacks=19 frames=89741 max_abs=0.00546 stop=0`**，停止后反向断电释放 `bobc1`（`STOP teardown_ok=1 bob_released=1`）。新增 `zizhan-mic-codec.py` ASan/UBSan 寄存器规划单测。

## 3. BT-02：QCA6490 SE20 蓝牙串口升级至 `3 Mbps`（`456a31e`）

- **改动与根因修复**：新增纯 C 协议与波特率规划模块 `drivers/ArmDTPlatform/ZizhanBtProtocol.h` 及 `tools/zizhan/tests/bt-hci-shim.py` 单测，更新 `ZizhanBtProbe.h`（默认 `zizhan_bt_baud=3000000`，`=115200` 可回退）：
  - 非 DFS 模式下将 `GPLL0_OUT_EVEN`（`300 MHz`）按 `M=4, N=25` 双沿分频为精确 `48.0 MHz`（`sampling=16, clk_div=1` → `3,000,000 bps`），并在 NVM Tag 17 写入 `0x0e`（`QCA_BAUDRATE_3000000`）。
  - 严格对齐 Linux `hci_qca.c` + `qcom_geni_serial.c` 切换时序：置位 `SE_UART_MANUAL_RFR` `BIT(31)`（`0x80000002` `UART_MANUAL_RFR_EN | UART_RFR_NOT_READY`）锁住从机发送 → 在 `115200` 下发完 `EDL_CHANGE_BAUDRATE`（`01 48 fc 01 0e`）并排空移位器 → 通过 `stop_rx_fifo()` 等待 `S_CMD_CANCEL_EN`（`BIT(4)`）确保 `S_GENI_CMD_ACTIVE=0` 后切换 `48 MHz` RCG 与重开 `UART_START_READ` → 释放硬件自动流控。
- **实机验证（`bt-3m-2`，`play188` `8fc313647abd`）**：
  - `baud switch want=3000000 active=3000000 rate=48000000 div=1 rcg_ok=1 m_clk=11 s_status=41000`。
  - `125,468 B`（517 段）`rampatch` 固件下载耗时从 `115.2 kbps` 的 `~11.2 s` 降至 **`552 ms`（提速 `20.2×`）**，27 段 NVM 与 `post-patch-reset`、`bd_addr` 全部一次通过；`bluetoothd` 全程 **`0` 次 `SIGABRT`**（`bridge rx=12401 tx=5337 hci forwarded=111 shimmed=172 ibs wakes=24 fail=0`），同芯片 Wi-Fi 连续 ping `85/84 s` 零断档。

## 4. UX-OPEN：XNU 补丁 `0045` 调度卫生静态键与 UPL `btref_get` 快路径（`dbb7465`）

- **热点定位与改动**：基于 `ux-k1` / `prof-bb-2` IPI 采样剖析（`DEVELOPMENT=1` 下 `_ml_set_interrupts_enabled_with_debug` 与 `__collect_preemption_disable_measurement` 占 `SpringBoard` 内核态样本逾 `50%`，且 `upl_debug_enabled=1` 使每次 `IOSurface` 映射都执行 `btref_get` 全栈回溯与 `object->uplq` 锁链），新增 `patches/xnu-ipados18/0045-zizhan-ux-hygiene-upl-fastpath.patch`：默认通过 `STATIC_IF_INIT` 将 `sched_debug_pmc` / `preemption_disable` / `interrupt_disable` 三把静态键打成硬件 `NOP` 并默认设 `upl_debug_enabled=0`（传 `zizhan_sched_hygiene=1`、`zizhan_upl_debug=1` 可完整恢复调试追踪）。
- **同内核背靠背受控 A/B（`build-play189` `62bca864dd35`，`ux-ab-base` vs `ux-ab-opt`）**：
  - `__collect_preemption_disable_measurement` / `__prepare_preemption_disable_measurement` / `_btref_get` 采样降至 **`0`**，`_ml_set_interrupts_enabled_with_debug` 下降 **`32.6%`**，15 次点开应用窗口中 `SpringBoard` 主线程可运行排队等待 `q` 由 **`153 ms` 降至 `103 ms`（`-32.7%`）**。
  - `zoom-cadence.py`：`open_zoom` 帧率 `57.3 → 58.5 fps`（最大帧间隔 `58.2 → 50.0 ms`，`>33 ms` `6% → 5%`）；**上滑退回主屏 `exit_zoom` 帧率 `56.4 → 58.3 fps`，`p90` 由 `33.2 ms` 降至 `25.0 ms`（立减一整帧，`>25 ms` 由 `15%` 降至 `9%`，`>33 ms` 由 `12%` 降至 `7%`）**，`0` panic、`0` `SIGABRT`、`0` `NONE FREE`。

## 5. 在场脚本与 `m3-standalone-1126` 重打包及 30 分钟全子系统静默浸泡

- **在场包与 M3 刷新（基于最终内核 `build-play189` `62bca864dd35`）**：已离线重打包 `out/video-1`（`116d92b3bbd8…`）、`out/fold-desk-1`（`a62bbbf8ab51…`）与 `work/m3-standalone-1126/pong-xnu-aprime.img`（`3448355df5f8…`，`direct=1`，`M3_STANDALONE_PREFLIGHT: PASS` 且 `backup_verified: true`），全程零分区写入。
- **30 分钟静默浸泡 `soak-1126-morn`（`play189` `62bca864dd35` + `sens-hal-v4`，`1,800 s`）**：全开 `3 Mbps` 蓝牙（累计 `rx=167,586 B`、`tx=32,658 B`、`217` 次 IBS 唤醒 `fail=0`）、WCD9385 物理主麦 + 双功放静音守护、3 轮合盖防误触/双击桌面/四胶囊循环、7 路 SLPI 传感器（`78,930` 次加速度/陀螺、`7,593` 次磁力、`7,586` 次气压 `99,722 Pa`、转轴 `177.0°`、距离 `prox=4`、`6/6` 次待机停流恢复）、Wi-Fi 连续 ping **`1,506 / 1,516 s`（`>2 s` 断档 `0` 次）**、`25,728` 零拷贝帧、CPU 峰值仅 **`73.0 °C`**（较 `0045` 前 `79.7 °C` 下降 `6.7 °C`，限频窗口 `0/26`），`0` panic、`0` `SIGABRT`、`0` `PastBuffer`、`0` `NONE FREE`，到期平稳回 Android（`WAIT_ANDROID: PASS`，电量 `100%`）。
