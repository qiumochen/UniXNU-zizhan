# APP-01：天气、权限与定位诊断

## 目标与已纠正的前提

用户要求继续开发，区分天气无内容是否来自系统授权弹窗或 GPS。首个交付是取得可区分这些假设的实际页面、服务与等待栈证据；天气城市温度与预报仍须设备验收。

接续源码为 `791cc1398eb17e41d8b815d76729d9478d76d4d9`，入口工作树干净。当前 OS/UI 保持 OS370。未改内核、驱动、OS 或 Data。

- 重新查看本机 `pp-weather-v18.shot1.png`：页面已有“上海”和温度占位符 `--`，并有左上侧栏按钮。因此撤回“没有任何文字 / 最可能没有城市”的接续假设；先添加城市没有足够依据。该图仍不能区分保存城市与当前位置名称。
- 只读检查与当前安装 manifest 对齐的 OS 镜像：`weatherd`、`locationd`、`tccd` 的 launchd 缓存项和天气可执行文件存在，Weather Info.plist 包含定位用途声明。存在不代表服务请求、授权或定位成功。
- `pp128-v8r2` 的 `locationd` 与 `tccd` 有成功 spawn 记录；`weatherd` 无匹配日志只能说明已捕获文本中没有记录，不能据此证明没有网络请求。`applog_name` 选择进程日志，不能替代网络跟踪或完整统一日志。
- 历史 [电池诊断](2026-11-01-battery.md) 曾证实 `locationd` 被反复退出的 `fairplayd` 同步请求卡住；OS370 保留该服务，此假设需要新栈复核。不能直接推广旧根因。
- 通用 HTTPS 成功只证明当时的测试站点可达，Apple 天气接口尚未验证。定位硬件和系统权限弹窗也尚未取得成功或失败的直接证据。

## v22：保持运行时输入的诊断控制

状态：HOST_VERIFIED（打包、输入身份和 preflight）；设备采集 SKIP，未发送镜像。

假设：天气数据路径被共享服务依赖阻塞；控制为 `pp-weather-v18`；唯一实验变量为诊断配置（选定服务日志 + `locationd` 一次 1 秒线程栈采样），不改变 UI 动作与运行时代码。

- 内核 `4b9871bb004de04210f1893c895f36c86aeb495ae543d60308be249698034522`；loader `1f51ed2b52bee5c42ff6cbd6ec115c4fb7e6e28d67ce96206f97f96c7fb2b8e2`，与控制逐字节一致。
- image `f2c61388c9fd3250da19edcff26583672db7ec6743361762544a8a031b222110`；ADT `c761eb3282c3968c482c7ebfbeb3704bf7d203c22571778b08173b4b6a3f8a7b`，8 个 `cluster-type`。
- `applog_name=launchd,SpringBoard,Weather,weatherd,locationd,tccd,networkd,trustd,nsurlsessiond`；`gs=1000 gs_proc=locationd gs_all=2 gs_sum=1 gs_n=1`。
- 保持 `tsa=1118 pong_touch_synth=200 pong_shot=150 pong_shot_n=1 pong_shot_step=6`。命令行 979 字节。
- 返回：`pong_autoreset=300 pong_autoreset_sb=240`，保留真实 panic 与长按复位；后部 USB-A，临时 fastboot boot，不刷分区。主机监督器有期限、15 秒心跳与日志，结束核对 Android 完成。
- 验收：新鲜日志及有效页面、定位等待栈或明确缺失原因、天气/权限服务实际事件、Android 返回分别判定。无日志不记 PASS。若 CS4K 在用户态前失败，保留负证据，不将其当作权限或 GPS 结果。
- 回退：此轮只改变临时启动参数；OS370 和全部既有回退文件保持。详细 manifest、preflight 与监督信息保留仓库外。

### v22 结果与当前阻塞

21:45 完整 preflight PASS：实际 OS 全窗口 hash 与 OS370 一致，UI/helper、完整备份清单、root/klog 连续 extent 与不重叠、live manifest、温度及可控后部 USB-A 均通过。这里只重新检查备份存在性和既有错误清单，没有重新校验全部备份内容。

21:46 开始主机监督，Android 端成功派发 `adb reboot bootloader`，但 100 秒 USB attach 窗口内没有 fastboot 枚举，cycle 退出 2 / `NO_FASTBOOT`。因此：

| 项目 | 结果 |
| --- | --- |
| 打包与 preflight | PASS，仅主机与 Android 前置 |
| 临时镜像发送 / XNU 启动 | SKIP，未执行 |
| 新天气页面 / locationd 等待栈 / 权限事件 | SKIP，无新设备证据 |
| cycle / Android 返回 | FAIL / 未确认；退出后主机看不到 adb 或 fastboot |
| 常规 3 秒 USB VBUS 恢复 | attach 内一次、退出后两次均未恢复枚举 |
| 延长 12 秒 VBUS 恢复 + 25 秒观察 | FAIL，仍未枚举；未再派发镜像 |

失败保留原运行名与监督记录，不覆盖、不把 USB 失败记为内核、权限或定位失败。停止所有设备操作后，下一动作是重新插拔已验证后部 USB-A 路径；恢复时先确认 fastboot product，返回 Android 并等待 `boot_completed=1`，新一轮重新 preflight/arm，使用新运行名与同一个不可变候选。不要从未 arm 的 fastboot 直接启动候选。

## 对用户问题的当前回答与下一次验收

当前证据不足以在“整个系统授权弹窗失效”与“定位服务阻塞”间定案。`tccd` 运行仅证明服务启动；定位授权还依赖 CoreLocation/locationd 与 SpringBoard 的提示交互，不能仅凭 tccd spawn 认定授权正常。也不能用没有 GPS 修复来解释一切：手机有 GNSS 硬件，但当前设备层的 `drivers/`、`configs/`、`dts/` 未检出 GPS/GNSS 适配接口，功能清单定位仍为待确认；固定城市天气应独立于 GPS 取得数据。

后续按可区分的证据推进：

1. v22 栈若显示 locationd 的同步调用卡在 fairplayd 或其他服务，先处理该实际依赖并做同场景回归，不绕过授权或制造定位结果。
2. 若 locationd 正常，下一轮采 Weather 自身等待栈，并用已有侧栏按钮确认保存城市/当前位置和可见权限状态；此时再决定是否需要添加城市或触发原生定位授权。
3. 应用确实请求数据后，单独检查天气 XPC、DNS/TLS 与 Apple 服务返回。没有捕获到网络请求的日志不能替代这项验收。
4. 权限弹窗需有实际 request → 提示/错误 → 用户选择/状态变化证据；GPS 需有真实驱动与位置更新证据。这两项不互相替代。

没有修改或安装运行时代码。未修复天气、未验证 GPS 或全系统权限弹窗。此次交付为纠正错误前提、固定诊断候选，以及记录阻塞后的安全接续动作。知识图没有本仓库索引，结构检查已回退到限定源码范围读取与搜索；未从无索引推断代码不存在。
