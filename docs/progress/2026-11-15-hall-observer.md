# GPIO62/Hall 首批 iPadOS 只读观察器

源码基点：`034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交工作树。当前顺序以 [STATUS](STATUS.md) 为准；本页记录这一批交付，不另建任务队列。

## 交付与输入

新增默认关闭的 `zizhan_hall_observe=1` 探针。它使用由 ADT 传入并严格匹配的 zizhan 输入，等待 root 最多 120 秒，随后以 5 ms 间隔观察最多 120 秒/24000 次。输出原始电平、active-low assertion、15 ms 去抖基线和变化。它不生成铰链角度，不发布 HID/传感器服务，不切屏，不配置电源、pinmux、GPIO 方向或中断。

本机保存的 live FDT 中，`/soc/gpio_keys/hall_key` 的 `gpios` 引用 `/soc/pinctrl@f000000`（`qcom,cape-pinctrl`），pin=62、flags=1；`linux,input-type=5`、`linux,code=0`、debounce=15 ms。`/soc` 的 1/1 cells、根 2/2 cells 和 ranges 核对了地址恒等变换。GPIO controller 声明覆盖 `0x0f000000+0x01000000`；现有维护中的 `0002-zizhan-fts.patch` 已使用其中的 `0x200000` 窗口，并定义 GPIO 输入寄存器为 `base+0x100000+pin*0x1000+4` 的 bit0。

新观察器仅映射 GPIO62 的一页 `0x0f13e000+0x1000`，使用 uncached/read-only mapping。每次输入读取前检查控制寄存器的 function=GPIO、output-enable=0；不满足即 `SKIP`。读到断言不能证明机械合盖，静止电平也不能证明折叠事件通过。未知实际电平与姿态对应关系留待独立设备采样。

`IOInterruptController` 只提供中断分发接口，不是电平接口；这不构成只读采样必须移植 Apple GPIO driver 的依赖。首版沿用本机已有 TLMM 输入布局，保持不注册/确认中断。后续事件驱动和唤醒必须另行实现资源所有权及 IRQ 链。

## 主机验证

- `hall-state.py`：生产去抖状态机在 ASan/UBSan 下回放两向翻转、初始抖动、重复时间、倒退时间和 uint64 高时间戳。
- `hall-contract.py`：严格资源、缺字段/错误字段拒绝，并运行实际 ADT encoder 后重新解析 GPIO62 metadata。
- `hall-probe.py`：实际 probe 代码在模拟 DT/时钟/MMIO 下运行 13 项；只移除主机不支持的 ARM barrier。覆盖关闭零访问、错误模式、外机身份、缺失/错误宽度/错误 pin/base、root 超时、map 失败、GPIO 输出/mux 不符、静止基线和两次翻转。核验 120 秒/24000 次上限、只读 map 和释放。
- 含新用例的完整日用host runner通过；porting-record结构16 areas、相对文档链接和`git diff --check`通过。原有重复area ID和不受支持的状态值已合并/修正，验证记录明确区分基点SHA与未提交改动。
- 平台 archive 使用当前 iOS kernel SDK 编译通过；精简与完整默认 GPU/Wi-Fi 内核串行构建均 `KERNEL_DONE`。最终完整内核 44468544 bytes，SHA-256 `9277cfe63190bca13d0f8e1f01ac6b938fd88c5b7b6870a46011f51ffbabedc1`；精简内核 17847952 bytes，SHA-256 `a3d4ecf0ac4f3b58559c43f579c1ff69fc83485928e92421fd9adf215831f901`。原始报告与编译日志留本机。

首次独立 platform 编译因未加载本机构建树环境而进入历史默认路径，在编译前失败；第二次实际 SDK 编译发现 `IODelete` 需要指针左值，修正后通过。首次 kernel make 被沙箱的 CPU/内存 `sysctl` 限制阻止；经权限审查后相同双配置构建通过。原始失败日志保留，不把环境失败当硬件失败。

## 设备边界

本批没有打包/发送启动镜像、安装 OS、写 Data 或启动设备轮次；手机当前运行状态未在本批重新采样。原始已安装候选与 OS370/旧 Data/回退链保持原交接记录，不因源码编译变更而更新。

未来设备轮次需新鲜实际输入/唯一 owner、完整身份和回退核验，并保留 pinned klog、panic/自动返回与 Android witness。GPIO assertion 日志、机械开合、铰链角度、显示路由和原生系统消费分别验收；缺一项时不能升级整条折叠能力。

## 新两小时窗口中的静态实机候选（09:09）

当前已通过的最新双kernel仍包含原Hall观察器且相关source hash未变。已准备独立Hall包，不启用SE15/功放探针；仅增加Hall显式开关。目标是实际GPIO输入/基线/120秒有界终态，未启动前保持PREPARED_ONLY。fresh全OS/extent/manifest/backup/单owner/后部USB-A/自动返回及Android完成仍需重新满足；当前无人开合，静态PASS不等于折叠事件、铰链角度或显示路由通过。

## 静态只读设备资格（09:14收口）

新两小时窗口内完成独立Hall轮：实际GPIO62 active-low/15ms输入前置通过，原始值1/high/asserted0，去抖基线建立，120秒观察完成21693次读取、0次变化，readonly1/fold_qualified0，明确PASS observer。当前无机械姿态见证，不把asserted0命名为“已展开”，不推断角度/开合事件、HID或显示路由。

09:10:40临时启动，返回与1637661字节新鲜deadline klog/ADSP13/真实电池更新/无fatal或音频/Android完成均通过。09:15:15独立只读观察Android/zizhan/boot_completed1、36.0°C，温度只记录。fresh wholeOS/extent/manifest/backup和source31hash匹配，原只读代码与最新双kernel相同，未改runtime所以未重复构建。独立审阅记为PASS_SCOPED_DEVICE_HALL_READONLY_BASELINE，旧“未上机”段落保留为首批历史。

下一折叠增量可把实际去抖二值状态接入默认关闭的系统状态接口，但机械两次开合/姿态关系仍需人在场；首次外屏、触控和自动切屏分别验证，保持默认关闭。本轮无OS/Data安装或commit/push。
