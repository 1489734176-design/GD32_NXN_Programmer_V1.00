#include "main.h"
#include "bsp_lcd.h"
#include "bsp_spi0_bus.h"

#ifndef LCD_BAREMETAL
#include "LCD_Font.h"
#include "LCD_Chinese.h"
#define LCD_DELAY(ms) vTaskDelay(pdMS_TO_TICKS(ms))
#else
#define LCD_DELAY(ms) delay_ms(ms)
#endif

/* At most 128 pixels (256 bytes) per selected transaction. No framebuffer:
 * the GD32F103CB only has 20 KB RAM. Initialization values follow LCD/LCD.c.
 */
#define LCD_CHUNK_PIXELS 128U
static uint8_t ui_invert;
static uint8_t ui_flip;

static void command(uint8_t value)
{
    SPI0_BusWait();
    GPIO_BC(GPIOB) = LCD_DC_Pin;
    SPI0_LCDWrite(value);
    SPI0_BusWait();
    GPIO_BOP(GPIOB) = LCD_DC_Pin;
}

static void data16(uint16_t value)
{
    SPI0_LCDWrite((uint8_t)(value >> 8));
    SPI0_LCDWrite((uint8_t)value);
}

static void window(uint16_t x, uint16_t y, uint16_t count)
{
    command(0x2A);
    data16(x); data16((uint16_t)(x + count - 1));
    command(0x2B);
    data16(y); data16(y);
    command(0x2C);
}

static void send_command(uint8_t value)
{
    SPI0_LCDSelect();
    command(value);
    SPI0_LCDRelease();
}

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

void LCD_DisplayOn(void)
{
    send_command(0x29);
    LCD_BackLight(1);
}

void LCD_DisplayOff(void)
{
    LCD_BackLight(0);
    send_command(0x28);
}

void LCD_Init(void)
{
    /* Packed command, argument count, arguments. No delays in this table. */
    static const uint8_t init[] = {
        0x3A,1,0x05,
        0xB2,5,0x0C,0x0C,0x00,0x33,0x33,
        0xB7,1,0x35, 0xBB,1,0x1F, 0xC0,1,0x2C,
        0xC2,1,0x01, 0xC3,1,0x12, 0xC4,1,0x20,
        0xC6,1,0x0F, 0xD0,2,0xA4,0xA1,
        0xE0,14,0xD0,0x08,0x11,0x08,0x0C,0x15,0x39,0x33,0x50,0x36,0x13,0x14,0x29,0x2D,
        0xE1,14,0xD0,0x08,0x10,0x08,0x06,0x06,0x39,0x44,0x51,0x0B,0x16,0x14,0x2F,0x31
    };
    static const uint8_t madctl[] = {0x00, 0x60, 0xC0, 0xA0};
    uint16_t i = 0;
    uint8_t n;

    SPI0_BusInit();
    /* Only the LCD owns these pins; never configure OLED I2C0. */
    GPIO_BOP(GPIOB) = LCD_DC_Pin | LCD_RES_Pin;
    LCD_BackLight(0);
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LCD_DC_Pin | LCD_RES_Pin);
    gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LCD_BLK_Pin);
    GPIO_BC(GPIOB) = LCD_RES_Pin;
    LCD_DELAY(20);
    GPIO_BOP(GPIOB) = LCD_RES_Pin;
    LCD_DELAY(120);
    send_command(0x01);
    LCD_DELAY(120);
    send_command(0x11);
    LCD_DELAY(120);
    send_command(0x13);

    SPI0_LCDSelect();
    command(0x36);
    SPI0_LCDWrite(madctl[LCD_DIRECTION]);
    SPI0_LCDRelease();
    while (i < sizeof(init)) {
        SPI0_LCDSelect();
        command(init[i++]);
        n = init[i++];
        while (n--) SPI0_LCDWrite(init[i++]);
        SPI0_LCDRelease();
    }
    send_command(0x21); /* Panel inversion, as in the supplied ST7789 driver. */
    ui_invert = 0;
    ui_flip = 0;
    LCD_Clear(LCD_BLACK);
    LCD_DisplayOn();
}

void LCD_Fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    uint16_t row, offset, count, i;
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if (width > LCD_WIDTH - x) width = LCD_WIDTH - x;
    if (height > LCD_HEIGHT - y) height = LCD_HEIGHT - y;
    for (row = 0; row < height; row++) {
        for (offset = 0; offset < width; offset += count) {
            count = width - offset;
            if (count > LCD_CHUNK_PIXELS) count = LCD_CHUNK_PIXELS;
            SPI0_LCDSelect();
            window(x + offset, y + row, count);
            for (i = 0; i < count; i++) data16(color);
            SPI0_LCDRelease();
            if (!SPI0_BusOK()) return;
        }
    }
}

void LCD_Clear(uint16_t color) { LCD_Fill(0, 0, LCD_WIDTH, LCD_HEIGHT, color); }
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color) { LCD_Fill(x, y, 1, 1, color); }
void LCD_SetInvert(uint8_t invert) { ui_invert = (uint8_t)(invert != 0); }
void LCD_SetFlip(uint8_t flip) { ui_flip = (uint8_t)(flip != 0); }

void LCD_BlitGRAM(const uint8_t gram[8][128])
{
    uint16_t row, offset, pixel;
    uint8_t sx, sy, bit;
    /* OLED_DrawPoint(row,column) stores the top pixel in bit 0.
     * Render in logical text order; LCD_DIRECTION controls panel rotation.
     */
    for (row = 0; row < 64 * LCD_UI_SCALE; row++) {
        sy = (uint8_t)(row / LCD_UI_SCALE);
        if (ui_flip) sy = 63 - sy;
        for (offset = 0; offset < 128 * LCD_UI_SCALE; offset += LCD_CHUNK_PIXELS) {
            SPI0_LCDSelect();
            window(LCD_UI_X + offset, LCD_UI_Y + row, LCD_CHUNK_PIXELS);
            for (pixel = 0; pixel < LCD_CHUNK_PIXELS; pixel++) {
                sx = (uint8_t)((offset + pixel) / LCD_UI_SCALE);
                if (ui_flip) sx = 127 - sx;
                bit = (uint8_t)(((gram[sy >> 3][sx] >> (sy & 7)) & 1) ^ ui_invert);
                data16(bit ? LCD_WHITE : LCD_BLACK);
            }
            SPI0_LCDRelease();
            if (!SPI0_BusOK()) return;
        }
    }
}

#ifndef LCD_BAREMETAL
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
            bit = (font[col / size + ((row / size) >> 3) * 8] >> ((row / size) & 7)) & 1;
            data16(bit ? color : back);
        }
        SPI0_LCDRelease();
    }
}

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

void LCD_ShowString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t back, uint8_t size)
{
    if (!size) size = 1;
    if (size > 4) return;
    while (*str && x < LCD_WIDTH) {
        if ((uint8_t)str[0] >= 0x81 && str[1]) {
            chinese(x, y, ((uint16_t)(uint8_t)str[0] << 8) | (uint8_t)str[1], color, back);
            str += 2; x += 32;
        } else {
            LCD_ShowChar(x, y, *str++, color, back, size);
            x += 8U * size;
        }
    }
}

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
