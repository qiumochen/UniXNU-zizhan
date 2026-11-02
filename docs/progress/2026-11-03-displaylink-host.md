# display-link WIP 主机核对（2026-11-03）

## 核对结果

2026-11-02 的 GPU 交接把 `ui_compat18.c` 的 display-link 拦截标成“未构建”。本机现有 `prepared/libpong-ui.dylib` 已含 `pong_dl_full=0` 和 display-link 日志字串；只读挂载当前准备好的 OS 镜像后，`/pong/libpong-ui18.dylib` 与该准备目录文件逐字节一致，也含相同字串，挂载已正常卸载。这说明当前**本机准备镜像**有这段 hook；手机已安装的 OS 版本仍须设备日志核对。

用保存的 `ui-compile-command.json` 编译当前 `ui_compat18.c`、`settings_compat18.c` 等源文件，输出到本机独立的 `out/ui-displaylink-host.dylib`，未覆盖准备镜像；arm64 Mach-O 编译成功，临时文件的 ad hoc 签名和 `codesign --verify` 通过。Metal bridge 从当前源码编到独立目录，再用它构建 plugin bundle，两个 arm64 Mach-O 产物均成功。产物和原始构建日志只留本机。

约定的 `pong_ipados18_ci_defconfig`、`zizhan_ipados18_gpu_defconfig` 均构建成功，本机日志分别是 `$ZIZHAN_SRC/ci_kernel-ui-preflight-ci.log`、`ci_kernel-ui-preflight-gpu.log`，均以 `KERNEL_DONE` 结束。UI、bridge、plugin 的主机构建不能证明回弹动画帧率，也不能证明当前手机系统已加载该 UI 库。

## 部署脚本检查

`ui-refresh.sh` 的 Photos 加载命令原本引用未定义的 `SRC`，在 `set -u` 下会在改写 OS 镜像后退出。现定义工作根目录并在动准备镜像前校验标签、备份冲突和 Android adb；挂载期间的复制失败会停止且执行卸载清理。主机脚本语法、非法标签、已有标签和无设备提前拒绝均检查通过；无设备的检查未生成新备份。脚本实际改写 OS 与设备安装仍未运行。

## 上机接续

先完成 g269 Wi-Fi 的临时启动和自动取证。USB 恢复后检查当前已安装 UI 库的启动日志是否出现 `Pong UI18: display links: made ...`；若没有，再按备份约定部署本机准备好的 UI 库。使用同一镜像和内核分别带 `pong_dl_full=0` 与默认值，保留自动返回，采集触摸后的逐帧时间线、截图与 SpringBoard 日志，再决定是否启用该拦截。动画手感需在得到具体轮次/镜像后列入待人工清单。
