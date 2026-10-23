#include "bsp_oled.h"
#include "bsp_lcd.h"
#include "font.h"
#include "ui_chinese_font.h"

/* Native pixel coordinates, no MCU framebuffer. LCD owns the displayed pixels.
 * Chinese: the same thin SimSun 16x16 bitmap at 1x or integer 2x scale.
 * Text size is its pixel height; x is the column, y is the row. */
oled_TypeDef oled = {0x79, 0x78, 0};

void OLED_Init(void) { LCD_Init(); }
void OLED_WR_Byte(uint8_t dat, uint8_t mode) { (void)dat; (void)mode; }
void OLED_DisPlay_On(void) { LCD_DisplayOn(); }
void OLED_DisPlay_Off(void) { LCD_DisplayOff(); }
void OLED_Clear(void) { LCD_Clear(LCD_BLACK); }
void OLED_Refresh(void) { /* Direct drawing is complete on return. */ }
void OLED_ColorTurn(uint8_t i) { LCD_SetInvert(i); }
void OLED_DisplayTurn(uint8_t i) { LCD_SetFlip((uint8_t)(i == 0)); }

static const uint8_t *find_chinese(uint16_t code)
{
    uint16_t lo = 0, hi = sizeof(ui_glyph16) / sizeof(ui_glyph16[0]);
    while(lo < hi)
    {
        uint16_t mid = (lo + hi) / 2;
        if(ui_glyph16[mid].code == code) return ui_glyph16[mid].data;
        if(ui_glyph16[mid].code < code) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

static uint8_t gbk(const char *s)
{
    uint8_t first = (uint8_t)s[0], next;
    if(first < 0x81 || first > 0xFE) return 0;
    next = (uint8_t)s[1];
    return next >= 0x40 && next <= 0xFE && next != 0x7F;
}

uint16_t UI_TextWidth(const char *s, uint8_t size)
{
    uint16_t width = 0;
    if(!s || (size != 16 && size != 32)) return 0;
    while(*s)
    {
        if(gbk(s)) { width += size; s += 2; }
        else { width += size / 2; s++; }
    }
    return width;
}

/* Return the first undisplayed character, always at a complete GBK boundary. */
const char *UI_TextLine(uint16_t x, uint16_t y, uint16_t width,
                        const char *s, uint8_t size, uint8_t mode)
{
    uint16_t end, advance;
    uint8_t scale = size / 16;
    const uint8_t *bitmap;
    if(!s || (size != 16 && size != 32) || x >= LCD_WIDTH ||
       y >= LCD_HEIGHT || size > LCD_HEIGHT - y) return s;
    if(width > LCD_WIDTH - x) width = LCD_WIDTH - x;
    end = x + width;
    while(*s)
    {
        advance = gbk(s) ? size : size / 2;
        if(advance > end - x) break;
        if(gbk(s))
        {
            bitmap = find_chinese(((uint16_t)(uint8_t)s[0] << 8) | (uint8_t)s[1]);
            if(bitmap) LCD_BlitMono(x, y, 16, 16, 2, bitmap, 0, scale, mode);
            else LCD_BlitMono(x, y, 8, 16, 8, asc2_1608['?' - ' '], 1, scale, mode);
            s += 2;
        }
        else
        {
            uint8_t ch = (uint8_t)*s++;
            if(ch < ' ' || ch > '~') ch = '?';
            LCD_BlitMono(x, y, 8, 16, 8, asc2_1608[ch - ' '], 1, scale, mode);
        }
        x += advance;
    }
    return s;
}

void UI_Text(uint16_t x, uint16_t y, const char *s, uint8_t size, uint8_t mode)
{
    UI_TextLine(x, y, LCD_WIDTH, s, size, mode);
}

void UI_TextBox(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                const char *s, uint8_t size, uint8_t mode)
{
    const char *next;
    if(!s || y >= LCD_HEIGHT) return;
    if(height > LCD_HEIGHT - y) height = LCD_HEIGHT - y;
    UI_Fill(x, y, width, height, (uint8_t)(!mode));
    UI_Fill(x, y, width, height, (uint8_t)(!mode));
    while(*s && height >= size)
    {
        next = UI_TextLine(x, y, width, s, size, mode);
        if(next == s) break;
        s = next;
        y += size;
        height -= size;
    }
}

void UI_Fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t on)
{
    LCD_Fill(x, y, width, height, on ? LCD_WHITE : LCD_BLACK);
}

void UI_Logo(uint16_t x, uint16_t y, uint8_t index)
{
#ifndef LCD_BAREMETAL
    if(index < sizeof(logo) / sizeof(logo[0]))
        LCD_BlitMono(x, y, 18, 8, 18, logo[index], 1, 2, 1);
#else
    (void)x; (void)y; (void)index;
#endif
}

void UI_BootLogo(const uint8_t *data)
{
    LCD_BlitMono(32, 40, 128, 64, 128, data, 1, 2, 1);
}

void UI_Progress(uint16_t y, uint8_t percent)
{
    char text[5];
    if(percent > 100) percent = 100;
    UI_Fill(8, y, 304, 32, 0);
    UI_Fill(8, y + 4, 220, 24, 1);
    UI_Fill(10, y + 6, 216, 20, 0);
    UI_Fill(10, y + 6, (uint16_t)(216U * percent / 100), 20, 1);
    text[0] = percent == 100 ? '1' : ' ';
    text[1] = percent >= 10 ? '0' + (percent / 10) % 10 : ' ';
    text[2] = '0' + percent % 10;
    text[3] = '%'; text[4] = 0;
    UI_Text(244, y, text, 32, 1);
}

/* Legacy optional online traffic graphs retain their row/column API.
 * The default offline UI uses UI_* exclusively. */
void OLED_DrawPoint(uint8_t row, uint8_t col, uint8_t on)
{
    if(row < 64 && col < 128) UI_Fill(8 + col * 2, 32 + row * 2, 2, 2, on);
}

void OLED_DrawLine(uint8_t r1, uint8_t c1, uint8_t r2, uint8_t c2, uint8_t on)
{
    int r = r1, c = c1, dr = r2 - r1, dc = c2 - c1;
    int sr = dr < 0 ? -1 : 1, sc = dc < 0 ? -1 : 1, error, twice;
    if(dr < 0) dr = -dr;
    if(dc < 0) dc = -dc;
    error = dr - dc;
    for(;;)
    {
        OLED_DrawPoint((uint8_t)r, (uint8_t)c, on);
        if(r == r2 && c == c2) break;
        twice = 2 * error;
        if(twice > -dc) { error -= dc; r += sr; }
        if(twice < dr) { error += dr; c += sc; }
    }
}

void OLED_ShowString(uint8_t row, uint8_t col, uint8_t *s, uint8_t size, uint8_t mode)
{
    UI_Text(8 + col * 2, 32 + row * 2, (const char *)s, size == 16 ? 32 : 16, mode);
}
