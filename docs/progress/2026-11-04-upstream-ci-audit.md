# 上游规范与 CI 核对（2026-11-04）

## 范围和依据

本轮核对已发布的 `b87344288ed1adcac4539f9a1ac8f57a40ef7bc7`，并补齐记录检查。没有新增硬件轮次，没有改变构建用 UniXNU、sheng、固件或 Apple 输入。

当前上游源码固定为 [`e67a79eb64012e8f30ba04c56ea37753bdb720a2`](https://github.com/UniXNU/UniXNU/tree/e67a79eb64012e8f30ba04c56ea37753bdb720a2)。依据为其 [设备仓库清单](https://github.com/UniXNU/UniXNU/blob/e67a79eb64012e8f30ba04c56ea37753bdb720a2/docs/zh/porting/device-repository.md)、[记录规则](https://github.com/UniXNU/UniXNU/blob/e67a79eb64012e8f30ba04c56ea37753bdb720a2/docs/zh/porting/records.md)、[打包接口](https://github.com/UniXNU/UniXNU/blob/e67a79eb64012e8f30ba04c56ea37753bdb720a2/docs/zh/porting/packaging.md)及检查器实现。

## 已确认和本轮修正

- 仓库公开，topic 含 `unixnu`；board schema 2 / record schema 1，name、SoC、OS profile 一致；保留 PongPlatform24 组件身份和四层选择。
- 上游 `devices.toml` 已收录本仓库，`record_state=available`，当时固定在 `fe4bff38928dce4b4c2e03a9bf0f4f3dca2cc7fd`。这是旧快照，不代表新推送已同步。目录按 UTC 00:00 / 12:00（北京时间 08:00 / 20:00）扫描；本轮未修改上游仓库或手动触发其工作流。
- 原有 Porting record CI 只查结构，没有 `--base` 增量检查。对本次 28 个提交使用上次发布版本 `08888642356f21afc9361c8e8ff5cd7b11c96f13` 复核，发现 `userland/ui_compat18.c` 的 input 区域没有同步更新；补充实际 UIKit 类名修正和合成输入的验证边界。
- 检查器升级到上述当前上游固定版本。push 使用 event.before，PR 使用 base.sha；已有基准对象缺失时尝试获取，不能静默退化为空比较。首次推送的全零基准只检查结构。
- 结构化记录改为简洁英文，保留已有证据的 kind、40 字符 revision、reference，以及所有已有区域的 status、components、paths。中文详细日志继续留在原引用中。同步修正 README 旧的 Wi-Fi 未接入和 GPU 54 fps 摘要。
- 文档现要求单列蜂窝网络，当前检查器的必需集合还只有 13 项。本机有基带，新增 `cellular=planned`，不能填 `not-applicable`；没有启用或假造蜂窝实现。
- 增加 `Wi-Fi host regressions`，固定现有 Ath12kWifi 版本，在 Ubuntu/clang 回放已有 SMMU、WMI、DHCP、USB 失败场景。此 job 不读取设备、固件或凭据，不是完整驱动构建或硬件测试。

## 验证命令

在独立上游检查器检出中，将设备 `migration/format2/components.toml` 追加到检查器 `components.toml` 一次，然后运行：

```sh
python3 <checker>/scripts/porting-record.py check --device <device>
python3 <checker>/scripts/porting-record.py check --device <device> --base 08888642356f21afc9361c8e8ff5cd7b11c96f13
python3 <checker>/scripts/porting-record.py check --device <device> --base b87344288ed1adcac4539f9a1ac8f57a40ef7bc7
```

本地结构检查输出 `zizhan: 14 structured areas checked`。以 `0888864...` 为基准的增量检查通过；新增工作流提交后还会对最终已提交 HEAD 复核上述两个基准。`check-wifi-host.py --cc clang --shared <pinned Ath12kWifi>` 全部通过；YAML 解析与 `git diff --check` 通过。

`--base` 比较基准与已提交 HEAD 的代码路径，并读取当前记录；工作区有代码改动时，不能用未提交状态代替最终检查。主机和设备验证证据各自保留，不把记录翻译或 CI 通过当作新硬件功能。

原始推送的记录检查：[41816187641](https://github.com/qiumochen/UniXNU-zizhan/actions/runs/41816187641) 已通过；内核 CI：[41816187280](https://github.com/qiumochen/UniXNU-zizhan/actions/runs/41816187280)。本轮修订后还需在最终提交跟进记录、Wi-Fi 主机回归和 macOS 内核三项结果。

## 新增 Linux 回归发现的主机兼容问题

首轮 [Wi-Fi host regressions](https://github.com/qiumochen/UniXNU-zizhan/actions/runs/41817919354) 的 SMMU 用例通过，但 WMI 用例编译失败：glibc 的 `<errno.h>` 经 `<linux/errno.h>` 取常量时，被 `-I` 加入的 Darwin shim 同名包装遮住，`EAGAIN` 未定义。将测试脚本的本地头搜索改为 `-iquote`，只处理用引号引用的驱动/shim 头；系统尖括号头仍由系统提供。不改共享驱动、不硬编码 errno 常量、不跳过用例。修正后 macOS/clang 回归通过，Linux hosted 结果随新提交跟进。

## 尚未满足的最新打包要求

- [ ] 从上游 `templates/package.sh` 原样采用统一 `scripts/package.sh`。
- [ ] 用 TOML 补齐当前 Darwin24 的预链接、payload、启动镜像输入与构建配方；保留已有私有输入边界。现有 `pong_build` 与本地 `ci_*` 不能直接算作新 `unixnu release` 流程。
- [ ] 接入 `.github/workflows/package.yml`，对实际支持的 22H374 配置执行 `--check` 和完整本地打包，核对清单、哈希、命令行长度与产物。不只复制一个无法运行的模板来满足文件名。

因此可以确认目录已经收录、记录可校验；不能宣称当前上游仓库清单全部完成。默认 hosted macOS CI 关闭 Wi-Fi、Adreno 和背光，没有本地 Apple/固件输入时也不产出完整启动和 userdata 镜像。它的通过只覆盖其日志所示的简化配置。Wi-Fi Settings 入网、跨 AP DHCP、跨频段 RDDM、真实 MSI 仍按 network 区域列为未完成。
