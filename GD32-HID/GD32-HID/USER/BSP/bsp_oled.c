/* 中文注释补充（2026-10-26）：中文渲染：字符串仍用GBK字节转义；本文件负责查字模、整数放大、换行与屏幕边界检查。 */
#include "bsp_oled.h"
#include "bsp_lcd.h"
#include "font.h"
#include "ui_chinese_font.h"
#include <string.h>

/* 直接使用原生像素坐标，MCU侧不分配帧缓存，显示像素由LCD掌管。
 * 中文使用同一套细宋体16×16点阵，按1倍或整数2倍放大。
 * 字号即像素高度；x为列坐标，y为行坐标。 */
oled_TypeDef oled = {0x79, 0x78, 0};

/* 初始化显示：直接转调LCD_Init，保留原OLED接口名。
 * 返回 - 无 */
void OLED_Init(void) { LCD_Init(); }
/* 兼容旧OLED写字节接口；改用原生像素直绘后本函数为空实现。
 * dat  - 原OLED要写入的数据字节（已忽略）
 * mode - 原OLED命令/数据选择（已忽略）
 * 返回 - 无 */
void OLED_WR_Byte(uint8_t dat, uint8_t mode) { (void)dat; (void)mode; }
/* 开显示：转调LCD_DisplayOn，保留原OLED接口名。
 * 返回 - 无 */
void OLED_DisPlay_On(void) { LCD_DisplayOn(); }
/* 关显示：转调LCD_DisplayOff，保留原OLED接口名。
 * 返回 - 无 */
void OLED_DisPlay_Off(void) { LCD_DisplayOff(); }
/* 小型字段缓存只保存文字，不分配320×240帧缓冲。清屏后缓存及进度边框失效。 */
typedef struct {
    char text[64];
    uint16_t x, y, width, height;
    uint8_t size, mode, valid;
} ui_field_t;
static ui_field_t ui_fields[16];
static uint8_t ui_page = 255, progress_valid, progress_percent;
static uint16_t progress_y;
/* 全屏清黑，并使所有字段缓存与进度条状态失效，下次绘制全部重画。
 * 返回 - 无 */
void OLED_Clear(void)
{
    uint8_t i;
    for(i = 0; i < 16; i++) ui_fields[i].valid = 0;
    progress_valid = 0;
    ui_page = 255;
    LCD_Clear(LCD_BLACK);
}

/* 切换页面：只有页面号变化才清屏，同页刷新不触发全屏擦除。
 * page - 页面编号，0~254；255为清屏后保留的初始值
 * 返回 - 无 */
void UI_EnterPage(uint8_t page)
{
    if(ui_page == page) return;
    OLED_Clear();
    ui_page = page;
}

/* 兼容旧OLED刷新接口；改为原生像素直绘后本函数为空实现。
 * 返回 - 无（绘制在调用时已同步完成） */
void OLED_Refresh(void) { /* 直接绘制，函数返回时已完成。 */ }
/* 兼容旧OLED反显接口，转调LCD_SetInvert。
 * i    - 1=反显，0=正常
 * 返回 - 无 */
void OLED_ColorTurn(uint8_t i) { LCD_SetInvert(i); }
/* 兼容旧OLED旋转接口，转调LCD_SetFlip；注意入参极性与LCD_SetFlip相反。
 * i    - 0=旋转180度，非0=恢复原方向
 * 返回 - 无 */
void OLED_DisplayTurn(uint8_t i) { LCD_SetFlip((uint8_t)(i == 0)); }

/* 按GBK编码二分查找16×16汉字点阵。
 * code - GBK内码，高字节在前
 * 返回 - 指向点阵数据的指针；查不到时返回NULL（绘制函数改显示问号） */
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

/* 判断当前位置的两个字节是否为合法GBK汉字，保证换行/裁剪不会拆开一个汉字。
 * s    - 指向待判断字节的字符串指针，须至少有2字节可读
 * 返回 - 1=是合法GBK汉字，0=不是（按单字节ASCII处理） */
static uint8_t gbk(const char *s)
{
    uint8_t first = (uint8_t)s[0], next;
    if(first < 0x81 || first > 0xFE) return 0;
    next = (uint8_t)s[1];
    return next >= 0x40 && next <= 0xFE && next != 0x7F;
}

/* 计算混合汉字/ASCII文本的显示宽度。
 * s    - 以空字符结尾的字符串，GBK编码
 * size - 字号（像素高度），只接受16或32；汉字占size像素，ASCII占size/2像素
 * 返回 - 文本总宽度（像素）；参数非法时返回0 */
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

/* 在一行限定宽度内显示文本，超宽即停止，不自动换行。
 * x     - 起始列坐标（原生像素）
 * y     - 起始行坐标（原生像素）
 * width - 本行可用宽度（像素），超出右边界时自动裁剪
 * s     - 以空字符结尾的字符串，GBK编码
 * size  - 字号（像素高度），只接受16或32；32为16点阵整数放大
 * mode  - 1=白字黑底，0=黑字白底
 * 返回  - 指向第一个未显示字符的指针；全部显示完时指向字符串结尾 */
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

/* 显示单行文本，不换行，超出右边界自动截断。
 * x    - 起始列坐标（原生像素）
 * y    - 起始行坐标（原生像素）
 * s    - 以空字符结尾的字符串，GBK编码
 * size - 字号（像素高度），16或32
 * mode - 1=白字黑底，0=黑字白底
 * 返回 - 无 */
void UI_Text(uint16_t x, uint16_t y, const char *s, uint8_t size, uint8_t mode)
{
    UI_TextLine(x, y, LCD_WIDTH, s, size, mode);
}

/* 在指定矩形内换行显示文字，按完整字符换行，超出高度后停止。
 * x      - 左上角列坐标（原生像素）
 * y      - 左上角行坐标（原生像素）
 * width  - 文本框宽度（像素）
 * height - 文本框高度（像素），决定最多显示几行
 * s      - 以空字符结尾的字符串，GBK编码
 * size   - 字号（像素高度），16或32
 * mode   - 1=白字黑底，0=黑字白底
 * 返回   - 无（进入时先按背景色清理整个矩形） */
void UI_TextBox(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                const char *s, uint8_t size, uint8_t mode)
{
    const char *next;
    if(!s || y >= LCD_HEIGHT) return;
    if(height > LCD_HEIGHT - y) height = LCD_HEIGHT - y;
    UI_Fill(x, y, width, height, (uint8_t)(!mode)); /* 文本框只清一次背景。 */
    while(*s && height >= size)
    {
        next = UI_TextLine(x, y, width, s, size, mode);
        if(next == s) break;
        s = next;
        y += size;
        height -= size;
    }
}

/* 按屏幕像素填充矩形，不申请整屏显存。
 * x      - 左上角列坐标
 * y      - 左上角行坐标
 * width  - 宽度（像素）
 * height - 高度（像素）
 * on     - 1=填白色，0=填黑色
 * 返回   - 无 */
void UI_Fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t on)
{
    LCD_Fill(x, y, width, height, on ? LCD_WHITE : LCD_BLACK);
}

/* 显示状态小图标（18×8点阵放大两倍）。
 * x     - 左上角列坐标
 * y     - 左上角行坐标
 * index - 图标序号，对应font.h中的logo表；越界时不绘制
 * 返回  - 无（LCD_BAREMETAL编译时为空实现） */
void UI_Logo(uint16_t x, uint16_t y, uint8_t index)
{
#ifndef LCD_BAREMETAL
    if(index < sizeof(logo) / sizeof(logo[0]))
        LCD_BlitMono(x, y, 18, 8, 18, logo[index], 1, 2, 1);
#else
    (void)x; (void)y; (void)index;
#endif
}

/* 将128×64单色开机图标放大两倍显示在320×240屏幕中央偏上位置。
 * data - 128×64点阵数据，按列页式存储（每行128字节、共8页）
 * 返回 - 无 */
void UI_BootLogo(const uint8_t *data)
{
    LCD_BlitMono(32, 40, 128, 64, 128, data, 1, 2, 1);
}

/* 绘制进度条：同页只画一次边框，百分比不变不写屏，变化时只补差量。
 * y       - 进度条顶部的行坐标（原生像素）
 * percent - 进度百分比，0~100；超过100按100处理
 * 返回    - 无 */
void UI_Progress(uint16_t y, uint8_t percent)
{
    char text[5];
    uint16_t before, after;
    if(percent > 100) percent = 100;
    if(progress_valid && progress_y == y && progress_percent == percent) return;
    if(!progress_valid || progress_y != y)
    {
        UI_Fill(8, y + 4, 220, 24, 1);
        UI_Fill(10, y + 6, 216, 20, 0);
        progress_percent = 0;
    }
    before = (uint16_t)(216U * progress_percent / 100);
    after = (uint16_t)(216U * percent / 100);
    if(after > before) UI_Fill(10 + before, y + 6, after - before, 20, 1);
    else if(after < before) UI_Fill(10 + after, y + 6, before - after, 20, 0);
    text[0] = percent == 100 ? '1' : ' ';
    text[1] = percent >= 10 ? '0' + (percent / 10) % 10 : ' ';
    text[2] = '0' + percent % 10;
    text[3] = '%'; text[4] = 0;
    UI_Field(9, 244, y, 64, 32, text, 32, 1);
    progress_y = y; progress_percent = percent; progress_valid = 1;
}

/* 带缓存的文字字段刷新：逐字比较新旧内容，只重绘变化字符，短文本只擦尾部。
 * id     - 字段编号0~15，须在当前页内唯一（16个缓存槽）
 * x      - 左上角列坐标（原生像素）
 * y      - 左上角行坐标（原生像素）
 * width  - 字段宽度（像素），超出部分截断
 * height - 字段高度（像素），按size逐行换行
 * text   - 要显示的文字，GBK编码，最长63字节
 * size   - 字号（像素高度），16或32
 * mode   - 1=白字黑底，0=黑字白底
 * 返回   - 无 */
void UI_Field(uint8_t id, uint16_t x, uint16_t y, uint16_t width, uint16_t height,
              const char *text, uint8_t size, uint8_t mode)
{
    ui_field_t *field;
    char current[64], glyph[3];
    const char *now, *old;
    uint16_t row, col, advance, old_advance, old_width;
    uint8_t bytes, old_bytes, redraw;
    unsigned int length;
    if(id >= 16 || !text || x >= LCD_WIDTH || y >= LCD_HEIGHT ||
       (size != 16 && size != 32)) return;
    if(width > LCD_WIDTH - x) width = LCD_WIDTH - x;
    if(height > LCD_HEIGHT - y) height = LCD_HEIGHT - y;
    length = 0;
    while(text[length])
    {
        bytes = gbk(text + length) ? 2 : 1;
        if(length + bytes >= sizeof(current)) break;
        memcpy(current + length, text + length, bytes); length += bytes;
    }
    current[length] = 0;
    field = &ui_fields[id];
    redraw = !field->valid || field->x != x || field->y != y || field->width != width ||
             field->height != height || field->size != size || field->mode != mode;
    if(!redraw && !strcmp(field->text, current)) return;
    /* 字段缩小/移动时先清旧范围，避免32像素空目录提示缩成28像素行后留下边缘。 */
    if(field->valid && (field->x != x || field->y != y || field->width != width || field->height != height))
        UI_Fill(field->x, field->y, field->width, field->height, 0);
    if(redraw && (field->valid || !mode)) UI_Fill(x, y, width, height, (uint8_t)!mode);
    now = current; old = redraw ? "" : field->text;
    for(row = 0; row + size <= height; row += size)
    {
        col = 0;
        while(*now && col < width)
        {
            bytes = gbk(now) ? 2 : 1; advance = bytes == 2 ? size : size / 2;
            if(advance > width - col) break;
            old_bytes = *old ? (gbk(old) ? 2 : 1) : 0;
            old_advance = old_bytes == 2 ? size : size / 2;
            /* 字宽发生变化后，本行余下位置重新覆盖，避免中英文交替留下残影。 */
            if(old_bytes && old_advance != advance)
            {
                const char *next = UI_TextLine(x + col, y + row, width - col, now, size, mode);
                uint16_t used = 0;
                while(now < next) { bytes = gbk(now) ? 2 : 1; used += bytes == 2 ? size : size / 2; now += bytes; }
                UI_Fill(x + col + used, y + row, width - col - used, size, (uint8_t)!mode);
                while(*old && old_advance <= width - col)
                {
                    col += old_advance; old += old_bytes;
                    old_bytes = *old ? (gbk(old) ? 2 : 1) : 0;
                    old_advance = old_bytes == 2 ? size : size / 2;
                    if(!old_bytes) break;
                }
                col = width; break;
            }
            if(!old_bytes || old_bytes != bytes || memcmp(now, old, bytes))
            {
                memcpy(glyph, now, bytes); glyph[bytes] = 0;
                UI_TextLine(x + col, y + row, advance, glyph, size, mode);
            }
            now += bytes; if(old_bytes) old += old_bytes; col += advance;
        }
        old_width = col;
        while(*old)
        {
            old_bytes = gbk(old) ? 2 : 1; old_advance = old_bytes == 2 ? size : size / 2;
            if(old_advance > width - old_width) break;
            old_width += old_advance; old += old_bytes;
        }
        if(old_width > col) UI_Fill(x + col, y + row, old_width - col, size, (uint8_t)!mode);
    }
    strcpy(field->text, current);
    field->x = x; field->y = y; field->width = width; field->height = height;
    field->size = size; field->mode = mode; field->valid = 1;
}

/* 兼容旧OLED行列坐标接口：在128×64虚拟坐标系中画一个点，放大两倍显示。
 * row  - 虚拟行坐标，0~63；越界不绘制
 * col  - 虚拟列坐标，0~127；越界不绘制
 * on   - 1=白点，0=黑点
 * 返回 - 无 */
void OLED_DrawPoint(uint8_t row, uint8_t col, uint8_t on)
{
    if(row < 64 && col < 128) UI_Fill(8 + col * 2, 32 + row * 2, 2, 2, on);
}

/* 兼容旧OLED接口：在128×64虚拟坐标系中用Bresenham算法画直线。
 * r1   - 起点行坐标，0~63
 * c1   - 起点列坐标，0~127
 * r2   - 终点行坐标，0~63
 * c2   - 终点列坐标，0~127
 * on   - 1=白线，0=黑线
 * 返回 - 无 */
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

/* 兼容旧在线图表的行列坐标接口，内部换算成原生像素后转调UI_Text。
 * row  - 虚拟行坐标，换算为y = 32 + row*2
 * col  - 虚拟列坐标，换算为x = 8 + col*2
 * s    - 以空字符结尾的字符串，GBK编码
 * size - 旧接口字号：16按32像素显示，其它值按16像素显示
 * mode - 1=白字黑底，0=黑字白底
 * 返回 - 无 */
void OLED_ShowString(uint8_t row, uint8_t col, uint8_t *s, uint8_t size, uint8_t mode)
{
    UI_Text(8 + col * 2, 32 + row * 2, (const char *)s, size == 16 ? 32 : 16, mode);
}
