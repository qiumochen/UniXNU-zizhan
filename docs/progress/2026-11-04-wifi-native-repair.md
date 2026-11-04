# 2026-11-04：原生 Wi-Fi 接口修正与跨频段故障

本轮接续 [硬件待办](../plan/2026-11-04-next-hardware-work.md) 和 [g290/g291](2026-11-04-wifi-usb.md)。用户授权无人值守闭环；Wi-Fi 遇到当前条件不能突破的阻碍后继续 GPU。所有设备轮次仅临时启动，使用后部 USB-A，保留自动返回；没有刷写分区。

最终网络源码提交 `fbac256`。Wi-Fi、CI、GPU 配置均完成主机构建；主机等待/租约/USB 回放、原生 wifid 作业与缓存接线回放通过。下表 g292–g295 是形成该修补过程中各自的本地快照，包含已撤销的 g294 实验，不能都视作同一源码版本。

## 参考与实现

- 对照 [PongWifi d2e33ca](https://github.com/Chengdong-Yu/pong-wifi/tree/d2e33ca6e0f6a3b3e7881a45ab36c6920e7afb07)：延后注册回调中的 station 启动，等 QMI/core 注册栈返回；Ethernet MSDU 与 WMI/HTT 的收发 offload 模式一致。没有迁入 Pong 的供电、PHY、GPIO、SMMU 或固件数据。
- 修正本机共享 shim 的有界等待：原代码只在 `lx_pump()` 返回无工作时检查期限，持续轮询 IRQ 时可能无限等候。zizhan 的兼容头在每次迭代检查期限，并保留 WMI 条件成功时只求值一次的 skb 所有权规则。主机回放覆盖空闲/持续忙碌、补充 credit、时钟绕回和零期限已就绪条件。
- 首次 MHI SYS_ERROR/FATAL/RDDM 记录故障和最近 WMI，设置真实 CRASH_FLUSH，停止可选 Wi-Fi worker，保留 DMA 存储，正常 iOS 启动和真实 panic 处理继续。网络接口收到失败/断链通知；没有在崩溃后的合作式回调里反复重入整套固件重置。WMI 记录点在 HTC 发送前，日志编号表示提交尝试，不能作为成功发送计数。
- 对照同一 Pong 源码补上 22H374 的 `IO80211Interface` user client：selector 0 转交同一 Apple80211 实现，处理内联/OOL 缓冲，先准备描述符再访问，忽略信封里的用户地址；锁外完成用户页面复制。解码原生 0x384 与旧 0x250 加入结构，保留原生不支持/失败结果。原有实时 DHCP ACK、独立租约会话和测试开关保留。
- [Pong services93](https://github.com/Chengdong-Yu/UniXNU-pong/blob/main/migration/ipados18/services93-wifi-checkin.md) 与 [services106](https://github.com/Chengdong-Yu/UniXNU-pong/blob/main/migration/ipados18/services106-mobilewifi-ui-wait.md) 指向缺少传统 MobileWiFi 接收端口；本机 g293 也反复查找 `com.apple.wifi.manager` 失败。为现有 Apple wifid 的规范 plist 和缓存补登记该端口，采用实际 check-in 后可见的原生策略，没有替代服务器或固定联网回答。

K90 的 [wd30→wd31](https://github.com/iola1999/hackios-redmi-k90-annibale/blob/d0cd74d49d7d2ccdb00cb0fad565feaf7ad487f5/docs/progress/2026-11-01-wifi-ath12k-driver.md) 仍是原生服务建立的参考。本机已有 en0 配置，不能仅凭兄弟项目结果宣称再次创建服务就能解决。最新 [Ath12kWifi](https://github.com/UniXNU/Ath12kWifi/tree/28de7b91d7a880682b777e0e3564245a99dbc3f5) 已将生产路径移到 cfg80211/mac80211 + wpa_supplicant，旧 `nw_sta.c` 只留测试；本机仍是 QCA6490/ath11k 的 legacy station，整套替换尚未实施。

## 真机结果

| 轮次 | 结果 |
| --- | --- |
| g292 | 2.4 GHz 首次联网、主动续租后两次 HTTPS 200 / 577 字节，断线清理通过；切到另一 BSSID 的 5 GHz 时出现 RDDM，故障前 WMI 与启动段都保留，正常返回 Android。 |
| g293 | 无凭据内核扫描 12 个网络；原生 IO80211 user client 实际打开，能力、信道、电源查询成功。没有观察到原生扫描/加入 SET；Settings URL accepted=0，扫描输出帧为 0，不能称设置页通过。 |
| g294 | 试验在频段变化时通过真实 remove/add 重建 station vdev。仍在管理帧发送阶段出现 RDDM，无第三次 HTTPS；该实验已撤销，未纳入最终源码。 |
| g295 | wifid 端口修补后没有再记录缺失 manager 端口的查找；原生接口查询继续成功，但仍无原生扫描/加入 SET，Settings URL accepted=0、扫描输出帧为 0。没有宣称原生入网完成。 |

[脱敏设备证据](../evidence/2026-11-04-wifi-native-repair.txt)。原始日志和私有网络输入仅留本机。g295 内核无测试密码和自动关联输入；新增端口的系统镜像由已安装 g290 副本克隆，仅 7 个 OS 块变化。安装前核对手机固定 extent、旧 manifest 及全部待写块，安装器逐块读回；Data/SystemOS 没有重装。g290 副本和安装状态保留供回退。

## 当前结论与接续

本轮修正了原生接口绑定和缺失端口，保住了首次固件故障，也实测了 2.4 GHz 首次联网及续租。**跨频段重连和原生设置页入网仍未通过**；g290 同 AP 生命周期通过的历史结论保持独立，不能替代这些验收。

Wi-Fi 接续应先取得 RDDM 的首次固件诊断与重连前后的真实 channel/peer/管理 TX 状态，再评估 legacy station 的生命周期或向完整 mac80211 迁移；不再重复相同无新增取证的跨频段轮次。原生管理路径还需分开查明接口命名完成、MobileWiFi 收发和 Settings 启动失败；不能用自制界面或固定设备字典替代系统结果。自然 T1、不同 SSID/子网和实际 GIC MSI 仍待验证。

按用户要求，随后转入 GPU display-link A/B：同一内核、同一 OS、同一合成滑动、无 Wi-Fi，仅切换 `pong_dl_full=0`；分别记录加载、首帧、帧间隔和时间线。其结果另记，不能作为 Wi-Fi 通过证据。
