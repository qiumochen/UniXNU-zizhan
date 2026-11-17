# 传感器、GNSS、音频与折叠接续（2026-11-14）

## 目标与边界

用户要求一小时内集中查看 GPS、传感器、声音和内外屏切换。本轮先使用 Android 原生状态/传感器服务作硬件输入基线；Android 通过不等于 iPadOS 驱动已接通。iPadOS 临时启动仍有有界自动返回，不刷写分区、不离线改 Data/TCC、不注入 GNSS 或模拟天气。

## 已有设备证据

- **传感器存在**：Android `sensorservice` 枚举 LSM6DSO accelerometer/gyroscope、AK0991X magnetometer、前/后 TCS3720 ambient light、proximity，以及 Xiaomi `fold_status` wake/non-wake 和 `oem_hinge_angle` wake/non-wake。温度传感器另有历史 XNU g396 32路一次性 VALID 读取，但未有持续热策略。
- **完整折叠事件**：`work/app01-20261114/android-sensorservice-20261114.txt` 最初记录展开 178°，`work/app01-20261114/fold-android-20261114-b.txt` 后续包含两次完整事件序列：178°→166→142→103→86→30→22→14→4°，fold closed 位由0到1；展开经过13/26/34°，状态回0并到90/114/144/175/178°。第二次同样到5° closed=1，再展开于34° closed=0。用户确认当时确实完全合拢。结论是 Android fold/hinge 传感器可靠地产生变化；本机 Hall 极性、与 iPadOS GPIO 输入的一致性仍未实测。
- **屏幕路由快照**：Android `dumpsys display` 读到内屏 1914×2160，内屏 ON；第二个 1080×2520 display device OFF。采样过程中没有提取到按折叠时间点聚类的完整 display 状态转移，因此不能说 Android 没有/有自动外屏切换。iPadOS 当前只支持主内屏；仓库调查确认外屏需要 DSI1/PHY1/INTF2、另一 L18 panel init、SE19触控和独立显示owner，自动切屏未实现。
- **GNSS 现状**：位置 master switch enabled；`com.miui.weather2` 的 fine/coarse appops 均允许。`dumpsys location` 显示 gps provider enabled/allowed，但 `ProviderRequest[OFF]`、`last location=null`、`mStarted=false`、location reports/TTFF/satellite status 全为0。启动天气时设备仍在锁屏，Weather没有形成活动定位请求；这不证明 GNSS 硬件故障。Android Settings Date & Time 后来读到自动时间开、时区行 spinner；经普通 UI 关闭自动时间后时区显示 Los Angeles、时钟仍落后 Shanghai/host 16小时。仅此不足以解释 Weather 的 `--`；尚未选择正确时区，也未观察 WeatherKit因果。
- **音频**：Android AudioService当前 music route 为 speaker；尚未取得声学/麦克风实测结果。公开 zizhan板级输入为 CS35L41 两颗，SE15 I2C/GPIO56/57、IRQ88/118、GPIO1 reset；Pong TFA9873/SE13常量不兼容，`pong_audio=0`保持。故本小时先做原生 Android 输出/内置麦克风操作基线，iPadOS出声仍需 CS35L41协议、真实live DT、调音和 ADSP 路由，不临时照搬或打开 Pong 功放路径。
- **温度等待**：按用户要求，维护 `cycle.sh`、APP-01 supervisors 取消35°C启动门槛，只记录电池温度。自动返回、pinned klog、每轮单一owner和最长30分钟仍保留；高温/长时间运行仍应按实测结果标记日志质量，不把可能受热/ramoops污染的轮次作为完整证据。
- **存储清理**：仅删除25个旧22G100 XNU/Metal编译输出和Rust target缓存，删除前检查无cycle/installer/build process和挂载。`cleanup-build-artifacts-20261114.json`记录路径、原始大小和保留范围，计划约6.57 GiB；work/native18从约107 GiB降至约100 GiB。raw候选OS/SystemOS/blank Data、隔离APFS镜像、KDK、source snapshots、receipts/logs、当前root-candidate-display build、所有22H374 canonical/backup/Data与pinned extent仍在。当前总卷由约93 GiB空闲增加至约99 GiB。

## 一小时人工验证次序

每项先报开始/结束与手机形态；屏幕亮着、保持 Android/USB-A，不让其进入锁屏或换启动环境。

1. **GNSS（约3–5分钟）**：室外或无遮挡窗边；解锁后打开 MIUI Weather，进入当前位置页并保持前台、亮屏。主机观察 activity、GPS Request、GNSS sessions、satellite count、TTFF、last location；有 fix 后核位置城市/地图精度，再单独开 iPadOS 临时 Weather 做相同现场请求。无 fix 时留空，不用 mock location。
2. **折叠传感器与显示（约5分钟）**：从完全展开平放开始，合盖到两侧接近贴合，停5秒，再展开平放，重复两次；同时记录 hinge angle/fold status、两个 display device 的分辨率/state/默认显示/foreground task、触控输入设备。已经有一次完整 Android sensor 状态翻转；再次同步 display 数据以验证面板路由关联。不要在 iPadOS 宣称外屏已支持。
3. **音频（约5分钟）**：Android 播放器播放短的本地/系统测试音，逐步从低音量增加到清晰可闻，暂停/恢复；用普通录音机做近讲/环境采样，检查录音波形/回放。核对输出是主扬声器还是听筒、左右/底部单元、失真和静音。用户无需把麦克风对准私人对话；保存只读测量/结果，不保存敏感录音内容。iPadOS `pong_audio=0`期间只记录未支持。
4. **基础传感器（约5分钟）**：打开指南针/系统自动亮度，分别平放/翻转/遮住前后光感，读 Android sensor timestamps/value和自动亮度变化；不做快速摇晃或高负荷测试。此为 Android 输入基线，不是 iPadOS sensor framework 资格。

## 后续实现顺序

1. GNSS只读请求/permissions/硬件状态先获得有效 fix 或具体 HAL 错误；Weather 固定城市数据与 GNSS分开验。
2. 更新 FOLD-01 Android display 状态转移证据，然后在 iPadOS完成外屏静态 single-panel 初始化、扫描帧和 SE19触控之前，不做自动切屏。
3. Audio-01 先从本机 live DT核对 CS35L41供电/IRQ/复位，分离只读chip ID、真实codec初始化、ADSP路由、测试音和恢复。
4. 传感器若要进入 iPadOS，按适用 Android/HAL/XNU栈逐类列依赖；先调查 hinge/fold和ALS，不把 Android sensor service视作现成 Darwin API。
