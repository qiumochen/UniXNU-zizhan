# 原机音频后端与无声时钟前置

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。[HALO无声运行](2026-11-15-amp-silent-run.md)已验证第一颗核心可精确启停，但32次实际采样无心跳，PLL原值10选择外部SCLK。缺少已建立的外部串行时钟是下一待核前置，不能据此直接断言根因或猜改内部PLL。当前队列见[STATUS](STATUS.md)。

## 原机只读输入

已只读留存8份本机XML，逐份远端/本机SHA匹配，原文及manifest留仓外；ALSA声卡、PCM后端和tinymix只读清单另保留。普通shell无法读ALSA时用已有root只读重取，未改变权限或控制值。tinymix部分DSP数组在闲置状态读取失败，失败原样保留，不能补零当实际运行状态。

本机ALSA卡为waipio-mtp-snd-card，PCM31是TDM-LPAIF-RX-TERTIARY multicodec，PCM32是其VIRT-2，PCM33为TX反馈。resource manager给扬声器VIRT-0声明48000Hz/2声道/24bit，而backend group另有96000Hz/3通道/24bit、slot_mask7。两层参数可能分别描述逻辑流与物理分组，当前不能选择其一当作播放中的实际格式。

本机static overlay的T/B ASPRX1 slot分别0/1，ASPRX2分别2/4，ASPTX1分别0/1；动态DSP源当前只读清单还存在声道差异，不能套用Pong TFA配置。保存的live DT声明GPIO121时钟、123帧同步、122data0、124data1及mi2s2功能，仍只是声明，不是实际引脚或BCLK资格。

## 下一活动播放采样

`tools/zizhan/mac/cs35l41-active-route.py` 只读三组PCM31/32播放及33反馈的hw_params/status，3次采样间隔3秒，每条adb15秒/输入长度界限。仅实际状态RUNNING且格式/通道/采样率字段完整才记运行格式；closed、PREPARED、缺字段或非法值均不能通过。脚本不启动播放、不写mixer，不把PCM状态当作物理BCLK/时隙测量，也不认证iPadOS声音。

```sh
python3 tools/zizhan/mac/cs35l41-active-route.py \
  --adb "$ADB" --output "$PRIVATE_NEW_ACTIVE_REPORT"
```

要求新仓外结果路径，保留失败/无活动结果。解析器已检查ALSA分数形式rate、真实RUNNING、PREPARED/closed/非法/缺字段负控制，脚本Python编译通过。原机参数输入和脚本准备完成后，待用户正常打开Android播放器、开始播放并保持前台，再采这次活动格式；不用重复询问声音是否正常，已有用户现场双扬声器基线。

之后才根据实际后端/分组与物理slot/clock契约实现默认关闭、AMP/BST/GLOBAL仍禁止的nativeADSP时钟资格。当前无新驱动改动或新kernel构建，沿用已验证mode19源码收据；不启动同一无心跳轮、不改OS/Data、不提交或推送。首次iPadOS输出前仍停下叫用户。

## 用户播放期间实际采样

用户启动正常Android播放，报告音量两格。三次采样PCM32协商一致为RW_INTERLEAVED/S24_LE/96000Hz/3通道，状态均PREPARED、指针0；PCM31/33均closed。原严格RUNNING采样结果保持`NO_RUNNING_ANDROID_TDM_PCM_OBSERVED`，不修改为RUNNING。owner为AGM服务。

同一播放期间AudioFlinger实际speaker输出非standby、48000Hz双声道；两颗HALO_STATE实际2，上下两颗心跳分别从0x48611到0x48728、0x486c2到0x48791，均推进。只认Android播放服务和原机DSP进展以及已协商TDM分组格式，不宣称Linux PCM RUNNING、物理BCLK或槽宽已测。debugfs clock/asoc路径不存在，未挂载或改权限。

固定Linux v6.6公开q6afe/q6prm定义确认tertiary TDM时钟ID204及PRM请求/释放协议；本机machine模块调用digital codec资源投票，实际PRM模块请求wire结构与已有编码相符。模块/公开源码hash保留仓外。物理slotwidth/BCLK/ADSP TDM endpoint配置仍需准确输入；下一先默认关闭APM readiness和digital core资源请求/释放资格，不设频率、不改pin/codec/graph或播放。

## 原机模块时钟请求推导

原机machine模块TDM路径读取`qcom,tdm-max-slots`到平台状态，初始化只保留1–4，超出范围会强制为4；因此saved live DT里的8不能直接参与乘法。实际hw_params路径按采样率×有效槽数×32计算，tertiary时钟表项204，attribute4/root0。结合本次协商96k，源码请求频率为12288000Hz。早期未跟进clamp时的24576000推导已明确拒绝，未发出任何频率请求。此处是原机请求规则重建，不是物理BCLK测量；下一个有界时钟请求应使用有效4槽及完整sourceguard。
