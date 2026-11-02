# zizhan 音频功放板级审查（2026-11-03）

## 输入与验证边界

以公开的 [zizhan Android 设备树固定版本](https://github.com/sm8450-mainline/fdt/blob/3e347601d9e91dd2f1f48e51499823225f7bc0bc/zizhan.dts) 核对音频节点，并与仓库 `PongAudio.cpp` 对照。该文件是从下游系统提取的公开副本；本机目前没有可供再次读取的手机 live DT，启用驱动前仍须与本机 live DT 核对。原始设备树副本留在本机，不入仓库。

脚本逐项断言控制器、引脚、功放地址、复位、IRQ 和声道前缀均存在于同一 I²C 节点，并核对 Pong 源码中的旧常量。结果全部通过；这只是来源和代码审查，没有执行 I²C MMIO，也没有启动音频或设备轮次。

| 项目 | 公开 zizhan DT | 当前 Pong 音频代码 |
| --- | --- | --- |
| 控制总线 | QUPV3 SE15 I²C，`0x880000`、`0x4000` | SE13，`0xa94000` |
| SDA/SCL | GPIO56/57，`qup15`、上拉 | GPIO48/49 |
| 功放 | 两颗 `cirrus,cs35l41`，地址 `0x40`、`0x42` | 两颗 TFA9873，地址 `0x34`、`0x35` |
| 复位/中断 | 两颗共用 GPIO1 复位；IRQ GPIO88、118 | Pong 复位使用 GPIO1、89 |
| 板级音频角色 | `sound-name-prefix` 分别为 `T`、`B`，其中 `T` 标记 right-channel-amp | Pong TFA profile |

## 实现顺序

`GeniI2c` 底层已有多字节收发入口，但 `PongAudio.cpp` 当前功放包装是 1 字节寄存器地址、2 字节值，并写入 TFA9873 专用寄存器。不能仅替换 I²C 地址或复用这些写入。先用本机 live DT 核对 SE15 的时钟、GPIO、复位极性与 IRQ，再在隔离配置里做只读芯片身份探针；前置条件失败时跳过 I²C。随后按 CS35L41 的真实寄存器协议和本机调音/固件输入实现初始化，最后核对 ADSP 音频路由、播放及静音/复位。每段均需临时 `fastboot boot`、自动返回和日志证据；真正的扬声器声音、失真和音量仍列为人工体验项。

默认 `pong_audio=0` 和现有 GPU 配置保持关闭。手机当前没有 USB 枚举，因此没有本轮音频设备结果或截图。
