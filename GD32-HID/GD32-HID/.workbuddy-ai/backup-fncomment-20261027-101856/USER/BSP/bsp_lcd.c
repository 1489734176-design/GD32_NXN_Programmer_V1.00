/* 中文注释补充（2026-10-26）：LCD由应用与启动程序共用；硬件SPI及既有引脚保持，字模绘制以原生像素坐标进行。 */
/* ST7789 液晶驱动：通过共享硬件 SPI0 输出像素，保留原 OLED 界面接口。 */
#include "main.h"
#include "bsp_lcd.h"
#include "bsp_spi0_bus.h"

/* 应用使用 FreeRTOS 延时及字库；Bootloader 使用裸机毫秒延时。 */
#ifndef LCD_BAREMETAL
#include "LCD_Font.h"
#include "LCD_Chinese.h"
#define LCD_DELAY(ms) vTaskDelay(pdMS_TO_TICKS(ms))
#else
#define LCD_DELAY(ms) delay_ms(ms)
#endif

/* 每次片选最多发送 128 个像素（256 字节），及时释放总线供 Flash 使用。
 * GD32F103CB 只有 20 KB RAM，不分配整屏 RGB565 缓冲区，直接分块输出。
 * 屏幕初始化参数沿用参考驱动 LCD/LCD.c。
 */
#define LCD_CHUNK_PIXELS 128U
static uint8_t ui_invert; /* OLED 兼容界面反显标志 */
static uint8_t ui_flip; /* OLED 兼容界面旋转 180 度标志 */

/* 在已选中的 LCD 事务内发送命令；切换 DC 前后都等待发送完成，
 * 避免前一字节或命令字节被误判为另一种类型，结束后 DC 恢复为数据模式。
 */
/* 向LCD发送一条命令，调用方负责持有SPI总线并控制片选。 */
static void command(uint8_t value)
{
    SPI0_BusWait();
    GPIO_BC(GPIOB) = LCD_DC_Pin;
    SPI0_LCDWrite(value);
    SPI0_BusWait();
    GPIO_BOP(GPIOB) = LCD_DC_Pin;
}

/* 发送 16 位地址或 RGB565 像素，高字节在前、低字节在后。 */
/* 向LCD发送RGB565像素的两个字节。 */
static void data16(uint16_t value)
{
    SPI0_LCDWrite((uint8_t)(value >> 8));
    SPI0_LCDWrite((uint8_t)value);
}

/* 设置单行窗口：从 (x,y) 开始连续写 count 个像素，然后启动显存写入。
 * 调用方负责片选及坐标范围，count 必须大于 0。
 */
/* 设置LCD连续像素写入窗口，配合分块发送减少共享SPI总线占用时间。 */
static void window(uint16_t x, uint16_t y, uint16_t count)
{
    command(0x2A); /* 列地址：起点 x，终点 x+count-1 */
    data16(x); data16((uint16_t)(x + count - 1));
    command(0x2B); /* 行地址：起点与终点都为 y */
    data16(y); data16(y);
    command(0x2C); /* 后续数据写入 ST7789 显存 */
}

/* 独立发送无参数命令；释放时恢复 Flash 的 SPI0 配置及原中断状态。 */
/* 独占共享SPI总线发送LCD控制命令，完成后释放总线。 */
static void send_command(uint8_t value)
{
    SPI0_LCDSelect();
    command(value);
    SPI0_LCDRelease();
}

/* 按屏幕背光极性控制 PA10，on 非零表示点亮。 */
void LCD_BackLight(uint8_t on)
{
#if LCD_BLK_ACTIVE_HIGH
    if (on) GPIO_BOP(GPIOA) = LCD_BLK_Pin;
    else GPIO_BC(GPIOA) = LCD_BLK_Pin;
#else
    if (on) GPIO_BC(GPIOA) = LCD_BLK_Pin;
    else GPIO_BOP(GPIOA) = LCD_BLK_Pin;
#endif
}

/* ST7789 开显示，再点亮背光。 */
void LCD_DisplayOn(void)
{
    send_command(0x29);
    LCD_BackLight(1);
}

/* 先关闭背光，再发送 ST7789 关显示命令。 */
void LCD_DisplayOff(void)
{
    LCD_BackLight(0);
    send_command(0x28);
}

/* 初始化屏幕；所有复位及唤醒延时都放在释放 SPI0 片选之后，
 * 避免在共享总线的关中断事务中阻塞任务或 USB 存储访问。
 */
void LCD_Init(void)
{
    /* 初始化表按“命令、参数个数、参数内容”顺序打包，表内不包含延时。 */
    static const uint8_t init[] = {
        0x3A,1,0x05, /* 像素格式：16 位 RGB565 */
        0xB2,5,0x0C,0x0C,0x00,0x33,0x33, /* 行场消隐时序 */
        /* 栅极控制、VCOM 电压与液晶模块控制。 */
        0xB7,1,0x35, 0xBB,1,0x1F, 0xC0,1,0x2C,
        /* 启用命令设置 VDV/VRH，并配置 VRH 与 VDV 参数。 */
        0xC2,1,0x01, 0xC3,1,0x12, 0xC4,1,0x20,
        0xC6,1,0x0F, 0xD0,2,0xA4,0xA1, /* 帧率与电源控制 */
        /* 正极性 Gamma 曲线，14 字节。 */
        0xE0,14,0xD0,0x08,0x11,0x08,0x0C,0x15,0x39,0x33,0x50,0x36,0x13,0x14,0x29,0x2D,
        /* 负极性 Gamma 曲线，14 字节。 */
        0xE1,14,0xD0,0x08,0x10,0x08,0x06,0x06,0x39,0x44,0x51,0x0B,0x16,0x14,0x2F,0x31
    };
    /* MADCTL 的 MY/MX/MV 控制行列方向；顺序对应 LCD_DIRECTION 的 0~3。 */
    static const uint8_t madctl[] = {0x00, 0x60, 0xC0, 0xA0};
    uint16_t i = 0;
    uint8_t n;

    SPI0_BusInit();
    /* 只配置 LCD 的 DC、RES 和 BLK；不初始化原 OLED 的 I2C0 引脚。 */
    GPIO_BOP(GPIOB) = LCD_DC_Pin | LCD_RES_Pin;
    LCD_BackLight(0);
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LCD_DC_Pin | LCD_RES_Pin);
    gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LCD_BLK_Pin);
    /* 硬件复位：保持低电平 20 ms，拉高后等待 120 ms。 */
    GPIO_BC(GPIOB) = LCD_RES_Pin;
    LCD_DELAY(20);
    GPIO_BOP(GPIOB) = LCD_RES_Pin;
    LCD_DELAY(120);
    send_command(0x01); /* 软件复位 */
    LCD_DELAY(120);
    send_command(0x11); /* 退出睡眠，等待屏幕唤醒 */
    LCD_DELAY(120);
    send_command(0x13); /* 进入正常显示模式 */

    SPI0_LCDSelect();
    command(0x36); /* 设置屏幕扫描方向 */
    SPI0_LCDWrite(madctl[LCD_DIRECTION]);
    SPI0_LCDRelease();
    /* 每条寄存器命令独立占用总线，参数发送完立即释放。 */
    while (i < sizeof(init)) {
        SPI0_LCDSelect();
        command(init[i++]);
        n = init[i++];
        while (n--) SPI0_LCDWrite(init[i++]);
        SPI0_LCDRelease();
    }
    send_command(0x21); /* 屏幕硬件反色配置沿用参考驱动，与 ui_invert 独立。 */
    ui_invert = 0;
    ui_flip = 0;
    /* 先清屏，再开显示与背光，避免上电时显示未初始化的显存。 */
    LCD_Clear(LCD_BLACK);
    LCD_DisplayOn();
}

/* 填充矩形：坐标为左上角，宽高以像素计，颜色为 RGB565。
 * 超出屏幕的部分裁剪，逐行分块写入；总线故障时停止后续输出。
 */
void LCD_Fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    uint16_t row, offset, count, i;
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if (width > LCD_WIDTH - x) width = LCD_WIDTH - x;
    if (height > LCD_HEIGHT - y) height = LCD_HEIGHT - y;
    for (row = 0; row < height; row++) {
        for (offset = 0; offset < width; offset += count) {
            count = width - offset;
            /* 限制每块长度，避免整行/整屏长时间屏蔽中断。 */
            if (count > LCD_CHUNK_PIXELS) count = LCD_CHUNK_PIXELS;
            SPI0_LCDSelect();
            window(x + offset, y + row, count);
            for (i = 0; i < count; i++) data16(color);
            SPI0_LCDRelease();
            if (!SPI0_BusOK()) return;
        }
    }
}

/* 全屏清为指定颜色。 */
void LCD_Clear(uint16_t color) { LCD_Fill(0, 0, LCD_WIDTH, LCD_HEIGHT, color); }
/* 单像素绘制，复用矩形填充的边界检查。 */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color) { LCD_Fill(x, y, 1, 1, color); }
/* 设置软件反显标志，仅影响下一次 OLED 显存转换。 */
void LCD_SetInvert(uint8_t invert) { ui_invert = (uint8_t)(invert != 0); }
/* 设置软件旋转 180 度标志，与 LCD_DIRECTION 的屏幕方向配置独立。 */
void LCD_SetFlip(uint8_t flip)
{
    static const uint8_t madctl[] = {0x00, 0x60, 0xC0, 0xA0};
    ui_flip = (uint8_t)(flip != 0);
    SPI0_LCDSelect();
    command(0x36);
    SPI0_LCDWrite(madctl[LCD_DIRECTION] ^ (ui_flip ? 0xC0 : 0));
    SPI0_LCDRelease();
}

/* 直接字库显示仅供应用使用，Bootloader 不编译以下接口以节省空间。 */
#ifndef LCD_BAREMETAL
/* 显示 8x16 ASCII 字符：size 为 1~4 倍，0 按 1 倍；越界则不显示。
 * 不支持的字符按空格处理，color/back 分别为前景/背景色。
 */
void LCD_ShowChar(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t back, uint8_t size)
{
    const uint8_t *font;
    uint16_t row, col;
    uint8_t bit;
    if (!size) size = 1;
    if (size > 4 || x >= LCD_WIDTH || y >= LCD_HEIGHT ||
        8U * size > LCD_WIDTH - x || 16U * size > LCD_HEIGHT - y) return;
    if ((uint8_t)ch < ' ' || (uint8_t)ch > 0x7F) ch = ' ';
    font = LCD_F8x16[(uint8_t)ch - ' '];
    for (row = 0; row < 16U * size; row++) {
        SPI0_LCDSelect();
        window(x, y + row, 8U * size);
        for (col = 0; col < 8U * size; col++) {
            /* 字模按列存储，前 8 字节为上半区，后 8 字节为下半区；
             * 行列除以 size 得到原字模坐标，实现整数倍放大。
             */
            bit = (font[col / size + ((row / size) >> 3) * 8] >> ((row / size) & 7)) & 1;
            data16(bit ? color : back);
        }
        SPI0_LCDRelease();
    }
}

/* 按 GBK 内码查找并显示固定 32x32 汉字，未找到字模则填背景色。
 * 汉字字模按行存储，每行 4 字节，高位对应左侧像素。
 */
static void chinese(uint16_t x, uint16_t y, uint16_t code, uint16_t color, uint16_t back)
{
    const uint8_t *font = 0;
    uint16_t k, row, col;
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT || 32U > LCD_WIDTH - x || 32U > LCD_HEIGHT - y) return;
    for (k = 0; k < sizeof(LCD_F32x32) / sizeof(LCD_F32x32[0]); k++) {
        if (LCD_F32x32[k].Code == code) { font = LCD_F32x32[k].Data; break; }
    }
    for (row = 0; row < 32; row++) {
        SPI0_LCDSelect();
        window(x, y + row, 32);
        for (col = 0; col < 32; col++) {
            data16(font && (font[row * 4 + col / 8] & (0x80U >> (col & 7))) ? color : back);
        }
        SPI0_LCDRelease();
    }
}

/* 字符串按 ASCII/GBK 混排；ASCII 随 size 放大，汉字固定 32x32。
 * 字符串须以空字符结尾，显示到右边界停止，单个字形越界时不绘制。
 */
void LCD_ShowString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t back, uint8_t size)
{
    if (!size) size = 1;
    if (size > 4) return;
    while (*str && x < LCD_WIDTH) {
        /* 首字节 >= 0x81 且还有下一字节时，按两个字节组合 GBK 内码。 */
        if ((uint8_t)str[0] >= 0x81 && str[1]) {
            chinese(x, y, ((uint16_t)(uint8_t)str[0] << 8) | (uint8_t)str[1], color, back);
            str += 2; x += 32;
        } else {
            LCD_ShowChar(x, y, *str++, color, back, size);
            x += 8U * size;
        }
    }
}

/* 将无符号数转换为固定长度十进制字符串，最多 10 位。
 * 从低位向高位取数，不足位补 0，超出 length 的高位舍弃。
 */
void LCD_ShowNum(uint16_t x, uint16_t y, uint32_t number, uint8_t length, uint16_t color, uint16_t back, uint8_t size)
{
    char str[11];
    uint8_t i;
    if (length > 10) length = 10;
    str[length] = '\0';
    for (i = length; i; i--) { str[i - 1] = '0' + number % 10; number /= 10; }
    LCD_ShowString(x, y, str, color, back, size);
}
#endif

/* 把单色点阵直接写进LCD原生像素坐标。
 * 行式点阵：高位在左；列页式点阵：每8行一页，低位在上。
 * 不申请临时像素数组，单次SPI传输不超过128个像素。 */
void LCD_BlitMono(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                  uint16_t stride, const uint8_t *bitmap, uint8_t column_pages,
                  uint8_t scale, uint8_t mode)
{
    uint16_t row, offset, count, pixel, sx, sy, out_width, out_height;
    uint8_t bit;
    if(!bitmap || !scale || scale > 4 || !width || !height ||
       x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    out_width = width * scale;
    out_height = height * scale;
    if(out_width > LCD_WIDTH - x) out_width = LCD_WIDTH - x;
    if(out_height > LCD_HEIGHT - y) out_height = LCD_HEIGHT - y;
    for(row = 0; row < out_height; row++)
    {
        sy = row / scale;
        for(offset = 0; offset < out_width; offset += count)
        {
            count = out_width - offset;
            if(count > LCD_CHUNK_PIXELS) count = LCD_CHUNK_PIXELS;
            SPI0_LCDSelect();
            window(x + offset, y + row, count);
            for(pixel = 0; pixel < count; pixel++)
            {
                sx = (offset + pixel) / scale;
                if(column_pages) bit = (bitmap[(sy >> 3) * stride + sx] >> (sy & 7)) & 1;
                else bit = (bitmap[sy * stride + sx / 8] >> (7 - (sx & 7))) & 1;
                bit ^= (uint8_t)(!mode) ^ ui_invert;
                data16(bit ? LCD_WHITE : LCD_BLACK);
            }
            SPI0_LCDRelease();
            if(!SPI0_BusOK()) return;
        }
    }
}
