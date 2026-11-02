# Photos 零尺寸断言修复（2026-11-03）

## 现象

g250、g255 自动点击照片图标后，Photos 能显示启动页，但约 1.5–3.7 秒后退出：

```text
Invalid parameter not satisfying: !PXPixelSizeAreaIsZero(roundedSize)
```

`pong_astc=0` 与 `pong_astc=1` 均复现，因此不是 ASTC 布局修复引入的回归。反汇编 `PhotosUIFoundation` 后确认，`PXCreateContext` 与 `PXCreateContextWithAutomaticFormat` 会把宽高取整；任一轴小于 1 时触发这条断言。

## 运行库加载

g257 把 `Photos` 加入 `kern.exec_inject` 白名单，g259 的内核探针确认环境变量已构造成功：

```text
name=Photos argv0=/Applications/Photos.app/Photos
DYLD_INSERT_LIBRARIES=/pong/libpong-ui18.dylib
```

但 g260 仍没有执行 UI 适配库。g261 给 Photos 二进制增加直接的 `LC_LOAD_DYLIB /pong/libpong-ui18.dylib` 后，适配库正常启动：

```text
Pong UI18: Photos adapter loaded
```

g261 首次安装钩子时发现 `dlsym()` 名写成了符号表形式 `_PXCreateContext`。运行库 API 需要 C 名 `PXCreateContext`；修正后 g262 同时安装两个入口。

## 兼容处理

`userland/ui_compat18.c` 现在只对 `Photos` 进程安装两个 context 入口钩子。包装函数保留所有整数参数，并调用原函数；进入 `PXCreateContext` 或 `PXCreateContextWithAutomaticFormat` 前，只把取整后小于 1 的宽或高收敛为 1。正常尺寸原样传递，其他 App 和系统进程不安装这两个钩子。

g262 捕获到触发点：

```text
PXCreateContext size=0.000x0.000 clamped to 1.000x1.000
caller=/System/Library/PrivateFrameworks/Tungsten.framework/Tungsten
```

此后进程不再以该断言退出。日志仍有 `IOSurfaceSharedEventAddEventListener failed: e00002c7`；它是独立能力缺口，Photos 在该轮没有因此崩溃。

## 可重复部署

- `tools/zizhan/mac/add-load-command.py` 用 `ipsw macho patch add` 增加 `LC_LOAD_DYLIB`，保存原 entitlement 后重新签名，并验证 load command 与签名。
- `tools/zizhan/mac/ui-refresh.sh` 在更新 UI 适配库时同步为 Photos 添加直接依赖，因此后续 UI 刷新不会丢失该修复。
- `PONG_UI_EXTRA_PROCESSES` 也包含 Photos，作为进程级注入入口；实测仅设置环境变量不足以加载适配库，直接 load command 是实际生效路径。

工具先在原始 Photos 副本上完成新增、签名和重复执行检查；第二次执行会识别依赖已存在并跳过。

## 设备验证

| 轮次 | 配置 | 结果 |
|---|---|---|
| g255 | ASTC=0，修复前 | 同一零尺寸断言，反复退出 |
| g260 | ASTC=0，环境注入 | 环境变量已写入，但适配库未执行 |
| g261 | ASTC=0，直接依赖 | 适配库执行；钩子因 `dlsym` 名称失败 |
| g262 | ASTC=0，修正钩子 | 两个入口安装；Tungsten 的 `0x0` 收敛为 `1x1`；不再出现断言或退出 |
| g263 | ASTC=1，诊断内核 | 同上；无 Photos 崩溃 |
| g264 | ASTC=1，最终双配置内核 | Photos 成功启动并保持运行；6 张截图完整；无零尺寸断言、无 `SIGABRT`/退出记录；自动返回 Android |

g264 的采样截图见 `docs/evidence/2026-11-03-photos-context.png`。截图为每 16 像素取一个点，只用于确认 Apps 画面和进程状态，不能评价文字细节。

## 构建检查

- `pong_ipados18_ci_defconfig`：成功，日志为本地 `$ZIZHAN_SRC/ci_kernel-photos-final-ci.log`。
- `zizhan_ipados18_gpu_defconfig`：成功，日志为本地 `$ZIZHAN_SRC/ci_kernel-photos-final-gpu.log` 和 `ci_kernel-photos-final-gpu2.log`。
- UI 适配库和静态 init 构建、4096-byte ad-hoc 签名通过；`add-load-command.py` 在原始和已修改副本上分别通过新增与幂等检查。

## 后续

首次进入 Photos 仍有需要人工轻触的提示/权限界面；后续相册、搜索和编辑路径需要在人工验收时继续观察。SharedEvent 监听仍未实现。
