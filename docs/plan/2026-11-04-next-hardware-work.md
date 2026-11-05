# 下一轮硬件适配待办（2026-11-04）

**本文件保留 11-04 起的实验设计与勾选结果；当前优先级、基线和任务状态统一见 [当前台账](../progress/STATUS.md)。** 11-05 已完成原生 Wi-Fi 基础链与内屏手动亮度，下一主线为首帧延迟，另安排冷启动/网络回归，再做外屏独立出图与触控。源码入口和取舍见 [兄弟项目调研](../research/2026-11-04-sibling-hardware-survey.md)，其中 g287/g290 是历史基线。

发布侧另有 [上游与 CI 待办](../progress/2026-11-04-upstream-ci-audit.md)：统一 package 模板与 action 尚未接入；目录收录、记录检查和内核构建分别验收。

默认使用 [GPU＋Wi-Fi 统一配置](../progress/2026-11-05-default-gpu-wifi.md)，本地默认构建/打包均开启两者；g344 真机自动联网、HTTPS、零 link loss 和完整设置页截图通过。旧 Wi-Fi 名称仅作兼容链接。

## 1. Wi-Fi：设置页入网和换网

11-04 晚间接续：[g299–g309](../progress/2026-11-04-wifi-settings-and-roaming.md) 已恢复大内核镜像的真实显示池，定位并缩短原生 InterfaceNamer 的 240 秒回退等待；g309 用真实 Settings 行、secure text field 和导航栏 Join 完成原生 Apple80211 加入、WPA2、DHCP 与 HTTPS 200。g304/g306 的 2.4↔5 GHz 两个方向、不同 BSSID 均通过首次/主动续租/重连三次 HTTPS 和断线清理。不同 SSID/子网、自然 T1 和真实 MSI 仍独立待验收。

- [x] 无内核自动关联的 Settings 扫描、选择私有测试网络并加入；核对原生加入、DHCP 和 HTTPS（g309）。
- [x] 2.4 GHz 与 5 GHz 两个方向切换到不同 BSSID，核对旧路由/DNS 清除和新租约 HTTPS（g304/g306）。

- [x] 无 UI 私有配置/内核凭据的重启自动入网及 HTTPS（g331/g332），原始 wifid、无设置页启动的对照也通过。
- [x] 定位原生 `Not Primary Interface(1005)`，补齐真实接口的 session Setup 绑定；g337 原始 wifid、无 Settings/fixture 连接保持约 309 秒。
- [x] 原生关开后取得第二次 DHCP ACK/HTTPS；g341 修正 timer 重入后，两组不同 DHCP REQUEST/ACK、两次 HTTPS 200、断开 session/路由/DNS 清理通过。见 [生命周期记录](../progress/2026-11-05-wifi-lifecycle.md)。
- [x] 清理本机开关测试并复验：g343 干净原生设置页自动入网、HTTPS、零 link loss、三张完整截图通过；准备 OS/UI/helper、overlay、layout、描述符和安装 state 已同步，旧副本保留。g342 无 Settings 的显示失败仍单列待查。
- [ ] 验证从一个 SSID/子网切到另一个后，旧路由和 DNS 清除，新网重新取得租约并能访问 HTTPS；另测自然 T1 续租。记录原生路由何时接手、补偿何时退出。保留 g290 的租约补偿作为回退；真实 GIC MSI 到达另行验证。

[g290 基线](../progress/2026-11-04-wifi-usb.md)：测试轮已通过首次联网、主动触发的续租、断开清理和同网重连；无凭据轮只验证了扫描。上述待办不用重做已通过的同网测试。

历史 11-04 无人值守接续：[g292–g295](../progress/2026-11-04-wifi-native-repair.md) 已补原生 IO80211 user client、22H374 加入结构、持续忙碌时的等待期限和首次固件故障回收。2.4 GHz 首次联网/主动续租 HTTPS 通过，跨频段重连仍触发 RDDM；重建 vdev 的无效实验已撤销。wifid 缺失 manager 端口的查找消失，但原生扫描/加入和 Settings 仍未通过。Wi-Fi 暂留待办，按授权转到下节 GPU；后续先补首次固件诊断与原生管理链证据，不重复同条件盲跑。

## 2. GPU：先测首帧和回弹，再选优化

11-05 [显示与亮度接续](../progress/2026-11-05-display-and-backlight.md)：已加固真实面板槽映射生命周期，保留旧路径对照；罕见 g342 的完整根因仍待复现。HID/原生手势/swap/上屏已分别取时，主屏静止首帧约 80 ms，提前 flush 候选明确退步后已撤销。新增诊断不等于性能问题解决。

11-04 [g296–g298 已完成设备对照](../progress/2026-11-04-displaylink-device.md)：旧 hook 类名没有命中实际 `UIKit.InProcessAnimationManager`，已修正并在设备记录到速率过滤。moving FPS 从 56.7–58.4 提高到 95.3，抬手后 1–1.5 秒的帧间隔中位数从约 33.2 ms 降至 8.3 ms。首帧中位数仍约 75–80 ms；Settings 滚动未单独跑，以下综合待办不全勾。

- [x] 确认已安装 UI 库的实际 display-link 目标，修正类名并用相同内核、合成滑动和零拷贝时间戳核验动画尾段。

- [x] 确认设备已加载 display-link 修复；保留 g296–g298 对照，并补原生手势/HID 分段和独立设置滚动样本。不同场景、静止与在动画中的样本分开记录。
- [ ] 按 CA 交帧、编码、GPU 执行、fence 与 DPU 上屏定位慢段。只有测到缓冲交接是瓶颈，再试 Pong 的 IOSurface 批处理；ASTC、壁纸和多任务内容另轮截图验收。

旧 [GPU 准备状态](../progress/2026-11-03-displaylink-host.md) 仅为历史主机构建基线；当前设备结果以 g298 和 11-05 分段记录为准，面板刷新率与应用帧率分别记录。

## 3. 屏幕：内屏调光 → 外屏独立出图 → 折叠切换

- [x] 内屏 DCS 板级输入、真实初始回读、非零档位与恢复，原生 CoreBrightness 滑条及实际 HID 拖动匹配回读通过；默认配置开启亮度。60/120 Hz 和最后干净轮结果见显示记录。
- [ ] 外屏：从本机 Android DT 与原厂资料确认路由和初始化顺序；先验证外屏 60 Hz 测试图、真实帧计数和熄屏恢复，再单独接外屏触控。
- [ ] 切屏：外屏显示和触控通过后，先手动切换，再按同机 LineageOS 的屏幕与触控映射验证折叠事件。验收新屏首帧、触控方向、反复开合和失败恢复。

设备轮次仅临时启动，保留自动返回；每轮记镜像、开关和设备日志。主机构建与设备通过分别记录。
