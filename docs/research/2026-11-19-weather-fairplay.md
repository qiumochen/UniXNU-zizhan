# 天气：定位已放行，剩下 FairPlay（2026-11-19）

## 本轮做了什么（全部静音、未写分区）

| 轮 | 操作 | 结果 |
| --- | --- | --- |
| wx-allow-1 | 打开天气，约 25 s 后模拟点击小组件定位弹窗左侧按钮（界面树里第一个按钮为“允许”） | 两张截图均无弹窗，页面“上海 --” |
| wx-check-1 | 只打开天气、不点击 | 打开后的截图里弹窗不再出现，说明询问已被回答 |
| tz-1 | 设置 → 通用 → 日期与时间，模拟点击“自动设置” | 界面日志 `SET_AUTOMATICALLY on=0 → on=1`；时区行仍在取值，结果待后续轮确认 |

定位总开关、天气“使用 App 时允许”在 11-14 已由正常 UI 打开；小组件定位询问今天已回答。**权限这一层已经没有阻塞。**

## 现在卡在哪里

- 天气 App 在 3 分 30 秒里向 launchd 查找 `com.apple.fairplayd` 和 `com.apple.fairplayd.versioned` 各 111 次（约每 2 秒一次），全部 `No such process`。除此之外天气和 weatherd 没有别的报错，主预报接口没有被调用（11-14 观察器结论相同）。
- 当前安装的系统沿用了 v23 起的改动：`fairplayd.A2` 启动项被拿掉，因为它在这台机器上起不来、却占着端口，曾经卡死 locationd。
- 11-14 的元数据调查：WeatherKit 第一方应用在进程内认证（JWT），第三方才走 authservice。天气 App 在发预报请求之前反复找 FairPlay，最可能是认证或授权前置在等它。

## 可选路线

1. **恢复 fairplayd 启动项再观察**：最小改动，但 v22 已经证明它在这台机器上无法正常运行（需要苹果硬件密钥），预计仍失败，且会重新卡住 locationd。不推荐。
2. **在天气数据服务层接第三方数据源（推荐）**：在已有的 UI 兼容库里拦截 `WeatherDataServiceProxy performRequestsWith:completion:`，用公开免密钥的天气接口（如 Open-Meteo）按经纬度取数，组装成天气 App 需要的结果对象。需要：反推结果对象结构（同版 dyld cache 元数据可得）、写兼容层、**重新安装 userland（写分区，需要你同意并在场）**。
3. **放弃系统天气**：用 Safari 打开天气网页作为替代。

## 时区

“自动设置”已重新打开（tz-1）。之后的 fold-d1 轮（约 10 分钟后、联网）系统日志时间仍是洛杉矶时间（主机 11-19 10:10 CST，手机 11-18 18:10），自动时区没有算出来。手动选“上海”需要在时区搜索框里输入文字，现有模拟键盘只支持输入网址，改它要重装 userland，留到有人在场时一并处理。

## 其他项目怎么处理 fairplayd（2026-11-19 晚，用户要求先调研、暂不接第三方数据源）

| 项目 | 平台 | fairplayd / FairPlay 的处理 | 天气或 App Store 的结果 |
| --- | --- | --- | --- |
| UniXNU-pong（本项目上游） | 同一台机器，iOS 16.5 | `userland/mkjobs.py`：fairplayd 能启动，但一启动就以 0 退出，KeepAlive 每 10 秒拉起（一次会话 22 次），因为内核没有 `com_apple_driver_FairPlayIOKitUserClient` | 上游写"天气和 App Store 空白从来不是它造成的"，App Store 卡在没有 storefront，网络之前就停了。他们那一版天气没走到认证这一步 |
| ChefKiss Inferno（QEMU 模拟 iPhone 11 / iOS 14） | 模拟器 | 没有解决；FAQ 说需要硬件的 "Legacy FairPlay Key Descrambler" AES 命令 | App Store 应用装上即崩 |
| Corellium（商业虚拟 iPhone） | 虚拟化 | 官方文档：不支持 FairPlay，也不提供解密方法 | App Store 应用不能运行 |
| pypush / Beeper Mini（iMessage） | 任意 | 用 Unicorn 模拟旧版 macOS 的混淆二进制，喂真实 Mac 的序列号；代码里直接带 FairPlay 私钥 | 能用了几天就被苹果封掉。这属于冒充真实设备并使用苹果私钥，本项目不走这条路 |

**结论**：目前没有任何项目在非苹果硬件上让 fairplayd 正常工作。能找到的"办法"要么是没解决，要么是冒用真实苹果设备的身份或密钥。

### 静态核对：天气确实要 FairPlay 签名

在 22H374 的 dyld cache（`ipsw dyld str`）里：

- `WeatherDaemon` 有 `WDSJWTAuthenticator`、`WDSJWTMescalSigner`、`httpHeaderMescalAuthenticationKey`，并链接 `AppleMediaServices`。
- `AppleMediaServices` 的 Mescal 实现基于 FairPlay（`AMSMescalFairPlay`、`FairPlay Mescal Failed`、`AMSFairPlayContext`）。
- `CoreFoundation` 里有 `com.apple.fairplayd.versioned` 等服务名，和天气 App 每 2 秒查一次 `com.apple.fairplayd` / `.versioned` 对得上。

所以链条是：天气要先向苹果换 JWT，换 JWT 的请求要带 Mescal 签名，Mescal 签名要 fairplayd，而 fairplayd 需要苹果芯片里的密钥。这和"一直找 fairplayd、从不发预报请求"的设备观察一致。上游的结论是在网络还没通的阶段得出的，和这里不矛盾。

**下一步**：天气保持挂起。用户已决定暂不接第三方数据源；如果以后要接，路线仍是"在天气数据服务层接公开接口"（见上文路线 2）。

