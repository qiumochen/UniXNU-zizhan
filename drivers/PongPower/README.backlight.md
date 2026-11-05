# zizhan 内屏亮度控制

本设备使用 L18 primary OLED，2160×1916、SM8475 DSI0（`0x0ae94000`、`0x400` 字节）、VC0。板级输入来自本机 Android live DT：DCS `0x51` 写入、`0x52` 回读，最大 2047、非零最小 4，`bl-inverted-dbv` 使实际写入为高字节、低字节。它与 Pong 的 NT37705 4095/10 不同。

`configs/zizhan-display-sm8475.json` 通过 version 2 的 `/chosen/pong-dsi-backlight` 传入实际参数。驱动严格校验板级参数；本机不接受旧 Pong 参数。未知面板或控制器、初始回读错误、资源失败时停止服务，保持原硬件状态。

异步 IOWorkLoop 用实际 DSI FIFO 完成事务，不分配假 DMA 缓冲，不复位像素流水线或 PLL。首次有效回读后才发布服务及 `IODisplayParameters`。每次写入再读取面板，匹配后才更新可见状态；不从请求值伪造完成。渐变有界，最新请求可以替换尚未处理的目标。

刷新率命令和亮度共用 `PongDsiLease.h` 的原子 owner lease，亮度持有到异步完成和寄存器恢复。DMA 超时但仍忙时保留所有权并停止依赖事务，不复用或复位未完成 FIFO。主机并发回放验证错误 owner、重复进入和两类使用者的互斥。

CoreBrightness 使用真实 AppleARMBacklight metaclass 的 conformance，保留唯一的本地类注册名，类未就绪时有界等待。原生属性允许 `brightness`、16.16 秒的 `FadePeriod` 及可选 `backlight-level` 数字；后者只记录为电流表元数据，实际 DCS 只写 `brightness`。没有本机校准的 CompensationTweak 字典仍返回不支持。

22H374 的 CoreBrightness 在空默认映射初始化时发生实际崩溃，运行时适配在**真实本机背光服务已回读发布、精确映像 UUID/入口及范围匹配**时，暂用该同一映像自带的软件默认曲线；映射存在后返回原函数。没有伪造光度校准或抑制其他崩溃。

## 验证入口

- `pong_backlight=0` 保留固件亮度，不启动服务。
- `pong_backlight_probe=1` 仅实际读初始值，写入次数为零，不发布服务。
- `pong_backlight_probe=2` 做非零档位与原值恢复，每步真实读回，不发布服务。
- `pong_backlight_probe=3` 用于原生 UI 资格轮，拒绝零目标，避免无人值守测试熄屏；正常模式 0 保留系统熄屏行为。
- `pong_backlight_diag=1` 有界记录请求、匹配回读和失败；`pong_brightness_ui_test=1` 只操作 DISPLAY 设置页唯一可见的原生滑条，日志动作本身不作为通过。

已验证：初始值 307、非零 64/512/恢复 307；原生设置滑条 25%/65%/50% 分别得到真实硬件 512/1331/1024。HID 拖动、刷新率和最终默认配置状态见 [最新设备记录](../../docs/progress/2026-11-05-display-and-backlight.md)。截图不提供物理光度证据。

## 一手参考

- 本机 live DT 的 `qcom,mdss_dsi_l18_38_0c_0a_prim_dsc_cmd`；原始设备导出留本机。
- [Xiaomi 显示驱动](https://github.com/LineageOS/android_kernel_xiaomi_sm8450-modules/tree/3b30ba7df6608c97c0ba2d55d679a6b5987c4c66/qcom/opensource/display-drivers/msm/dsi)：DBV 编码、FIFO、BTA 和 readback。
- [Pong services85](https://github.com/Chengdong-Yu/UniXNU-pong/blob/e0ee6df109b4b5dc706cac660d4f8210dc911dac/migration/ipados18/services85-backlight-startup.md)：相同 22H374 CoreBrightness 初始化故障；只参考 ABI/时序，不复用面板参数。
- [PongPower](https://github.com/Chengdong-Yu/pong-power/blob/c37179ed12ba7f981b2b088aea482b969085eb84/PongBacklight.cpp)：原生亮度属性契约。
