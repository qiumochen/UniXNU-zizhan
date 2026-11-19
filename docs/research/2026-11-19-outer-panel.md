# 外屏（L18 secondary）点亮资料与方案（2026-11-19）

来源：本机保存的 Android live DT（`work/hardware-20261115/audio-dma-live-dt-v1.dts`，仓库外）。只读整理，未上电、未写寄存器。

## 两块屏

| | 内屏 | 外屏 |
| --- | --- | --- |
| DT 面板名 | `xiaomi l18 38 0c 0a prim cmd mode dsc dsi panel` | `xiaomi l18 38 0c 0a sec cmd mode dsc dsi panel` |
| 分辨率 | 2160×1916 | 1080×2520 |
| 物理尺寸（DT, 0.1 mm） | — | 65.7 × 153.2 mm |
| DSI | DSI0 / PHY0，`pll_byte_clk0`/`pll_dsi_clk0` | **DSI1 / PHY1**，`pll_byte_clk1`/`pll_dsi_clk1` |
| 模式 | 命令模式 + DSC | 命令模式 + DSC（8 bpp，8 bpc，slice 540×40，每包 2 slice） |
| 刷新 | 60 / 120 Hz | 60 / 120 Hz |
| 复位 | GPIO0 | **GPIO125**，序列 `<0 1 1 10>`（低 1 ms，高 10 ms） |
| ESD 中断 | GPIO116 | GPIO60 |
| TE | TE pin 1 | TE pin 1 |
| 背光 | DCS | DCS（`bl_ctrl_dcs`，0x04–0xFFF，初值 0x133） |

两块屏同一家族（38 0c 0a，DCS 序列以 `F0 5A 5A` 解锁），内屏现有的 DSI 命令/DSC 代码可以复用，外屏主要是换控制器实例和参数。

## 外屏供电（`dsi_panel_pwr_supply_sec_L18`）

| DT 名 | 电压 | 实际来源 |
| --- | --- | --- |
| `secvddio` | 1.8 V | WL2866D LDO `wl2866d-dvdd1`（QUP SE1 I2C `0x984000`，地址 0x28） |
| `secdvddd` | 1.8 V | `display_panel_vddd`：固定稳压器，**GPIO93** 高有效，`regulator-boot-on` |
| `secvci` | 3.0 V | WL2866D LDO `wl2866d-avdd1` |

时序 / porch（60 Hz 模式）：h 前后肩 32/32、h 同步 32；v 前肩 48、后肩 32、同步 24；面板时钟 0x31975000（≈832 MHz）；PHY timing `00 1d 07 07 17 22 07 07 08 02 04 00 19 0c`；lane map 0123，burst mode，RGB888。

## 点亮步骤（每一步都要有人在场看出光，首次失败可物理恢复）

1. **只读**：映射 DSI1 控制器前先确认它的时钟/GDSC 是开着的，否则读寄存器会总线错误死机。先读 GCC/DISP_CC 中 DSI1 相关时钟的使能位，和 TLMM GPIO93/125/60 的当前配置，确认 UEFI 是否已经给外屏上过电。
2. **供电**：WL2866D 打开 dvdd1(1.8 V) → GPIO93 拉高（VDDD）→ avdd1(3.0 V)，按 DT 顺序和 post-on-sleep。
3. **复位**：GPIO125 按 `<0 1 1 10>`。
4. **DSI1/PHY1**：沿用内屏 DSI0 的初始化代码，换基址、PLL1 和上述 PHY timing。
5. **面板命令**：DT 里 sec 节点的 on-command（LP 模式），之后 DSC PPS，`11 00`/`29 00`。
6. **静态测试图**：先从一块固定 buffer 推一帧，看到出光后再接 GPU 扫描输出和 TE。
7. **触控**：SE19 外屏触控单独做，确认方向和边缘。
8. **折叠切换**：最后再和 Hall（GPIO62，`zizhan_hall_observe=2` 已能让内屏跟随开合熄屏/亮屏）接起来。

## 现在的状态

- 没有外屏驱动；只有内屏（DSI0）。
- 第 1 步只读检查需要先确认地址都在已开时钟的模块里，否则同样有死机风险，所以也放在有人在场时做。

## 2026-11-19 下午：外屏已经在出帧，写内存就能显示（只读盘点 + 闭环验证）

- self-1/outer-1（只读）：DISP_CC 的 DSI1 byte/pclk/esc 分支全开，DSI1 控制器版本 0x20060000，PHY 0x25；外屏复位 GPIO125、VDDD GPIO93 已为高；外屏 TE（GPIO87）约 250 次/秒跳变。DPU：**CTL1 → INTF_2（DSI1）由 VIG2 从 0xb8fc9900 取 1080×2520、行距 4320 的画面**，格式与内屏相同；这块内存在启动程序保留的 splash 区（no-map 的 0xb8000000+0x2b00000）里，默认双缓冲模式不使用它。
- outer-2（`zizhan_outer_probe=2`）：打开 INTF_2 的 MISR（0xae37180/184），写入前签名恒为 0x0c3d2ca8；往 0xb8fc9900 写彩条后签名变为 0xbe2dceaf 并保持不变，INTF_2 帧计数 3706 且在走。**外屏显示我们写进去的画面，已经闭环确认，无需任何 DSI/MDP 初始化。**

下一步：合盖时把内容画到外屏（先做状态/时钟画面，再研究让 iPadOS 把外屏当成一块屏）；外屏触控（SE19）单独做。

## 外屏触控（资料，未操作）

ST FTS（`st,spi-sec`），SPI 在 **QUP wrap2 S4 / SE19（0x890000）**，IRQ GPIO119，复位 GPIO52，坐标 10800×25200；AVDD = WL2866D avdd2，VDD = WL2866D dvdd2（同一颗 WL2866D 已经在给外屏面板供电）。现有内屏触控驱动（`bsd/dev/pong_fts.c`，XNU patch 0002）把 QUP0 SE4 的 GCC 偏移和 RPMh 的 ldoc7/ldoc2 写死，接外屏需要先把这几项参数化（GCC wrap2 S4 时钟、WL2866D I2C 供电），再在 ADT 里加第二个触控节点。
