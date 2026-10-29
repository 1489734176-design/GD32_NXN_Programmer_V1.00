/* 原生屏幕布局：应用默认竖屏240×320；Bootloader保持横屏；直接画字，不分配整屏显存。 */
#ifndef _BSP_LCD_H__
#define _BSP_LCD_H__

#include "gd32f10x.h"

/* HS20S010B / ST7789V2 液晶屏移植接口。
 * LCD 使用硬件 SPI0 重映射引脚 PB3(SCK)、PB5(MOSI)，仅发送数据。
 * 外部 Flash 使用 PA4/PA5/PA6/PA7，由 bsp_spi0_bus 统一切换与保护总线。
 * 应用和 Bootloader 共用本驱动；Bootloader 定义 LCD_BAREMETAL，采用裸机延时，
 * 不编译直接字符显示函数。本驱动不操作 PA9。
 */
/* LCD 控制引脚：SCL/SDA/RES/DC/CS 位于 GPIOB，BLK 位于 GPIOA。 */
#define USE_LCD 1
#define LCD_SCL_Pin GPIO_PIN_3 /* PB3：SPI0 时钟 */
#define LCD_SDA_Pin GPIO_PIN_5 /* PB5：SPI0 数据输出 */
#define LCD_RES_Pin GPIO_PIN_15 /* PB15：硬件复位，低电平有效 */
#define LCD_DC_Pin GPIO_PIN_9 /* PB9：0 为命令，1 为数据 */
#define LCD_CS_Pin GPIO_PIN_8 /* PB8：片选，低电平有效 */
#define LCD_BLK_Pin GPIO_PIN_10 /* PA10：背光控制 */
#define LCD_BLK_GPIO_Port GPIOA
#define LCD_BLK_ACTIVE_HIGH 1 /* 1：高电平点亮背光；0：低电平点亮 */

/* 显示方向：0 为竖屏，1 为横屏，2/3 分别为竖屏/横屏旋转 180 度。
 * 可在工程预定义 LCD_DIRECTION；应用默认竖屏240x320，Bootloader默认横屏320x240。
 */
#ifndef LCD_DIRECTION
#ifdef LCD_BAREMETAL
#define LCD_DIRECTION 1
#else
#define LCD_DIRECTION 0
#endif
#endif
/* 屏幕原生几何尺寸，随编译方向选择。 */
#if LCD_DIRECTION == 0 || LCD_DIRECTION == 2
#define LCD_WIDTH 240
#define LCD_HEIGHT 320
#else
#define LCD_WIDTH 320
#define LCD_HEIGHT 240
#endif
/* 常用颜色：RGB565 格式，红 5 位、绿 6 位、蓝 5 位。 */
#define LCD_BLACK 0x0000
#define LCD_WHITE 0xFFFF
#define LCD_RED 0xF800
#define LCD_GREEN 0x07E0
#define LCD_BLUE 0x001F
#define LCD_YELLOW 0xFFE0
#define LCD_CYAN 0x07FF
#define LCD_MAGENTA 0xF81F
#define LCD_GRAY 0x8410
#define LCD_CN_SIZE 32 /* 直接显示汉字时使用 32x32 点阵 */

/* 初始化共用SPI0、控制引脚与ST7789；清为黑屏后开启显示和背光。
 * 返回 - 无 */
void LCD_Init(void);
/* 单色点阵直接绘制，不建帧缓存，按整数倍率分块写入。
 * x            - 目标左上角列坐标（原生像素）
 * y            - 目标左上角行坐标（原生像素）
 * width        - 点阵宽度（像素，放大前）
 * height       - 点阵高度（像素，放大前）
 * stride       - 每行或每页的字节跨度
 * bitmap       - 点阵数据指针，为空时直接返回
 * column_pages - 1=列页式（ASCII/图标），0=行式（汉字）
 * scale        - 放大倍数1~4
 * mode         - 1=正显，0=反显
 * 返回         - 无 */
void LCD_BlitMono(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                  uint16_t stride, const uint8_t *bitmap, uint8_t column_pages,
                  uint8_t scale, uint8_t mode);
/* 彩色单色点阵：笔画使用color，背景为黑色；反显时交换笔画/背景。
 * 其余参数与LCD_BlitMono相同，旧接口继续使用白色，兼容Bootloader。 */
void LCD_BlitMonoColor(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                       uint16_t stride, const uint8_t *bitmap, uint8_t column_pages,
                       uint8_t scale, uint8_t mode, uint16_t color);
/* 1倍字模叠画：split_x为绝对填充右边界，左侧黑字彩底，右侧彩字黑底。
 * 只更新绝对横向区间[clip_x, clip_x+clip_width)，并自动裁剪屏幕边界。
 * 点阵格式与LCD_BlitMono相同；不申请临时像素数组，支持软件反显。 */
void LCD_BlitMonoSplit(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                      uint16_t stride, const uint8_t *bitmap, uint8_t column_pages,
                      uint16_t split_x, uint16_t color,
                      uint16_t clip_x, uint16_t clip_width);
/* 发送ST7789开显示命令并点亮背光。
 * 返回 - 无 */
void LCD_DisplayOn(void);
/* 先关闭背光，再发送ST7789关显示命令。
 * 返回 - 无 */
void LCD_DisplayOff(void);
/* 控制背光，电平极性由LCD_BLK_ACTIVE_HIGH指定。
 * on   - 非0=点亮背光，0=熄灭
 * 返回 - 无 */
void LCD_BackLight(uint8_t on);
/* 填充矩形，越界部分自动裁剪。
 * x      - 左上角列坐标（原生像素）
 * y      - 左上角行坐标（原生像素）
 * width  - 宽度（像素）
 * height - 高度（像素）
 * color  - RGB565颜色值
 * 返回   - 无 */
void LCD_Fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
/* 全屏填充一种颜色。
 * color - RGB565颜色值
 * 返回  - 无 */
void LCD_Clear(uint16_t color);
/* 绘制单个像素。
 * x     - 列坐标（原生像素）
 * y     - 行坐标（原生像素）
 * color - RGB565颜色值
 * 返回  - 无 */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color);
/* 设置软件反显标志，仅影响后续点阵绘制。
 * invert - 1=反显（黑白互换），0=正常
 * 返回   - 无 */
void LCD_SetInvert(uint8_t invert);
/* 设置软件旋转180度标志，与LCD_DIRECTION的屏幕方向配置独立。
 * flip - 1=旋转180度，0=恢复原方向
 * 返回 - 无 */
void LCD_SetFlip(uint8_t flip);
/* 显示8×16 ASCII字符，按整数倍放大（仅应用侧编译）。
 * x     - 左上角列坐标
 * y     - 左上角行坐标
 * ch    - 字符，0x20~0x7F；0x7F为上标3
 * color - 前景色，RGB565
 * back  - 背景色，RGB565
 * size  - 放大倍数1~4，0按1倍处理
 * 返回  - 无 */
void LCD_ShowChar(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t back, uint8_t size);
/* 显示ASCII与GBK汉字混排字符串，汉字固定32×32不随size缩放。
 * x     - 起始列坐标
 * y     - 起始行坐标
 * str   - 以空字符结尾的GBK字符串，汉字字模须存在于LCD_Chinese.h
 * color - 前景色，RGB565
 * back  - 背景色，RGB565
 * size  - ASCII放大倍数1~4
 * 返回  - 无 */
void LCD_ShowString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t back, uint8_t size);
/* 显示十进制数字，不足位补前导零。
 * x      - 起始列坐标
 * y      - 起始行坐标
 * number - 待显示的数值
 * length - 显示位数1~10，超出位数保留低位
 * color  - 前景色，RGB565
 * back   - 背景色，RGB565
 * size   - ASCII放大倍数1~4
 * 返回   - 无 */
void LCD_ShowNum(uint16_t x, uint16_t y, uint32_t number, uint8_t length, uint16_t color, uint16_t back, uint8_t size);
#endif
