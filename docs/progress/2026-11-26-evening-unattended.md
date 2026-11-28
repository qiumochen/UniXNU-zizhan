# 2026-11-26 晚间无人值守收口：Wi-Fi/音频并发修复、展开亮度防闪与外屏上滑解锁、麦/扬增益重调、双开关精简电池页与冷启动容器正屏、QMI_LOC 1Hz GPS 全链路及 OV13B10 相机实帧出图

本轮按用户确认的 6 阶段计划（含「电池页仅保留两个开关」「外屏加上上滑解锁/回桌面」「阶段 5/6 替换为 GPS 与摄像头」）完成全部逆向、编码、85 项主机 ASan/UBSan 单测、双配置内核编译、增量 `sens-hal-v10` 部署校验及多轮实机静默闭环（全程强制 `zizhan_pulse_dry=1`、`audio_output=0` 零外放；零分区刷写；MPSS 全程锁定 `operating_mode=3` 离线零蜂窝发射；严守位置与网络隐私不记录真实经纬度/SSID/IMEI；`main` 保持 `dac504b` 不变）。

## 1. Stage 1：修复 Wi-Fi 与常驻音频守护进程 APPS SMMU 租约死锁（连续 9/5 次冷启动全通）

- **根因定位**：`ZizhanAudioDmaMap.cpp` 在 `zizhan_audio_dma_map_begin()` 安装常驻音频环 Stage-1 页表（`slot=112 bank=73`）后一直持有全局 `pong_smmu_lease_lock()` 直至守护进程退出，导致后启动的 `PongWifi` 无法获取 SMMU 锁映射 `64 MiB` WLAN DMA 池（`video-2` 中 `en0` 打不开）。
- **改动与验证**：在 `drivers/PongPower/ZizhanAudioDmaMap.cpp` 完成寄存器回读校验后立即释放 `pong_smmu_lease_unlock()`，并在 `end()` 拆除阶段重新加锁保护；更新 `tools/zizhan/tests/cs35l41-dma-platform.py`。实机连续 **9 次冷启动**（`ux-s14-v1`、`gnss-1..4`、`cam-cap-2`、`cam-cap-5`、`cam-cap-6`、`soak-1126-eve`）100% 同时拉起 `PongWifi`（DHCP IPv4 正常收发 `10,278` 帧）与 `ZIZHAN_AUDIO_DAEMON: SUMMARY PASS`（`starved=0`）。

## 2. Stage 2：消除展开屏幕亮度瞬冲 100%/50% 闪白 + 外屏单指上滑解锁与下巴上滑回桌面

- **2a 展开亮度防闪**：`CoreBrightness` 在唤醒亮屏后约 `0.3～1.5 s` 下发软件默认 `1024`（`50%`）覆盖用户设定亮度；`drivers/PongPower/PongBacklight.cpp` 在 `pong_backlight_panel_power(true)` 精确恢复折叠前硬件 DBV，并在唤醒后 `1800 ms` 窗口内拦截自动 `1024` 脉冲（保留手动滑条实时调节）。
- **2b 外屏单指上滑手势**：`drivers/ArmDTPlatform/ZizhanOuterTouchPlan.h` 与 `ZizhanOuterProbe.h` 新增距离传感器防误触保护的外屏时钟态单指上滑（`dy <= -160 px`）切换至 `2:1` 实时外屏桌面，以及桌面态底部下巴（`y >= 1700`）单指上滑注入虚拟手指 `9` 原生 Home/解锁上滑手势（保留双击与底部四胶囊栏，通过 `outer-touch.cpp` 单测与 `ux-s14-v1` / `soak-1126-eve` 实机验证）。

## 3. Stage 3：WCD9385 物理主麦克风增益提升（`+20 dB`）与扬声器 `-26 dB` 软拐点响度曲线

- **麦克风增益**：`drivers/PongPower/ZizhanMicCodec.{h,cpp}` 将 WCD9385 `ADC1` 前置增益由 `step 12` 提至 `step 16`（`+6 dB`），并将 `TX_MACRO DEC0_VOL`（`0x033c3000`）由 `0 dB` 提至 `+14 dB`（`0x18`，合计 `+20 dB`），消除语音备忘录底噪过低、回放偏轻问题。
- **扬声器响度曲线**：`userland/ZizhanAudioPlugIn.c` 将 CoreAudio 音量下限由 `-40 dB` 收紧为 `-26 dB`，并引入 `+3 dB` 三次 Hermite 软拐点防削顶限幅器，使 `80%` 滑条处有效增益较旧版提升 **`+9.3 dB`**（峰值严格 `<= 32767` 不破音）。

## 4. Stage 4：「设置 → 电池」双开关精简模式 + 冷启动应用容器旋转彻底对齐

- **4a 双开关精简电池页**：按用户指示在 `userland/zizhan-battery-ui18.h` 与 `userland/settings_compat18.c`（默认 `zizhan_batt_ui=2`）过滤不工作的图表/健康占位项，**仅保留 `BATTERY_PERCENTAGE_IDENTIFIER`（电池百分比）与 `LOW_POWER_MODE_IDENTIFIER`（低电量模式）两个原生可用开关**（`soak-1126-eve.shot3.png` 实机截图确认竖屏干净无转圈）。
- **4b 冷启动容器方向补齐**：除 `FBSSceneSettings` 外，`SpringBoard` `SBDeviceApplicationSceneHandle` 在 `currentInterfaceOrientation == 0` 时其 `defaultInterfaceOrientation` 与 `_launchingInterfaceOrientationForOrientation:` 仍回退 `1`（导致「设置」冷启动容器闪横、FaceTime 左侧闪过底部小白条）；`userland/ui_compat18.c` 与 `zizhan-scene-orient18.h` 同步对齐容器启动方向，`ux-s14-v1` 与 `soak-1126-eve` 实机验证首帧与容器全程锁定竖屏 `4`（`958×1080`）。

## 5. Stage 5：3-DSP 并发 + 高通 MPSS `QMI_LOC` 1 Hz 硬件定位引擎 → iPadOS `CoreLocation` 桥接

- **3-DSP 与离线射频隔离**：`ZizhanDspResources.h`、`PongAdsp.cpp`、`ZizhanSlpi.cpp`、`ZizhanMpss.cpp`、`ZizhanRmtfs.h` 打通 `ADSP`（PAS 1）+ `SLPI`（PAS 12）+ `MPSS`（PAS 4）+ 内存态只读 `rmtfs` 并发运行与干净停机（`pas_shutdown x0=0`），全程锁定 `DMS operating_mode=3` 杜绝任何蜂窝发射。
- **`QMI_LOC v2` 协议闭环（`ZizhanQmiLoc.h`）**：逆向 `modem.mdt` `LOC v2.152` 消息描述符表，按 `AFW` 框架客户端发送 `0x0021 REG_EVENTS` → `0x003E SET_ENGINE_LOCK(NONE)` → `0x0022 START(1000 ms)`，`gnss-4` 与 `soak-1126-eve` 实机全部返回 `res=0 err=0`，引擎进入 `FIX_SESSION_STATE = STARTED(1)` 并以 1 Hz 稳定推送 `0x0024` 定位报告（室内无天空遮挡条件下 `status=1 IN_PROGRESS` 连续流式运行 `170+ s`）。
- **用户态无挂起 `CoreLocation` 桥接（`zizhan-cllocation18.h`）**：通过 `/dev/zizhan-sensors` 类型 `23` → `touch.c` `notify_set_state64` → `CLLocationManager` 方法调配直供实时 `CLLocation`（绕开 `locationd` XPC 阻塞，`Zizhan loc18 probe[5]: services_enabled=1 cl_abi_ok=1 ... seq=10`）。

## 6. Stage 6：`CAM-01` 超广角 `OV13B10` 物理 4-lane MIPI CSI-2 → VFE-lite0 实帧 RAW10 采集出图

- **全链路硬件管线（`ZizhanCamIsp.h`、`ZizhanCamCapture.h`）**：完成 `GCC_CAMERA` + `CAM_CC PLL0`（`400 MHz`）+ `TITAN_TOP GDSC` + `APPS SMMU` `camera_region`（`0x9f500000+8 MiB`，`2048` 页 Stage-1 映射，避开 `NativeUfs` 占用的 bypass `bank 77` 并保持共享 `ldob5/ldoc10` 供电，按 `0xc00001fe` 掩码判读 QSMMU500 `CBn_FSR`）+ `PM8008` + `CCI0` `OV13B10`（`0x560d42`）`1364×768` 寄存器表 + `CSIPHY1`（4-lane `1120 Mbps`）+ `CSID-lite0 RDI0` + `VFE-lite0 WM0` DMA 写入与 `SGRBG10` 降采样预览编码。
- **实机验证（`cam-cap-6`、`soak-1126-eve`，`build-play201` `c0e1c460bf42…`）**：
  - 内部 `TPG1` 自检：`pkts=813`，`nz=525312/1047552`，`min=0 max=1023 mean=513`。
  - 物理 `OV13B10` 实时光学帧与传感器 MIPI 彩条帧：`frames=2`，`pkts=1540`，**`ecc=0x0 crc=0x0`**，**`nz=1047552/1047552`（100% 像素完整落盘）**，成功解码导出无损 `cam-cap-6-ov13b10_live.png` 与 8 色标准彩条 `cam-cap-6-ov13b10_colorbar.png`，且 `SMMU teardown slot=108 bank=69 disabled=1` 干净回收。

## 7. 15 分钟全子系统静默浸泡 `soak-1126-eve` 与在场产物就绪

- **`soak-1126-eve`（`build-play201` `c0e1c460bf42…` + 已安装校验 `sens-hal-v10`，`900 s`）**：全开 Wi-Fi（`192.168.31.185`，`10,278` 帧）、静音多会话全双工音频与校准主麦（`sessions=4`，输出 `789,120` 帧 `starved=0`，拾音 `869,664` 帧，`audio_output=0`）、`3 Mbps` 蓝牙（`rx=58,577 B`、`tx=18,989 B`、`76` 次唤醒 `fail=0`）、7 路 SLPI 传感器（`37,553` 次加速度/陀螺、`3,614` 次磁力、`3,609` 次气压 `99,614 Pa`、`4/4` 次待机恢复）、`MPSS + QMI_LOC` 1 Hz 定位流与干净关停、2 轮合盖时钟/外屏桌面/展开循环、锁向冷启动双开关电池页截图（`shot3..6.png` 均 `120/120` 行完整），CPU 峰值 **`54.4 °C`**（GPU `49.7 °C`，限频 `0/78`），**`0` panic**，到期平稳回 Android（`32.4 °C`）。
- **在场验收包与 M3 包（均设 `ZIZHAN_USER_PRESENT=yes` 门禁，零分区写入）**：`out/video-3`（`18a2a812303b…`，`run-video-3.sh`）、`out/cam-live-1`（`9173e6791b98…`，`run-cam-live.sh` 实景拍照自动解码 PNG）、`out/fold-desk-2`（`467051fcefb4…`）、`work/m3-standalone-1127/pong-xnu-aprime.img`（`7d4baddaf301…`，`M3_STANDALONE_PREFLIGHT: PASS`）。
