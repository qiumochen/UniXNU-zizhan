# 11-08/09 累计内容发布

## 发布范围

用户要求 commit/push，并核对整体交接是否还有工作树内容未进入 main。权威设备仓库入口为 `power02-panelpool-128`，HEAD `fbf5b1b39c1784f0d8df7f8dd1a9dbd582ddfb39`；fetch 后 origin/main 仍为 `087416a4eb083032b2a4fbb43617f76a11d41c36`。入口无未提交或未跟踪文件、无stash、只有一个worktree；28个提交共46个文件尚未进入main。

本批包括128MiB面板池规划、显示owner记账和默认关闭的DSI/ADSP观察与运输控制、CPU退出调用契约、截图采样/裁剪与round-report工具、可选pong_open入口，以及日期证据与天气交接。设备资格以各轮记录为准：panel off/真睡眠没有完成，pong_open设备成功未验证、当前已安装UI不含该入口，天气仍未修好。

只向 `qiumochen/UniXNU-zizhan` 的 origin/main 正常快进推送，不强推、不推上游。其他checkout/本机镜像、凭据、原始DT、日志和本地操作脚本不混入此仓库；整体本机清单在仓库外交接。本次发布不启动手机、不安装OS/UI或固件。

## 推送前验证

- 本机重新构建 `pong_ipados18_ci_defconfig` 和 `zizhan_ipados18_gpu_defconfig` 均 `KERNEL_DONE`；最后构建为完整默认profile。相对累计运行时代码没有新增内核改动。
- 精简内核SHA256：`8099a3f97d407b2eec7eb47c9bfe6fa62585b2658e603e8b9552559221542ea4`；完整默认：`d15a48e82bde9217dbf1cdddcf6c97eb8739ed1456b9a35f8d94112656e6f090`。构建成功不等于设备资格。
- 日用和显示主机回归通过。Wi-Fi首次使用脚本旧默认shared路径失败（输入不存在），显式指定本机已准备的Ath12kWifi共享源后回归全部通过，原负记录保留。
- 在仓库外隔离目录重新编译并4KiB签名UI helper通过，canonical UI hash仍为 `c3897b10cc22c1be310d3f121b080731052c819be56979a236d76a4ce2fb4388`；没有安装它。
- 对全部累计新增文本作个人路径、设备唯一标识、常见凭据赋值扫描，无命中；发布完整diff已审查。
- 0021补丁包含unified-diff上下文行的空格/制表符；默认`git diff --check`会对补丁文本告警，不能删除上下文空格破坏patch。禁用blank-at-eol/space-before-tab这两类检查后累计diff通过，实际patch应用回放与owner主机回归通过；普通新文档diff检查保持默认。
- 累计porting增量检查首次指出configuration、firmware、input区域未更新证据；本发布提交补齐，重新按固定`e67a79eb64012e8f30ba04c56ea37753bdb720a2`检查结构和相对推送前main的完整增量。不是用等于HEAD的base得到空比较。

## 云端CI与发布收口

推送前尚无本次SHA的CI结果。只跟踪该SHA实际触发的Porting record、Daily-use、Display和Wi-Fi主机回归；macOS内核工作流为手动，不把未运行项写为PASS，不继承其他SHA的绿色状态。推送和CI完成结果在下方追加；最终机器操作状态留本机交接。

### 首次推送结果与测试修正

`32add9f87305b37dac0a4ff81fd52d966aebc709` 已正常推送main，Porting record（42544541744）、Display（42544542000）、Wi-Fi（42544541742）SUCCESS；Daily-use（42544541994）FAIL。失败位于测试 `dsi-command.cpp`：GCC将 `condition ? DmaBusy : 0` 的枚举/非枚举混用作为`-Wextra -Werror`拒绝，本机Clang未报该告警。

修正只把mock读回值显式写为 `uint32_t(DmaBusy)` 和 `0u`，保持寄存器故障回放语义及所有运行时代码。重新运行完整日用host并跟踪修正SHA；首次失败保留，不重跑相同失败输入掩盖问题。此修正不需重建未改内核，不代表设备功能变化。
