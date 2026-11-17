# CS35L41 HALO 无声执行与停止候选

源码基点 `034a0b0037b28f4ff4e46703cbd4d9bb75db25e5` 加未提交修改。用户已确认Android上下扬声器都有声且正常，继续[iPadOS保护参数读回](2026-11-15-amp-protection-coefficients.md)之后的无声执行。当前任务AUDIO-RUN-01的最小验收是两颗保护程序实际心跳推进、功放输出保持关闭、核心停止与最终复位；播放路由和首次声音另验。队列见[STATUS](STATUS.md)。

## 实际原机输入

已按原机模块manifest核对hash，从符号 `cs35l41_fs_errata_patch` 解析16项速率设置，均为DSP RX/TX寄存器值1。原机 `wm_halo_configure_mpu` 是23项写序列、`lock_regions=0xffffffff`；核心start只更新CCM bit0，和通用源码同时操作reset位的方式不同。本候选采用原机方式，没有复制原机重试或无关初始化。

原机保护WMFW的v3算法metadata解析确认：global算法400a4、XMbase1f4，`HALO_STATE`/`HALO_HEARTBEAT` offset0/1；cd算法XMbase94，`CSPL_STATE` offset2。均是4字节、XM24、只读/volatile控制。因此真实观察地址为028007d0/028007d4/02800258；不以mailbox默认0或core-enable ACK冒充执行证据。私有文件、module/disassembly与提取收据留仓外，公开只记录寄存器事实。

## 默认关闭模式17

只有同轮完整初始化、WMFW RAM、两颗实际算法身份和分声道保护参数全部通过才运行。先核GENI配置和PWR1/PWR2/HALO禁止状态；16项速率写逐一读回、20个MPU访问寄存器逐一读回（23项包含2解锁及1关闭），然后只置核心bit0，精确读回。读取PLL/DSP时钟原值，不改这些配置；不写GLOBAL/AMP/BST/ASP、音量或输出控制。

每颗最多32次采样、间隔10ms，读真实HALO状态/心跳、CSPL/mailbox、MPU违例与watchdog，同时核PWR1/PWR2及core状态。至少两次心跳变化且HALO状态非零才认本轮执行推进；状态数值保留原样，不将其推断为完整播放/保护调参资格。违例/watchdog非零、禁止状态变化、无心跳或超时均失败。成功后清core bit0并精确读回；确定总线时失败也有限停止，传输不确定则不派后续清理，由平台最终共享reset收容。

固定最多82次值写、2秒整体界限、逐事务20/2/2ms界限和固定循环上限。每次运行最终复位，设置和RAM均不作为安装状态。旧模式1–16保持；首次iPadOS扬声器输出前仍停下叫用户。

## 验证与实机要求

```sh
python3 tools/zizhan/tests/cs35l41-run.py
python3 tools/zizhan/tests/cs35l41-run.py "$PRIVATE_WMFW" "$PRIVATE_TOP" "$PRIVATE_BOTTOM"
python3 tools/zizhan/tests/cs35l41-setup.py
python3 tools/zizhan/mac/check-daily-use-host.py
```

实际GENI模型执行完整链，合成与原机输入分别验证；覆盖无心跳有界停止、错误前置、MPU读回不符、fault、禁止状态变化、整体/冻结时钟超时，以及本阶段每个真实事务位置失败后不再派清理。平台35项控制保持原模式，mode17复位失败也正确终结。完整日用及reduced/full kernel按实际收口后才备包。

设备前置保持fresh wholeOS/extent/manifest/完整备份/单owner/后部USB-A、temporary boot、自动/panic/power-key返回、pinned deadline和Android完成见证、30分钟监督。分别判定初始化/参数与执行；只有两颗心跳变化、core启停读回、禁止输出/finalreset和返回全部满足才认此限定功能。无心跳时保留PLL/DSPCLK/状态原值，下一动作必须针对具体前置，不重复相同启动轮。

## 首轮实机MPU读回失败

mode17真实16项速率写/读回通过，接着MPU解锁两写和第一项XM访问写均ACK；该访问项完整读回与ffffffff不符，reason14/stage3/19attempt=ACK，core未启动。候选未记录observed值，不能推定为位宽掩码。PLL原值10/DSPCLK3、禁止PWR1=0/PWR2=3300/HALO100保持，最终reset/新鲜deadline1392196字节/ADSP13/电池/无fatal/Android完成通过，31.2°C。下一独立mode18仅23MPU写和20项完整原始读数，不启动core；先确认硬件读回再制定确切mask，旧strictmode17FAIL保留。

## mode18实际读数与独立经验前置

两颗完整20项读数完全一致：3、1、00ff0000、000fffff、0、0、0、0、f，其余为0。78次写均ACK，core未启动，禁止状态保持/最终reset。200秒返回、1379159字节新鲜deadline/ADSP13/无fatal/Android完成通过，33.4°C。此结果只证明真实读数，不能把它直接命名为权限掩码。

下一独立mode19把此完整向量作为明确经验前置：每项精确相等，配置key关闭后再采全部20项也必须相同，未知差异失败。日志明确`mpu_rights_verified=0`；没有忽略未知位，旧mode17完整FFFFFFFF要求及失败结果保留。只有此限定配置receipt通过才继续无声心跳/核心停止观察，真实心跳仍是执行资格的独立标准。

## mode19精确前置通过、心跳失败

chip0完整经验MPU向量及关闭后再次20项精确匹配，41次写ACK；核心100→101→100精确启停。但32次真实采样HALO/heartbeat均0，reason4/stage5，813542微秒后有限停止/finalreset，第二颗未尝试。PLL10/DSPCLK3原值、VIO/watchdog0，功放输出保持关闭。此结果只认精确配置receipt与核心启停，不能算程序执行。

PLL10选择外部SCLK，而当前尚未建立nativeADSP播放时钟。下一先核原机实际backend/slots/width/BCLK与本机引脚，准备输出关闭的外部时钟资格；不能把时钟相关性当因果或直接猜改内部PLL。旧失败不重跑。
