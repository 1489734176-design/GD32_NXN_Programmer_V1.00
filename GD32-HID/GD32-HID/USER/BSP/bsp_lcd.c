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
 * 不分配整屏 RGB565 缓冲区，直接分块输出，避免占用大块 RAM。
 * 屏幕初始化参数沿用参考驱动 LCD/LCD.c。
 */
#define LCD_CHUNK_PIXELS 128U
static uint8_t ui_invert; /* OLED 兼容界面反显标志 */
static uint8_t ui_flip; /* OLED 兼容界面旋转 180 度标志 */

/* 在已选中的LCD事务内发送一条命令：切换DC前后都等待发送完成，
 * 避免前一字节或命令字节被误判为另一种类型；结束后DC恢复为数据模式。
 * value - 要发送的ST7789命令码（不含参数）
 * 返回  - 无（调用方须自行持有SPI总线并控制片选） */
static void command(uint8_t value)
{
    SPI0_BusWait();
    GPIO_BC(GPIOB) = LCD_DC_Pin;
    SPI0_LCDWrite(value);
    SPI0_BusWait();
    GPIO_BOP(GPIOB) = LCD_DC_Pin;
}

/* 发送16位数据，高字节在前、低字节在后。
 * value - 待发送的16位值：RGB565像素或窗口坐标
 * 返回  - 无（调用方须自行持有SPI总线并控制片选） */
static void data16(uint16_t value)
{
    SPI0_LCDWrite((uint8_t)(value >> 8));
    SPI0_LCDWrite((uint8_t)value);
}

/* 设置单行窗口：从(x,y)开始连续写count个像素，然后启动显存写入。
 * x     - 起始列坐标，范围0~LCD_WIDTH-1
 * y     - 行坐标，范围0~LCD_HEIGHT-1
 * count - 本行连续写入的像素数，必须大于0且不越界
 * 返回  - 无（调用方负责片选及坐标范围） */
static void window(uint16_t x, uint16_t y, uint16_t count)
{
    command(0x2A); /* 列地址：起点 x，终点 x+count-1 */
    data16(x); data16((uint16_t)(x + count - 1));
    command(0x2B); /* 行地址：起点与终点都为 y */
    data16(y); data16(y);
    command(0x2C); /* 后续数据写入 ST7789 显存 */
}

/* 独占共享SPI总线发送一条无参数命令，完成后释放总线。
 * value - 要发送的ST7789命令码
 * 返回  - 无（释放时恢复Flash的SPI0配置及原中断状态） */
static void send_command(uint8_t value)
{
    SPI0_LCDSelect();
    command(value);
    SPI0_LCDRelease();
}

/* 按屏幕背光极性控制背光引脚（PB6）。
 * on   - 1=点亮背光，0=熄灭；实际电平由LCD_BLK_ACTIVE_HIGH决定
 * 返回 - 无 */
void LCD_BackLight(uint8_t on)
{
#if LCD_BLK_ACTIVE_HIGH
    if (on) GPIO_BOP(LCD_BLK_GPIO_Port) = LCD_BLK_Pin;
    else GPIO_BC(LCD_BLK_GPIO_Port) = LCD_BLK_Pin;
#else
    if (on) GPIO_BC(LCD_BLK_GPIO_Port) = LCD_BLK_Pin;
    else GPIO_BOP(LCD_BLK_GPIO_Port) = LCD_BLK_Pin;
#endif
}

/* ST7789开显示（0x29），再点亮背光。
 * 返回 - 无 */
void LCD_DisplayOn(void)
{
    send_command(0x29);
    LCD_BackLight(1);
}

/* 先关闭背光，再发送ST7789关显示命令（0x28）。
 * 返回 - 无 */
void LCD_DisplayOff(void)
{
    LCD_BackLight(0);
    send_command(0x28);
}

/* 初始化ST7789：硬件复位、退出睡眠、设置扫描方向与Gamma，最后清屏开显示。
 * 所有复位及唤醒延时都放在释放SPI0片选之后，避免在关中断事务中阻塞任务。
 * 返回 - 无 */
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
    /* 只配置 LCD 的 DC、RES、BLK 与字库片选 FCS；不初始化原 OLED 的 I2C0 引脚。 */
    GPIO_BOP(GPIOB) = LCD_DC_Pin | LCD_RES_Pin;
    LCD_BackLight(0);
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LCD_DC_Pin | LCD_RES_Pin);
    gpio_init(LCD_BLK_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LCD_BLK_Pin);
    /* 软件字模不需要屏内字库：FCS 保持高电平（未选中）。 */
    rcu_periph_clock_enable(RCU_GPIOD);
    GPIO_BOP(LCD_FCS_GPIO_Port) = LCD_FCS_Pin;
    gpio_init(LCD_FCS_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LCD_FCS_Pin);
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

/* 填充矩形，坐标为左上角，逐行分块写入；超出屏幕的部分自动裁剪。
 * x      - 左上角列坐标，>=LCD_WIDTH时直接返回
 * y      - 左上角行坐标，>=LCD_HEIGHT时直接返回
 * width  - 宽度（像素），超出右边界时被裁剪
 * height - 高度（像素），超出下边界时被裁剪
 * color  - RGB565颜色值
 * 返回   - 无（总线故障时提前停止后续输出） */
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

/* 全屏清为指定颜色。
 * color - RGB565颜色值
 * 返回  - 无 */
void LCD_Clear(uint16_t color) { LCD_Fill(0, 0, LCD_WIDTH, LCD_HEIGHT, color); }
/* 绘制单个像素，复用矩形填充的边界检查。
 * x     - 列坐标，范围0~LCD_WIDTH-1
 * y     - 行坐标，范围0~LCD_HEIGHT-1
 * color - RGB565颜色值
 * 返回  - 无 */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color) { LCD_Fill(x, y, 1, 1, color); }
/* 设置软件反显标志，仅影响下一次点阵绘制，不改变屏幕硬件反色。
 * invert - 1=反显（黑白互换），0=正常
 * 返回   - 无 */
void LCD_SetInvert(uint8_t invert) { ui_invert = (uint8_t)(invert != 0); }
/* 设置软件旋转180度标志，与LCD_DIRECTION的屏幕方向配置独立。
 * flip - 1=旋转180度，0=恢复原方向
 * 返回 - 无（内部重发MADCTL命令） */
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
/* 显示8×16 ASCII字符，字模按整数倍放大。
 * x     - 左上角列坐标，越界或放不下整字时不显示
 * y     - 左上角行坐标，越界或放不下整字时不显示
 * ch    - 要显示的字符；超出0x20~0x7F时按空格处理
 * color - 前景（笔画）颜色，RGB565
 * back  - 背景颜色，RGB565
 * size  - 放大倍数1~4；传0按1倍处理，超过4直接返回
 * 返回  - 无 */
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

/* 按GBK内码查找并显示固定32×32汉字，未找到字模则整字填背景色。
 * x     - 左上角列坐标，越界或放不下整字时不显示
 * y     - 左上角行坐标，越界或放不下整字时不显示
 * code  - GBK内码，高字节在前（如“中”为0xD6D0）
 * color - 前景（笔画）颜色，RGB565
 * back  - 背景颜色，RGB565
 * 返回  - 无 */
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

/* 显示ASCII/GBK混排字符串：ASCII随size放大，汉字固定32×32。
 * x     - 起始列坐标，显示到右边界自动停止
 * y     - 起始行坐标
 * str   - 以空字符结尾的字符串，GBK编码
 * color - 前景颜色，RGB565
 * back  - 背景颜色，RGB565
 * size  - ASCII放大倍数1~4；传0按1倍，超过4直接返回
 * 返回  - 无 */
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

/* 把无符号数转成固定长度十进制字符串并显示，不足位补0。
 * x      - 起始列坐标
 * y      - 起始行坐标
 * number - 待显示的数值，超出位数的高位被舍弃
 * length - 显示位数，1~10；超过10按10处理
 * color  - 前景颜色，RGB565
 * back   - 背景颜色，RGB565
 * size   - ASCII放大倍数1~4
 * 返回   - 无 */
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

/* 把单色点阵直接写进LCD原生像素坐标，不申请临时像素数组。
 * x            - 目标左上角列坐标
 * y            - 目标左上角行坐标
 * width        - 点阵宽度（像素），放大前
 * height       - 点阵高度（像素），放大前
 * stride       - 每行（列页式）或每行字节数（行式）的存储跨度
 * bitmap       - 点阵数据指针，为空时直接返回
 * column_pages - 点阵存储方式：1=列页式（每8行一页，低位在上），0=行式（高位在左）
 * scale        - 放大倍数1~4，0或超过4时直接返回
 * mode         - 1=正显（点阵位为1画白），0=反显
 * 返回         - 无（总线故障时提前停止后续输出） */
void LCD_BlitMono(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                  uint16_t stride, const uint8_t *bitmap, uint8_t column_pages,
                  uint8_t scale, uint8_t mode)
{
    LCD_BlitMonoColor(x, y, width, height, stride, bitmap, column_pages, scale, mode, LCD_WHITE);
}

/* 彩色点阵仍分块直接写屏，不增加显存；color为笔画颜色，背景黑色。 */
void LCD_BlitMonoColor(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                       uint16_t stride, const uint8_t *bitmap, uint8_t column_pages,
                       uint8_t scale, uint8_t mode, uint16_t color)
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
                data16(bit ? color : LCD_BLACK);
            }
            SPI0_LCDRelease();
            if(!SPI0_BusOK()) return;
        }
    }
}

/* 兼容旧 128x64 OLED 页式显存：整屏放大后居中写入 LCD。
 * gram - 8页×128列，每字节低位在上；ui_flip/ui_invert 与旧接口一致。 */
#define LCD_UI_SCALE   ((LCD_WIDTH == 240) ? 1U : 2U)
#define LCD_UI_X       ((uint16_t)((LCD_WIDTH - 128U * LCD_UI_SCALE) / 2U))
#define LCD_UI_Y       ((uint16_t)((LCD_HEIGHT - 64U * LCD_UI_SCALE) / 2U))
void LCD_BlitGRAM(const uint8_t gram[8][128])
{
    uint16_t row, offset, pixel;
    uint8_t sx, sy, bit;
    for(row = 0; row < 64U * LCD_UI_SCALE; row++)
    {
        sy = (uint8_t)(row / LCD_UI_SCALE);
        if(ui_flip) sy = (uint8_t)(63U - sy);
        for(offset = 0; offset < 128U * LCD_UI_SCALE; offset += LCD_CHUNK_PIXELS)
        {
            SPI0_LCDSelect();
            window((uint16_t)(LCD_UI_X + offset), (uint16_t)(LCD_UI_Y + row), LCD_CHUNK_PIXELS);
            for(pixel = 0; pixel < LCD_CHUNK_PIXELS; pixel++)
            {
                sx = (uint8_t)((offset + pixel) / LCD_UI_SCALE);
                if(ui_flip) sx = (uint8_t)(127U - sx);
                bit = (uint8_t)(((gram[sy >> 3][sx] >> (sy & 7U)) & 1U) ^ ui_invert);
                data16(bit ? LCD_WHITE : LCD_BLACK);
            }
            SPI0_LCDRelease();
            if(!SPI0_BusOK()) return;
        }
    }
}

/* 同一次字模写出依据绝对列坐标选择背景；进度填充经过字模时同步反色。
 * 横向裁剪不改变字模原点，因此可以只重绘填充变化所穿过的若干列。 */
void LCD_BlitMonoSplit(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                      uint16_t stride, const uint8_t *bitmap, uint8_t column_pages,
                      uint16_t split_x, uint16_t color,
                      uint16_t clip_x, uint16_t clip_width)
{
    uint16_t left, right, clip_right, row, col, count, pixel, sx;
    uint8_t bit;
    if(!bitmap || !width || !height || !clip_width || x >= LCD_WIDTH ||
       y >= LCD_HEIGHT || clip_x >= LCD_WIDTH) return;
    if(width > LCD_WIDTH - x) width = LCD_WIDTH - x;
    if(height > LCD_HEIGHT - y) height = LCD_HEIGHT - y;
    if(clip_width > LCD_WIDTH - clip_x) clip_width = LCD_WIDTH - clip_x;
    left = x > clip_x ? x : clip_x;
    right = x + width;
    clip_right = clip_x + clip_width;
    if(right > clip_right) right = clip_right;
    if(left >= right) return;
    for(row = 0; row < height; row++)
    {
        for(col = left; col < right; col += count)
        {
            count = right - col;
            if(count > LCD_CHUNK_PIXELS) count = LCD_CHUNK_PIXELS;
            SPI0_LCDSelect();
            window(col, y + row, count);
            for(pixel = 0; pixel < count; pixel++)
            {
                sx = col + pixel - x;
                if(column_pages) bit = (bitmap[(row >> 3) * stride + sx] >> (row & 7)) & 1;
                else bit = (bitmap[row * stride + sx / 8] >> (7 - (sx & 7))) & 1;
                bit ^= ui_invert ^ (uint8_t)(col + pixel < split_x);
                data16(bit ? color : LCD_BLACK);
            }
            SPI0_LCDRelease();
            if(!SPI0_BusOK()) return;
        }
    }
}
