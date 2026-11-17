# Weather 与 Safari 接续调查（2026-11-14）

范围：22H374 / main 系统应用，记录本日晚间上游核对和下一步，不替代当前任务台账。

## 上游与兄弟项目

UniXNU-Pong 当前公开提交 `a6a8a7f8` 收录 services100–104 的逐轮修复记录。Weather 已知相关的是固定提交 `0ee4243a` 的 services188：旧 `locationd` 曾通过 FairPlay 同步等待；该记录并未证实 WeatherKit 预报成功。services100–104主要涉及 Wi-Fi 分配、UI、存储、音频和启动顺序，不能解释在真实 bag/map HTTP 200 后仍没有主预报请求。Pong 当前公开树只见输入/UI相关变更，未发现天气实现。

Sheng 固定HEAD `1108068` 的键盘提交 `b952733` 修复键盘报告缓冲与并发串行化，聚焦实体键盘报告生命周期，不是 iPadOS Safari 软键盘实现。可借鉴的是 HID service readiness、报告时序和并发核验；不可直接复制其他设备驱动。Tavily 搜索服务本轮返回 HTTP 554，研究改为 GitHub 固定提交 API、上游源码和本机 22H374 二进制交叉核实。

## Weather 证据与诊断

最新已完成设备轮 q 的 Weather 保持上海 `--`。真实 Wi-Fi 已关联，扫描和 DHCP 有证据，Wi-Fi DMA 失败计数为0；单轮网络observer记录 bag 与地图元数据HTTP 200，但未捕获主预报 URL。Weather与WeatherDaemon相关栈未见旧 locationd 同步等待。它们证明网络/权限前置已进一步缩小，不证明预报服务成功。

同版 WeatherDaemon 从 22H374 dyld shared cache 提取的 ObjC 元数据显示：

- `WeatherDataServiceProxy performRequestsWith:completion:`：主天气请求入口。
- `WeatherDataServiceProxy fetchAvailableDataSetsWith:completion:`：可用数据集入口。
- `WDSJWTAuthenticatorServiceListener generateTokenWithUrl:completion:`：token生成入口。
- 二进制包含 `com.apple.weatherkit.authservice` 和 `com.apple.weatherd.weatherkit` 服务名，并明确记录第三方通过 authservice认证、Weather第一方应用进程内认证。

所以缺少独立 authservice launchd job 不能作为根因。下一判断要分层看：Weather UI有没有调用主 WeatherDataService；WeatherDaemon有没有调用WeatherKit网络请求；认证是否完成/具体错误是什么。

新增默认关闭的 `userland/zizhan-weather-observer18.h`，只在 `zizhan_net_diag=1`时对上述 ObjC入口核对运行时 ABI，再观察原请求、回调结果和错误。观察预算每入口最多32次，类懒加载采用0/2/10/25秒有限探测；不主动发请求、不生成token、不记录token内容。回调签名需通过Apple Blocks descriptor中的真实类型核对才包装，未知签名原样走原实现。

Mac host ASan/UBSan 测试 `tools/zizhan/tests/weather-observer.py` 使用真实 ObjC/Blocks runtime，覆盖方法不匹配、一次安装、请求与结果/错误对象身份、同步/异步与nil回调、超预算原样转发。该测试依赖 Apple Foundation，只在 Mac host 单独运行，不加入 Ubuntu daily-use runner。通过不等于设备ABI已匹配；若真实二进制走其它Swift/CFNetwork入口，记录观察边界再扩大。

## Safari 输入负控

`app01-keyboard-v2` 已OS-only安装并通过整卷读回。新一轮慢速真实HID把 `https://example.org/` 写进Safari地址栏；截图与上层文本能看到输入，WKWebView仍是`https://example.com/`，页面没有导航。故事件发送成功不等于Safari接受Enter。地址输入未通过，视频/HTTPS/滚动既有有限资格不受影响。

下一次键盘实验应新增明确的第二次物理Enter及充足观察窗口，并记录按键 down/up、焦点文本、WKWebView URL和页面标题；不得用 URL API 打开网页冒充输入成功。若Enter仍未导航，调查虚拟键盘服务是否被 backboardd 打开、软键盘未出现时Safari地址栏的提交行为和HID usage，再改输入驱动。

## 证据边界

Safari 视频资格只覆盖公开LAN静音H.264样本、真实播放/暂停/继续和不同帧。天气当前没有温度/预报。主机观察器测试和代码编译不能升级设备资格；实际安装和设备结果只按私有 operation 收据与本地运行日志确认。

## u 轮结果与 v 单变量

u从 Android 正常临时启动/返回，fresh log `logs/app01-weather-1114u.txt`及两张完整记录保存在本机（其中第二张截图有缺列，整轮 screenshot integrity FAIL）。只读Weather UI accessibility值明确为“上海，第2个城市”；SpringBoard观察到原生模态“允许来自‘天气’的小组件使用你的位置？”及“不允许”按钮。主Weather入口、可用数据集入口、token生成入口全部ABI匹配，但无 forecast RPC调用；NSURLSession只见`weather-edge.apple.com/v1/configs`、Weather bag和元数据，不见WeatherKit天气请求。天气卡片仍`--`。这将阻塞点缩小到真实应用交互/主服务请求未发起阶段，但不能证明该系统小组件权限modal就是唯一原因，也不能据此诊断认证。

v轮保持已安装OS、UI、保存上海、应用定位授权、Wi-Fi和observer不变，只在按钮坐标56%,58%通过Pong真实HID发出一次点击。日志确认触点先于Weather打开，系统widget定位modal在之后仍然出现；这轮不是有效拒绝操作，也不能证明点按坐标和按钮映射。w轮保持触点坐标/observer/应用状态不变，只把Weather open延迟20秒改成1秒，使应用更早出现。该选项不撤回天气App“使用App时允许”，不改TCC数据库或位置数据。先用画面确认modal消失，再看observer是否有新forecast入口和真实响应；若未消失或仍无请求，按实测回看HID dispatch与窗口内容，不扩大推断。
