# ST7789 LCD 固件（2026-10-22）

适用硬件：烧录器本体 GD32F103CB，HS20S010B / ST7789V2，240×320。主程序使用 `GD32-HID/GD32-HID` 加密版本；标准 FLM 工程未修改。

## 烧录文件

| 文件 | 用途 / 地址 |
|---|---|
| `DAPlink-ST7789-full.hex` | 推荐首次迁移使用，包含 Bootloader 和主程序，HEX 自带地址 |
| `Bootloader-ST7789.hex` / `.bin` | Bootloader，BIN 起始地址 `0x08000000`，48 KB 分区 |
| `DAPlink-ST7789.hex` / `.bin` | 主程序，BIN 起始地址 `0x0800C000`，80 KB 分区 |

这些文件烧入烧录器本体，不是被烧录目标芯片的固件。原 OLED Bootloader 也必须更新；只更换主程序无法解决旧 Bootloader 等待 OLED 应答的问题。HEX 文件中的地址无需手动偏移。

原 Bootloader 的应用升级机制仍保留：SPI Flash 上的 `0:/update/DAPlink.bin` 必须使用本次的新主程序。若那里存有旧版本升级包，请替换为 `DAPlink-ST7789.bin`（设备内名称仍为 `DAPlink.bin`）或移除旧升级文件，避免后续升级覆盖新程序。不要把 Bootloader BIN 放到该应用升级路径。

## 接线及行为

| 信号 | GD32 引脚 |
|---|---|
| LCD SCL / SCK | PB3 |
| LCD SDA / MOSI | PB5 |
| LCD RES | PB15 |
| LCD DC | PB9 |
| LCD CS | PB8 |
| LCD BLK | PA10，高电平亮、低电平灭 |
| USB_EN | PA8 |
| 主程序 CDC_LED | PA9 |
| 目标 SWCLK / SWDIO / nRESET | PB13 / PB14 / PB12 |

按用户确认的飞线与 PB14/PB15 割线接法配置。Bootloader 不配置或驱动 PA9；它原来对 PB5 的 CDC 指示灯操作已停用，避免占用 LCD MOSI。

- LCD 和板载 Flash 都用硬件 SPI0。LCD 事务重映射到 PB3/PB5，事务结束后恢复 PA5/PA6/PA7；接线不需再改。LCD 使用 16 分频，Flash 保持 4 分频。当前 96 MHz 系统配置下分别为 6 MHz 和 24 MHz。
- 切换前片选均拉高、发送完成后才切换，保留软件 NSS 高电平。LCD 单向发送，不占用 PB4；PA15 也不配置为 SPI NSS。
- 因 USB 文件操作在中断里访问 Flash，总线事务保存、屏蔽并恢复中断状态；LCD 每次最多 128 像素，Flash 读取每次最多 256 字节。LCD 初始化的毫秒延时均在事务外。
- SWD 初始化、关闭及 `DAP_SWJ_Pins` 不再操作 PA10。PA8/PA9/PA10 已分配其他功能，因此关闭 JTAG 能力声明；SWD 功能保留。
- 保留现有 OLED 菜单、中文字模、状态和 1 KB 画面缓冲区，转成 LCD RGB565 显示。默认横屏 320×240，原界面放大 2 倍、居中为 256×128。没有分配整屏彩色缓冲区。
- 主程序与 Bootloader 的 `OLED_Init` / `OLED_Refresh` 都已转接 LCD；Keil 工程不再编译旧 `bsp_i2c.c`。屏幕缺席不会让初始化等待 I²C ACK。
- Bootloader 找不到升级文件时，有有效应用就启动应用，否则进入原有 USB 恢复模式；按住左键仍可进入恢复模式。

## 编译及源文件

两个原 Keil 工程均使用 ARMCC 5.06 update 7 完整重编译，结果为 **0 错误、0 警告**。

| 项目 | BIN 大小 | RAM（RW+ZI，包含配置的堆栈/堆空间） |
|---|---:|---:|
| 主程序 | 60,872 字节 | 20,064 / 20,480 字节 |
| Bootloader | 31,852 字节 | 16,512 / 20,480 字节 |

驱动源在 `GD32-HID/GD32-HID/USER/BSP/bsp_lcd.c/.h`，共用 SPI 驱动在同目录 `bsp_spi0_bus.c/.h`。Bootloader 的 Keil 工程直接引用这两份源文件，并定义 `LCD_BAREMETAL` 使用自身 `delay_ms`。原 `LCD/LCD.c` 留作移植参考；初始化参数沿用该驱动。

显示方向在 `bsp_lcd.h` 的 `LCD_DIRECTION` 中配置：1 为当前横屏，3 为横屏旋转 180°，0/2 为竖屏（原 UI 以 1 倍居中显示）。主程序与 Bootloader 共享此设置。Keil 的常规 HEX 输出仍位于各项目 `DAPlink/MDK-ARM/GD32F10X_OBJ/DAPlink.hex`。

已检查链接结果无 `i2c_init/i2c_write/i2c_dma_write`，BIN/HEX 数据一致、分区不重叠、向量表和校验和有效。`manifest.json` 记录本次 BIN 的 SHA-256。源文件修改前备份和构建日志在 `.planning/2026-10-22-st7789-hardware-spi/`；保留原文件各自的 UTF-8 或 GBK 编码。

## 上板验证

本次没有连接硬件运行或实际烧写设备；编译成功不能替代以下实测：

1. 冷启动：LCD 背光点亮、菜单和中文字形正常，无原 OLED 时也能进入主程序。
2. USB 传入并读回一个已知文件，比较内容，确认刷新屏幕时 Flash 文件传输正常。
3. 离线烧录一次已知可用的目标算法/程序，确认 PB14 SWDIO、烧录进度和结果；连接/断开 SWD 时背光不变。
4. 使用新的应用升级包走一次 Bootloader 升级，确认 LCD 进度显示；移除升级文件后，已有应用仍能启动。
