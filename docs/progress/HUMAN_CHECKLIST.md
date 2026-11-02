# 待人工验证清单

## 2026-11-03：恢复手机 USB 连接以继续自动轮次

- 轮次/镜像：g265–g267 均未启动；待自动运行 g268 镜像 `$ZIZHAN_SRC/out/boot-g268/pong-xnu-initial.img`。
- 操作：在方便时确认手机有电并重新插接手机端 USB 线；停在 Android 或 fastboot 界面即可。此项只需恢复物理连接，无需观察 iPadOS 的 Wi-Fi 页面。
- 期望：主机重新枚举手机，`adb devices` 或 `fastboot devices` 能看到设备；后续 Wi-Fi 轮次由脚本自动启动、取日志并返回 Android。

## 2026-11-02：iPadOS 多任务与交互

- 镜像：`$ZIZHAN_SRC/out/boot-astc-manual/pong-xnu-initial.img`
- 操作：用 `cycle.sh` 临时启动；确认 SpringBoard 出现后打开两三个 App，执行多任务切换，再做一次打开和返回动画。
- 期望：多任务画面不黑、不残缺；App 缩略图与应用本体一致；打开/返回动画连续，触摸没有明显错位。
- 记录：记下问题发生的 App、操作和现象，并报告对应轮次名称。

## 2026-11-02：主屏与壁纸外观

- 镜像：同上。
- 操作：观察主屏壁纸、Dock、状态栏和横竖屏切换。
- 期望：壁纸内容稳定，没有黑块或残留；Dock 与状态栏位置正确。
- 记录：需要时拍一张照片，并注明发生在启动后多久。

## 2026-11-03：Photos 首次进入与浏览

- 镜像：`$ZIZHAN_SRC/out/boot-photos-final/pong-xnu-initial.img`
- 操作：打开 Photos，处理首次启动的提示或权限弹窗，然后进入“最近项目”并打开一张图片。
- 期望：Photos 不再立即退出；弹窗、网格和图片查看界面可以正常操作。
- 记录：若弹窗、相册、搜索或编辑仍异常，记录具体页面和是否出现崩溃。
