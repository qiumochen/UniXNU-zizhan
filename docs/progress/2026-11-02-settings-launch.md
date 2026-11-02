# 设置首次启动白屏（2026-11-02）

用户报告：开机后第一次进设置，白屏要等很久。

## 1. 计时

`settings_compat18.c` 在 `pong_url` 轮次里打印启动时间线：构造函数、主队列第一次运行、第一屏有单元格，都是相对 exec 的毫秒数，加上开机 uptime。探针在主队列上每 100 ms 轮询一次，主线程忙时探针跟着推迟，和屏幕一样。

| 轮次 | 打开时机 | 主队列第一次运行 = 第一屏 |
|---|---|---|
| g235 | `pong_url=15`，开机后台服务还在起 | 6885 ms |
| g236 | `pong_url=60`，系统已空闲 | 6287 ms |
| g237 | `pong_url=40`，同时采样 | 6436 ms |

开机负载只占约 0.6 s；主线程 6 秒多没回到 run loop。

## 2. 主线程在等什么

g237：`gs=1000 gs_proc=Preferences gs_all=1 pong_touch_synth=2`，落在启动期的两个 1 秒采样窗口里，主线程全部在等：

```
-[ISDefaults init]                       (IconServices)
  MGCopyAnswer("UniqueDeviceID")
    libMobileGestalt UDID 计算 → MGCopyAnswer(WifiAddress)
      dispatch_semaphore_wait
```

libMobileGestalt 按 `IODeviceTree:/product` 的 `udid-version` 选算法（属性缺失时为 -1）：

- 1（旧算法）：SHA1(SerialNumber + ECID + WifiAddress + BluetoothAddress)；
- 2：ChipID-ECID，取 `/chosen` 的 `chip-id`、`unique-chip-id`。

Pong 的 ADT 写的是 1。我们 Wi-Fi 关着，WifiAddress 的回答一直等不到。键名由 base64(MD5("MGCopyAnswer" + 名字)) 的前 22 个字符反查得到（`VasUgeSzVyHdB27g2XpN0g` SerialNumber、`gI6iODv8MZuiP0IA+efJCw` WifiAddress、`k5lVWbXuiZHLA17KGiVUAA` BluetoothAddress、`AJFQheZDyUbvI6RmBMT9Cg` HasBaseband）。

## 3. 修复

`make-pong-adt-ipados18.py`：有 `--unique-chip-id/--chip-id` 种子时（zizhan 一直传）`udid-version` 写 2，这是 iPhone17,1 自己的版本；没有种子时保持 1。

g238（参数同 g237）：**第一屏 1219 ms**（原来 6436 ms）。帧率、崩溃情况和 g237 相同；界面正常。

## 4. 剩下的 1.2 s

没有再细分。g222 的采样里能看到中文断行用的 CoreNLP/MeCab 分词、objc 方法查找、文件打开、SwiftUI 建图，都是普通冷启动开销。
