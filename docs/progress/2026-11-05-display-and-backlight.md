# 冷启动画布、首帧分段与内屏亮度（2026-11-05）

按用户要求无人值守推进 [硬件待办](../plan/2026-11-04-next-hardware-work.md) 的显示稳定性、首帧/设置滚动和内屏亮度。全部设备轮次使用后部 USB-A、临时 `fastboot boot` 和自动返回，真实 panic 保留。私有镜像、固件、网络信息和原始日志留在本机。

## 面板池映射

历史 g342 在背光尚未开启时，就出现过 3756 次面板槽映射失败；旧等待汇总停在 WidgetRenderer 的一笔提交，距最后提交约 323 秒。后续发现该字段是最近等待时的快照，不能据此证明 GPU 硬件仍未完成；画布反复创建失败及画面不推进是独立的实际故障证据。

原画布路径在每次创建 IOSurface 时，重新为 loader 保留的物理槽建立 kernel pager/mapping。此前截图调试也记录过应用启动后新的物理映射被拒绝，而启动早期建立的映射可用。修正现对实际已分配的槽，在显示初始化时分别 prepare、建立缓存 kernel 映射并保持生命周期；画布只借用有界的 subdescriptor。无有效映射的槽不会交给客户端，普通真实分配/拷贝路径仍可回退。

`pong_slot_premap=0` 保留旧路径供对照。真实 descriptor/map 的持有与 prepare/complete 配对，不改变 cache 属性、物理池位置或 GPU fence 的成功条件。新增有界失败日志保留原始 VM-fault 错误；不会将失败转换成成功，也不无限刷日志。

g346 普通主屏画布记录 `cached kernel map reused=1`，三张完整截图及 HTTPS 通过。g348 的三个预映射均成功，设置页和三张截图通过。g347 旧路径与 g348 新路径的原生 IOSurface 延迟分配探针均成功，但没有确认探针取到面板槽，不能把它们写成旧失败/新成功。历史 g342 的长停仍未稳定复现；本次完成映射生命周期加固，不宣称所有 GPU 长停均已解决。

g345 的 deadline fence 虽差一笔，最新提交只有 67 ms，trace 中 153 笔提交全完成、零丢失，属于返回时在途；它不是 g342 那种持续长停。

## 修正 fence 汇总的过期快照语义

g369 的 120 Hz 设置页与 g371 的 60 Hz 补图轮都显示旧汇总差一笔，最后提交距 deadline 分别约 38.8 秒、21 秒。最初将其判作长停，但继续读取代码发现：`native_adreno_fence_wait` 中的“最新完成”只在等待时更新，实际工作稍后完成且没有再次等待时，该快照可以保持旧值。最后提交的年龄不能单独证明它仍未完成。g372 的实际 outstanding-fence 监测没有触发，进一步要求直接取证。

因此保留旧字段作为明确标注的 `wait snapshot`，另外在汇总中通过 cache acquire 直接读取实际 CP 写入时间戳与 ring 的最后提交值。只读监测也仅在真实 outstanding fence 超过两秒后采集 CP/IB/SMMU 状态，每 fence 一次、每轮最多四次；不复位 GPU、不提前释放 BO，也不伪造完成值。

调查中发现同 A730 的 PongAdreno 有新的用户 IB-list 边界协议，已做主机结构回放，但该候选未上机且已从本机源中撤下。旧汇总并非充分故障证据，不把未经设备验证的协议改动作为修复发布。最终 g374 硬件控制使用原有 ring 协议，只增加真实完成值观测：旧等待快照为 763/762，实际 CP 为提交 763、完成 763，未触发真实 outstanding-fence 监测。三张设置截图完整、HTTPS 200、零画布映射错误或 backboardd 崩溃。这个对照直接证明该类“差一笔长停”是汇总误报，已修正诊断语义。历史 g342 的映射失败仍是实际问题，不因此被改写为整轮成功。

## 首帧与设置滚动

`touch_lat=1` 下增加有界时间戳：内核触摸样本、backboardd 实际读出/派发、原生 pan gesture 的 Began/首次 Changed/Ended、swap 与实际面板完成。手势观测只添加 target，不改变阈值或事件。应用 `sendEvent:` 观察未取得有效样本，已撤下；没有用缺失数据填零。

FTS 合成输入增加可选 `tsdelay`、`tsv` 和有界自定义拖动参数，用真实 HID 队列驱动场景。默认真实触摸路径保持原策略。性能与截图分轮，原始日志缺失、缓冲覆盖或窗口不匹配的样本不补算。

| 轮次 / 场景 | 保留样本 | 实测结果 |
| --- | ---: | --- |
| g349，设置页竖向滚动 | 49 次匹配 DOWN | 样本到 HID 读出中位数 41 µs，派发 265 µs；DOWN 到 swap 20.60 ms、到面板 26.03 ms。早期 URL 日志被覆盖，场景截图需独立核对。 |
| g352，主屏分页 | 12 组完整分段 | HID 读出 0.039 ms、派发 0.213 ms；手势确认 25.77 ms，确认到 swap 27.96 ms，再到面板 10.88 ms。各段中位数不能相加当作总中位数。 |
| g363，主屏静止间隔 4 秒 | 17 次静止 DOWN | 到首帧中位数 79.76 ms，p90 87.87 ms。原生手势确认后到 swap 仍有明显时间。 |
| g365，受控提前提交候选 | 16 次静止 DOWN | 到首帧中位数 301.68 ms，p90 323.92 ms；31 次实际内容偏移后的 flush。候选明确退步，已完全撤销。 |

提前提交没有通过，所以不改变默认提交时机，也不降低手势识别门槛。已有 display-link 类名修复保留；约 80 ms 的主屏首帧仍待解决。本轮把瓶颈缩小到原生手势确认和前端交帧之间，不能据此宣称 GPU 提频或 BO 缓存会解决它。

`tools/zizhan/mac/frame-latency.py` 从保留的实际记录输出样本数、中位数和 p90；静止要求上次 swap 距 DOWN 至少 90 ms，缺失匹配直接排除。

## zizhan 内屏 DCS 与所有权

本机 live DT 的 L18 primary 面板为 2160×1916、DSI0、VC0、DCS 调光、反转 DBV 字节序，最大 2047、非零最小 4。Pong 的 NT37705 4095/10 参数不适合本机。新增 `configs/zizhan-display-sm8475.json` 和 version 2 ADT，驱动严格核对自己的面板参数；旧 Pong 参数在本机被拒绝。

刷新率切换与异步亮度事务共享真实 DSI FIFO，因此用原子 owner lease 串行化；亮度持有到完成及寄存器恢复，刷新率忙时留待下一帧。超时后若 DMA 仍忙，保留所有权并停止依赖工作，不复位未完成的 FIFO。CPU-only 配置有自己的控制器 owner，不产生未定义 GPU 引用。

g350 只读探针实际回读 307/2047、亮度写入 0；随后共享缓存签名检查 ENOMEM 导致真实 launchd panic。相同镜像重跑完整启动，再次回读 307，截图和 HTTPS 通过。新增签名缓存分配/读取阶段诊断，只记录真实错误，不绕过签名或 panic。

g351 在不发布 CoreBrightness 服务时完成 **307 → 64 → 512 → 307**，三次写入及匹配回读，恢复原值。该结果证明 DCS 写入与读回，不等于已接通设置滑条。

## 原生亮度链

g353 发布真实服务后 backboardd 反复 SIGSEGV。g355 有界致命信号记录确认 15 次同一崩溃位置：22H374 CoreBrightness 的 `_DisplaySliderToLogicalBrightnessInternal +272`，经 DisplayCreate 和 CBDisplayContaineriOS 初始化链。捕获后恢复默认处理并重发原始信号，没有吞掉崩溃。

参考同版本 [Pong services85](https://github.com/Chengdong-Yu/UniXNU-pong/blob/e0ee6df109b4b5dc706cac660d4f8210dc911dac/migration/ipados18/services85-backlight-startup.md)：该运行时先换算极值，后创建自己的默认曲线，空映射窗口仍解引用首项。独立核对本机映像 UUID、入口指令和崩溃调用链后，适配只在真实 zizhan 背光已回读发布、匹配的 CoreBrightness、且内部映射为 null 时使用该映像自带软件默认曲线；映射存在后调用原函数。这不是面板光度校准，不发布伪造 nits。主机回放直接编译实际 helper，验证空映射、边界、原状态不变和原函数透传。

g356 界面恢复，原生请求 1024 获得真实回读。g359/g360 发现原生滑条请求还有 `backlight-level` OSNumber；同版本 [PongPower 实现](https://github.com/Chengdong-Yu/pong-power/blob/c37179ed12ba7f981b2b088aea482b969085eb84/PongBacklight.cpp) 将其作为可选电流表元数据。驱动现校验并记录该字段，DCS 仍只写 `brightness`。未知的 CompensationTweak 字典仍拒绝，不伪造补偿能力。

g361 操作设置页实际可见 PSSegmentableSlider：**25% → 65% → 50%**，实际收到目标 **512 → 1331 → 1024**，每步真实回读匹配，原始 50% 恢复，零 backboardd 崩溃，两张完整截图。仅 RUNNING 的 UI 动作不作为 PASS，设备回读是独立依据。

g364-hid-thumb 用真实 HID 从实际滑块中心拖到较高位置再反向，驱动收到连续原生请求，目标升至 1986、回到 1028，实际回读匹配；随后原生设置恢复已确认的 50%/1024，63 次原生请求、42 次实际写入，零 backboardd 崩溃。前一轮从轨道空白处开始的拖动未命中滑块，只有事件派发、没有亮度变化，作为失败控制保留。

默认设备配置在这些验证后开启 `CONFIG_XNU_PONG_BACKLIGHT=y`，不再需要独立背光 profile。g367 使用正常模式，未启用亮度资格探针或 UI 动作，实际 60.5 帧/秒、亮度 1024 回读及 HTTPS 200，零映射失败/捕获的backboardd 崩溃；截图只有一张且日志缺块，不记完整图像通过。g368 切回默认 120 Hz、无 Settings URL、使用最终干净 UI，三张主屏截图完整、HTTPS 200、零映射失败/backboardd 崩溃，实际扫描 120.1 帧/秒。deadline 的新提交仅 58 ms 在途，不记为长停。

屏幕截图不能测量物理亮度；本轮只证明实际 DCS 控制和面板回读，未测光度校准。g374 正常 120 Hz 设置页与 g375 正常 60 Hz 分别取得三张、两张完整截图和 HTTPS 200。两轮直接 CP 读数分别为 763/763、665/665，旧等待快照差一笔不代表硬件仍未完成；没有捕获到真实两秒 outstanding-fence。两种刷新率正常启动与亮度服务共存通过，未将其扩大为睡眠恢复或任意运行中刷新率切换的资格。


## 证据与回退

完整源码 revision、主机结果及脱敏设备节选见 [证据文件](../evidence/2026-11-05-display-and-backlight.txt)。默认关闭所有 UI 自动操作、合成手势和延迟观测；`pong_backlight=0` 保留固件亮度，`pong_slot_premap=0` 供旧映射控制。真实资源失败、签名校验失败与 fatal signal 保留，不用关闭错误处理来制造通过。


## 最终准备态与验证范围

实际干净 trial 的原始 wifid、公共 UI/helper、OS 全部 chunk 和整体哈希、手机实际连续 extent 及 manifest 校验后，已同步默认准备 OS、overlay、layout、4K 描述符和安装 state。旧 OS/UI/helper 与布局状态完整保留。推广过程未再写手机；设备已回 Android。

完整默认内核和精简 CI 内核均构建通过；实际 brightness helper 回放、DSI owner 并发测试、既有 Wi-Fi 回归通过。默认正常包同时开启 GPU、Wi-Fi 与 L18 亮度，测试动作、合成触摸和观测开关均未启用。新增显示主机工作流与现有记录、Wi-Fi 回归、macOS 构建分别验收。

本轮落地映射加固、正确 fence 观测和可用的内屏亮度；主屏首帧约 80 ms 仍是后续工作。g342 的罕见画布失败没有稳定复现，不能用多次成功启动承诺它绝不再发生。面板光度校准、自动环境光、睡眠恢复、外屏、真实手指主观流畅度仍未资格化。
