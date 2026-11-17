# RPMh 完整分组发送接口

源码基点为 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。本批完成上一批SLPI请求核心所需的有界整组发送接口；新增API尚无开机调用者，未启动SLPI或发送设备请求。当前台账见[STATUS](STATUS.md)，前置计划见[SLPI资源lease](2026-11-15-slpi-power-lease.md)。

## 实现

`ZizhanRpmhBatch.h` 复用设备既有APSS DRV2映射与MMIO barrier，只配置active TCS4。调用者持有原`powerLock`，同一次触发发送1至16条命令，实际数量仍受硬件容量限制。无效地址/重复地址/非法数量在MMIO前拒绝，版本/容量/TCS4忙/与GPU TCS3的地址冲突在写入前拒绝。sleep/wake TCS、GPU事务和GIC handler不由该核心改动。

每条命令配置response，wait/enable使用完整payload mask，只触发一次。先清旧完成位并核对清除，随后要求所有命令状态完成、TCS空闲，以及该请求期间观察到busy或清除后出现的新完成位；旧的命令status即使看起来完成，也不能单独构成PASS。清除、响应、收尾分别有10/100/10ms期限，同时有迭代上限，避免异常时钟引起无限循环。

开始写寄存器即把owner置为uncertain，只有完整完成及收尾读回后清除。若失败，保留payload/状态，直到reset前拒绝后续batch及原单条请求；迟到的完成不能自行解除这个限制。正常开机未调用新API时，原单条路径仍保持原实现。

含Wi-Fi内核的`PongWifiPlatform.cpp`与无Wi-Fi内核的`PongRpmh.cpp`都暴露`pong_rpmh_batch`，复用各自唯一公共锁及映射，没有新建第二个RSC owner。该接口接受已构造的完整payload，不自行解析cmd-db，也不保证未知baseline或caller构造的VCD正确。SLPI lease与它之间的baseline交接/同锁绑定仍未接入。

## 验证

`python3 tools/zizhan/tests/slpi-rpmh-batch.py` 对实际生产template及两个实际owner API源码运行ASan/UBSan，模拟MMIO仅用于控制，不作硬件成功。覆盖1/3/16命令、全部地址/值、单次trigger、capacity/argument/other-TCS冲突、旧完成/部分完成/永忙、清除/收尾失败、冻结时钟上限、迟到完成禁重试、映射失败、锁和legacy路径的uncertain拒绝。新增测试进入日用runner；公共Wi-Fi平台变化也触发日用host检查。

日用host和Wi-Fi host均通过。Wi-Fi第一次因未显式指定共享源码而使用旧默认目录、在进入dispatcher测试前失败；已保留原日志，显式指定实际`--shared`后整套通过。这是测试环境路径修正，未修改共享源码。

精简 `pong_ipados18_ci_defconfig` 与完整 `zizhan_ipados18_gpu_defconfig` 内核均通过，源码hash与本批receipt一致。精简kernel SHA-256为 `9b5fbec0f535bbe994694071be01d37d810d2872f048440ac15b6dfc62404749`，完整为 `04d0d5b3fe97ba0d8bb4e83f086a456e94a2f2cd22671b00cae78cd39ed6cbf1`。未派设备试验，不把源码回放/编译提升为硬件ACK资格。

## 剩余前置

SLPI仍缺本机完整cmd-db与可信的开机固件保留APSS资源票交接；idle TCS不能推断远端AOP票为零。整组发送接口本身不解决这项缺口，不能直接把它接上默认零baseline启动PAS12。此具体前置保持未解时，下一独立增量转向SE15/CS35L41只读chip-ID候选，避免继续重复已通过的资源核心或ADSP/IPCC轮次。

本批无候选打包、固件安装、手机命令或Data修改。源码仍dirty main、无commit/push/云端CI；原始日志/receipt/设备输入留仓外，现有OS/UI/helper及全部回退链保留。
