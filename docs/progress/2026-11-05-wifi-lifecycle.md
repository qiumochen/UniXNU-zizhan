# Wi-Fi：自动加入、连接保持与原生开关（2026-11-05）

接续 [设置页与跨频段验证](2026-11-04-wifi-settings-and-roaming.md)。全部设备轮次使用后部 USB-A、临时 `fastboot boot` 和自动返回，完整备份保留；没有 flash 分区。主机构建、回放和设备功能分别记录。

## 自动加入后的周期断开

g331/g332 已能由原始 wifid 使用保存的凭据自动加入并完成 HTTPS，但随后约一分钟主动离开。真实 wifid 文件日志在 g334 中给出 `Not Primary Interface (1005)` 和 PrimaryTimeout。此前 helper 已取得公网路由，wifid 却未及时把接口认作 IPv4 routable；HTTPS 成功不足以证明原生管理状态正确。

DHCP ACK 补偿服务只有 State IPv4/DNS，缺少守护进程用于把服务映射回接口的 Setup Interface。修复从现有原生服务复制实际 `en0` 的 Interface 绑定，在同一次 `SCDynamicStoreSetMultiple` 中发布 session 自有的 Setup 绑定与真实 ACK 配置。没有另起 DHCP 客户端，没有制造硬件接口，也不覆盖原生服务。找不到真实绑定即撤销补偿；断开和原生接手时一起撤销 Setup、IPv4、DNS 和路由。

g335-fixed 原生 IPv4 routable 通知及时到达，单次关联和 HTTPS 200 后保持至约 247 秒，没有再次 link loss。g335 的首次打包因本机重打包重复附加基础参数，超过命令行上限而被 loader 拒绝；它没有启动内核，修正本机参数拼接后才进行实际设备轮次。

## 固件 dispatcher 与系统网络通知

g336 使用原始 wifid、无 fixture、无 Settings 启动，完成密钥安装后固件 worker 停止推进，未取得 IP 或 HTTPS。原实现从 cooperative firmware dispatcher 同步发送 ifnet/socket 通知，接收者可能等待仍需由该 dispatcher 处理的工作。

通知现进入有界 32 项 FIFO，由既有 `thread_call` 在 firmware dispatcher 和网络锁之外派发。队列保留每一次上下线边缘；满队列会明确使接口失败并丢弃旧成功通知，不伪造成功。主机回放验证顺序、容量、环绕及失败重置。

g337 使用原始 wifid、无测试 fixture、不打开 Settings，实际默认 DMA 64 MiB，自动关联一次、HTTPS 200，连接与帧数持续到约 309 秒，零 link loss。默认 DMA 改为已经多轮验证的真实 64 MiB；显式选择 128 MiB 仍保留申请失败后回退 64 MiB 的路径。所有分配器和 SMMU 边界使用真实长度。g333 的 shared-cache ENOMEM 真正 panic 仍保留为失败记录，不能据此断言 DMA 预算是其唯一原因。

## 原生开关后的第二次连接

本机测试版 UI 只操作 Settings 现有 UISwitch：首次入网后等待 60 秒关闭，20 秒后打开。fixture 仅含 `toggle_only`，没有 SSID 或密码；stock wifid 从系统保存的凭据发出关联。测试版 helper 在首次 HTTPS 后等待真实断开、检查 session 清理，再等待新的 DHCP ACK 和第二次 HTTPS。这些临时测试源码和镜像留在仓库外。

g338/g339/g340 首次 HTTPS 与断开清理通过，开关重新打开后再次认证、密钥安装和通知投递成功，但没有第二个 DHCP ACK/HTTPS，不能记作开关闭环通过。g339 的内核线程采样显示 Wi-Fi worker 从正常睡眠转为持续 RUNNING；g340 的只读进度诊断显示它卡在 `nw_wifi_step`，内层 pump 已返回而外层深度仍为 1。

固定共享 shim 的 timer 遍历在回调前缓存后继节点。回调的 WMI 等待可以递归 `lx_pump()`，内层摘掉后继节点并 `list_del_init()` 后，该节点自指；外层返回后沿失效后继无限遍历。设备构建适配保留固定的共享 checkout，在生成的 `lx_rt_pong.c` 中，每次回调后从当前链表重新选取到期且非 running 的任务，单轮最多 256 次。

主机回放直接提取原始与实际适配后的 timer pass 编译运行：旧代码重现递归 pump 挂住；新代码通过递归摘除、取消后继、运行中重排、持续就绪任务预算和未来 deadline 检查。共享源码不匹配时构建和回放明确失败，避免静默套错适配。

g341 使用同一份 g338 本机开关测试 OS，只替换修复后的内核。首次 HTTPS 200，真实关闭后 session/路由/DNS 清理 PASS，重新打开后原生 wifid 完成第二次关联和密钥安装，取得新的 DHCP ACK，第二次 HTTPS 200，helper 的 `native UI lifecycle PASS` 通过。原始包记录确认两组不同的 REQUEST/ACK 事务；不是复用第一次租约日志。仅有预期关闭时的一次 link loss，worker 的 generation 持续增加，至轮次末尾没有重现忙循环。310 秒自动返回，取得 1,843,243 字节新 UFS 日志；三张截图均为完整 120/120 行，网络标识仅留本机。

随后从实际 g338 manifest 制作干净 g342 OS，恢复公共 UI/helper 并移除 fixture。逐块校验手机旧 OS、只写 OS 增量和 manifest 并读回；Data/SystemOS 保留。只读镜像检查确认原始 wifid、无观察库/fixture/临时日志代码、UI/helper 签名和哈希一致，以及原生 job/cache 一致。g342 无 Settings 启动，HTTPS 200，24 个保留的周期样本全部为实际 UP，帧数从 681 增至 292019，worker generation 持续推进，431 秒自动返回。该轮显示画布映射反复失败，未生成截图，并使 console 环形缓冲覆盖了早期关联/握手记录；不能把“日志中关联计数为零”当作没有关联，也不能把它写成完整界面验收。UI 库与此前 g337 使用的干净版本逐字节相同。g343 使用同一份干净 OS，只以正常 URL 打开 Wi-Fi 设置页，无 fixture、无临时测试 UI/helper；stock wifid 自动关联一次，真实密钥安装、DHCP、HTTPS 200，零 link loss，三张完整截图和设置页渲染通过，没有画布映射错误。306 秒自动返回，取得 1,943,231 字节新 UFS 日志。g342 的显示失败仍单列保留，没有改写为通过。

## 准备镜像同步

完成干净轮后，将实际已安装且验证过的 OS、公共 UI/helper 同步到本机准备态及 overlay，更新 layout、4K 描述符和默认安装 state。重新核验实际连续 extent、手机 manifest、本地 OS 所有 chunk 和整体哈希；canonical 与实际 trial 的 manifest/描述符逐字节一致。overlay 不含 fixture/观察库，原生 job/cache 一致，UI/helper 严格签名通过。

旧准备 OS/UI、layout、描述符、安装状态及相关 overlay 元数据保留完整回退副本。同步准备态没有再写手机；设备已正常回 Android。本机路径、镜像、凭据和原始日志不提交。

## 验证边界

自然 T1、不同 SSID/子网、长时间压力、企业/SAE/6 GHz 和真实 GIC MSI 仍需独立验收。当前无线接收仍依赖 polling，默认 GPU 配置保持 Wi-Fi 关闭。短时保持不等于长时间稳定，UI 动作也不等于关联或公网通过。

## 主机与设备证据入口

可公开的结果见 [脱敏证据](../evidence/2026-11-05-wifi-lifecycle.txt)。原始 DHCP、原生服务日志和带网络名称的截图留本机。

从工作根目录运行主机回放和三配置构建：

```sh
python3 UniXNU-zizhan/tools/zizhan/mac/check-wifi-host.py --shared sheng/drivers/Ath12kWifi
ZIZHAN_SRC="$PWD" PONG_WIFI_TEST_CONFIG= DEFCONFIG=zizhan_ipados18_wifi_defconfig bash ci_kernel.sh
ZIZHAN_SRC="$PWD" DEFCONFIG=pong_ipados18_ci_defconfig bash ci_kernel.sh
ZIZHAN_SRC="$PWD" DEFCONFIG=zizhan_ipados18_gpu_defconfig bash ci_kernel.sh
```

g341 的设备命令为 `cycle.sh out/g341-timer-reentry/pong-xnu-initial.img g341-timer-reentry 480`。镜像包含自动返回，原生开关动作依赖前述本机无凭据 fixture 和临时测试 UI/helper；它们不是默认镜像的一部分。根镜像块号、日志块号和实际安装 manifest 均须以本机核验结果为准，不照抄其他设备。
