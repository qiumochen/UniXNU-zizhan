# 2026-11-28 下午（主机侧实现与独立审查闭环）：外屏锁屏时钟/`1:1` 竖屏锁与电源键联动（`OUTER-04`/`OUTER-05`）、三摄 `90°/270°` 原生 Bayer 解马赛克降噪与色彩矩阵（`CAM-04`）及 M3 菜单更新

记录时间：`2026-11-28 15:00 CST`（本轮手机未接电脑，全部在主机侧完成源码修复、ASan/UBSan 单测扩展、双配置内核与用户态编译、独立上下文对抗性审查修复及离线启动包生成；**未执行任何设备刷写或实机运行，待接回手机后做增量镜像更新与在场实测**）。

---

## 1. 修复范围与设计要点

### 1.1 `OUTER-04`：合盖必先进外屏锁屏时钟、解锁默认进 `1:1` 满高桌面、合盖强制锁定竖屏
- **合盖先进锁屏时钟 + 解锁进 `1:1` 满高视网膜桌面**（`drivers/ArmDTPlatform/ZizhanOuterProbe.h`、`drivers/ArmDTPlatform/ZizhanHallProbe.h`）：
  - 在日常档（`zizhan_outer_fold=1` 与 `zizhan_outer_fold=3`）下统一设置 `prefer_full_1to1 = 1` 且 `direct_desktop_on_fold = false`（仅保留 `zizhan_outer_touch_synth=1` 或显式 `zizhan_outer_direct=1` 直进桌面供自动化自检）。
  - 每次物理合盖（包括开机时霍尔已处于合盖态的 `Event::Baseline`）均先进入外屏锁屏时钟（`active_view = 1`，默认 `12 s` 亮屏窗口），单指上滑或双击唤醒后默认进入 **`1:1` 满高桌面（`1080×1916 @ y=220..2136`，`active_view = 3`）** 并保留底部五胶囊栏（`VOL- / HOME / ZOOM / CLOCK / VOL+`）一键热切 `1:1 ↔ 2:1`。
- **合盖强制竖屏锁定与展开自动恢复**（`drivers/PongPower/ZizhanSlpi.cpp`、`drivers/PongPower/ZizhanCamStream.h`、`userland/zizhan-scene-orient18.h`、`userland/ui_compat18.c`）：
  - 外屏 `DSI1`（`1080×2520`）为原生竖向扫描，与顺时针旋转 `90°` 的内屏 `DSI0`（`2160×1916`，竖握为 `wori = 4`）相差 `90°`：
    1. 内核 `ZizhanSlpi.cpp` 在 `zizhan_outer_lid_closed() != 0` 期间将上报给 HID 的重力向量固定夹紧为 `(0, +9807, 0)` mm/s²（对应 iOS `-1.0g Y` 轴标准竖屏 `wori = 1`），屏蔽合盖期间的重力自动旋转；
    2. `ZizhanCamStream.h` 在只读 `ZIZHAN_CAM_CMD_QUERY` 的 bit 6 暴露实时 `lid_closed` 状态，`SpringBoard` 主队列 `cover_orient_lock_watch` 通过 `zizhan_cover_orient_lock_step` 在合盖时调用 `SBOrientationLockManager lock:1`，展开瞬间自动恢复用户原有的自由 4 向重力旋转（`unlock`）或手动方向锁（`lock:1..4`）。

### 1.2 `OUTER-05`：合盖外屏物理电源键亮灭屏与 `<1 W` 深睡闭环
- **XNU 补丁 `0048` 电源键钩子与 3 秒救砖红线守护**（`patches/xnu-ipados18/0048-zizhan-cover-power-key.patch`）：
  - 在 `bsd/dev/pong_buttons.c` 的 `look()` 中新增 `zizhan_power_button_hook(down)`，置于 `k->down = down;` 之后；同时将深睡唤醒吞键标志 `swallow_power` 移入 `look()` 内部仅抑制单次唤醒的 HID/外屏翻转事件而**不篡改物理 `keys[0].down`**，确保包括“从 `<1 W` 深度睡眠按住电源键不放满 `3000 ms`”在内的任何场景下 `pong_button_reset_hook()` 均 100% 正常触发回 `fastboot`。
- **外屏 `DSI1` 亮灭屏状态机与内屏防误亮守卫**（`ZizhanOuterProbe.h`、`ZizhanSleepPlan.h`、`ZizhanSleep.h`）：
  - 合盖亮屏态（时钟 `1` 或桌面 `2/3`）短按电源键 → `blank_cover("power-key-off")` 立即将 `active_view` 置 `0`、下电外屏 `DSI1`（`dbv=0` + DCS `0x28/0x10`），并通过 `ensure_inner_asleep_while_folded()` 确保内屏落入 `state == 0`（任何情况下只要 `lid_closed == 1` 且 `disp != 0` 均维持 `cover_mute(1)`，杜绝合盖内屏发光），随后由 `ZizhanSleep` 进入 `<1 W` 平台深睡。
  - 合盖灭屏态（`active_view == 0` 或 `<1 W` 深睡中按电源键唤醒）→ 先经 `ensure_dsi1_ready()` 校验 MDSS/DSI1 时钟并点亮 `DSI1`，再绘制外屏锁屏时钟（`active_view = 1`，`12 s` 窗口，期间 `zizhan_cover_autosleep_blocked` 暂停自动重睡，且 `ZizhanSleep` 不拉起内屏 `lightDisplayAfterWake`）；灭屏与深睡期间 FTM5 外屏触控自动排空 FIFO 不触发口袋误唤醒。

### 1.3 `CAM-04`：传感器 `90°/270°` 物理贴装旋转、原生坐标 Bayer 解马赛克、降噪与色彩校正
- **原生传感器偶对齐 `2×2` Bayer 解马赛克与旋转解耦**（`userland/zizhan-cam-proto.h`、`drivers/PongPower/ZizhanCamStream.h`）：
  - 移除此前破坏 Bayer 奇偶行相位的 `flipY = 1`；先在传感器原生偶对齐 `(qx, qy)` 坐标提取 `(R, Gr, Gb, B)` 四元组并完成 `2×2` 邻域抗锯齿与单坏点（Hot-quad）剔除，再按实机 DT 贴装角执行等比例中心裁切与象限旋转（后置 `IMX766`/`OV13B10` `rotCw = 90°`，前置 `IMX596` `rotCw = 270°` + 自拍水平镜像 `mirrorX = 1`），输出保持 `960×720` BGRA 零拉伸变形。
  - 新增 64 桶直方图 `98%` 分位白点（修复单颗亮点压暗整幅画面）、随模拟增益缩放的暗部噪声门限（Noise Coring）、暗部伪彩抑制、行和恒为 `256` 的中性灰守恒 Q8 `3×3` sRGB CCM 矩阵、线性暗趾 Gamma 2.0 曲线，以及帧间运动自适应时域 IIR 降噪（`zizhan_cam_temporal_denoise_bgra`，静态像素 75% 历史滤波，二维码/运动边缘零拖影；AF 扫焦后自动清零历史帧）。
- **预览图层四向旋转矩阵与非全屏子区域 `frame` 保持**（`userland/zizhan-avcapture18.h`）：
  - 结合实机 `SpringBoard` 根窗口 `saf` 矩阵（`wori=4:[0,-1,1,0]`、`wori=3:[0,1,-1,0]`、`wori=1:[1,0,0,1]`、`wori=2:[-1,0,0,-1]`），证明并实现 `zizhan_cam_preview_quarter_turns_cw`（合盖 `wori=1 -> 0`，展开 `wori=4 -> 0`、`wori=3 -> 2`、`wori=1 -> 3 (-90°)`、`wori=2 -> 1 (+90°)`）。
  - 在 `PongZizhanPreviewLayer18` 上拦截 `setFrame:` 保存客户端期望子区域 `client_rect` 并幂等合成旋转 `bounds`/`position`/`affineTransform`，同时在预览泵与拍照输出增加 `hdr->sensor_pos == ctx->position` 校验消除前后切镜瞬间的跨镜头残留帧。

### 1.4 M3 启动菜单标题更新（`scripts/build-ipados18-bootimg-local.py`）
- 当 `--identity` 为 `ipad*` 时，将 UniXNU/DarwinBoot 启动菜单项标题从 `"Pong iOS 18 - iPhone 16 Pro"` 更新为 **`"iPadOS 18 (zizhan)"`**。

---

## 2. 独立上下文代码审查与闭环记录

在首轮实现与双配置内核编译通过后，拉起独立只读子代理对全量 `git diff` 与 XNU 补丁链做了对抗性审查，共检出并当场闭环修复以下问题：
1. **`CRITICAL-1`（深睡长按 3 秒强制复位失效）**：原 `0046` 在 `keys_thread` 中于按键松开前将 `pdown` 置 `false` 会阻塞 `keys[0].down` 置位；已在 `0048` 中改为在 `look()` 内更新 `k->down = down` 后再按 `swallow_power` 吞掉唤醒边沿，并在 `tools/zizhan/tests/zizhan-sleep.c` 增加连续按住 `>=3000 ms` 的回归断言。
2. **`CRITICAL-2`（展开内屏 `wori=1/2` 预览旋转反了 `180°`）**：修正 `zizhan_cam_preview_quarter_turns_cw` 为 `wori=1 -> 3`、`wori=2 -> 1`，并在 `tools/zizhan/tests/avcapture18.c` 增加全部四向 `saf * R(turns)` 复合矩阵恒等断言。
3. **`HIGH-1` & `HIGH-2` & `MEDIUM-1`（合盖内屏静音竞态、`DSI1` 唤醒时钟序与合盖冷启动基线）**：收口为 `ensure_inner_asleep_while_folded()` 与 `draw_after_power()`，确保任何合盖非桌面态先置 `cover_mute(1)`、先唤醒 `DSI1` 后读 `INTF_2` MISR、清零跨睡眠 `cover_power_swallowed_down`、合盖唤醒不拉起内屏 `lightDisplayAfterWake`，并在 `ZizhanHallProbe` 处理合盖冷启动 `Baseline`。
4. **`HIGH-3` & `MEDIUM-2` & `MEDIUM-3` & `LOW-1`**：补齐 `PongZizhanPreviewLayer18` `setFrame:` 子区域保持、`zizhan_cam_ctl_query` 只读单次 `sysctl` 查询、前后切镜 `sensor_pos` 过滤、AF 后重置 `temporalValid`，以及新增 C/C++ 单行 `if` 花括号规范。

---

## 3. 主机侧验证结果与离线待测产物（手机未连接）

- **主机回归套件**：
  - `python3 tools/zizhan/tests/xnu-patch-format.py`：`6/6 OK`（含 `0048` 正反向 `git apply` 往返校验）。
  - `python3 tools/zizhan/mac/check-daily-use-host.py`：全套 ASan/UBSan 与 Python 回归 `PASS`。
  - `python3 tools/zizhan/mac/check-display-host.py`：全项 `PASS`。
  - `python3 tools/zizhan/mac/check-wifi-host.py --shared <sheng>/drivers/Ath12kWifi`：`11/11 PASS`。
- **双配置内核与用户态编译（`JOBS=4`）**：
  - `DEFCONFIG=pong_ipados18_ci_defconfig`：编译链接 `PASS`。
  - `DEFCONFIG=zizhan_ipados18_gpu_defconfig`（`build-play206`）：编译链接 `PASS`，内核 SHA-256 为 `8c401674031f530d1ac98ac23c181a826fb413d61a7cda12fa6bd3a385408a9b`。
  - `libpong-ui18.dylib`（`-Wall -Wextra -Werror` + 4K 页签名）：编译 `PASS`，SHA-256 为 `2e792d459da0d11e4e1bbd2844fd8ffdf24d10460576c6d0dc31cc3ba499ffff`；`build-apps.sh` 三款应用编译 `PASS`。
- **离线打包与 M3 双启动镜像校验（零设备接触）**：
  - 静默无人值守镜像 `out/daily-1129`（命令行 `992 B < 1024 B`，菜单标题 `"iPadOS 18 (zizhan)"`）：`51926823163ecc92786e4da09cab0b7b379ca838c508f08e54bd6531fd9bbdfb`。
  - 在场 10 分钟验收镜像 `out/video-8`（命令行 `992 B < 1024 B`）：`6a2b2aa0d0031be3631b00c1007e79f936f867e95c1bc9a24ac6983fb2961b58`。
  - M3 独立双启动包 `work/m3-standalone-1129/pong-xnu-aprime.img`（默认项 `Android`，第二项 `"iPadOS 18 (zizhan)"`）：SHA-256 `66dca0358229b0a24a2e2a2929febecc489c48e7fa9b64727bcbc303b5a1bfc3`，通过 `check-m3-standalone.py` 全项与原厂 `boot_b.img.gz` 备份校验（`M3_STANDALONE_PREFLIGHT: PASS, backup_verified: true`）。
- **待手机接回后的下一步操作**：
  1. 将手机通过后置 USB-A 接入 Mac（Android 态），运行 `ZIZHAN_USER_PRESENT=yes bash work/m1-firstsound/run-video-8.sh`（自动执行 `os-trial-edit.py sens-hal-v27-64g --ui --activate` 增量写入 UI 适配器并整盘 SHA-256 回读校验，随后 `fastboot boot` 启动 `out/video-8` 实测 `OUTER-04`、`OUTER-05`、`CAM-04`）。
  2. 在场运行 `ZIZHAN_USER_PRESENT=yes bash work/m3-standalone-1129/run-m3-ram-test.sh` 验证 M3 双启动菜单与直跳 Android/iPadOS。
