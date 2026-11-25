/* 中文注释补充（2026-10-26）：中文渲染：字符串仍用GBK字节转义；本文件负责查字模、整数放大、换行与屏幕边界检查。 */
#include "bsp_oled.h"
#include "bsp_lcd.h"
#include "font.h"
#include "ui_chinese_font.h"
#include <string.h>
#include <stdio.h>

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
/* 小型字段缓存只保存文字，不分配帧缓冲。清屏后缓存及进度边框失效。 */
typedef struct {
    char text[64];
    uint16_t x, y, width, height, color;
    uint8_t size, mode, valid;
} ui_field_t;
static ui_field_t ui_fields[16];
static uint16_t ui_text_color = LCD_WHITE; /* 仅彩色文字调用期间改变，返回后恢复。 */
static uint8_t ui_page = 255, progress_valid, progress_percent;
static uint16_t progress_y;
/* 最多每8像素一个字节；240像素屏仅42字节静态缓存，无像素数组。 */
typedef struct {
    char text[LCD_WIDTH / 8 + 1];
    uint8_t valid;
    uint16_t x, y, width, fill, color;
} ui_progress_status_t;
static ui_progress_status_t ui_progress_status[2];
/* 全屏清黑，并使所有字段缓存与进度条状态失效，下次绘制全部重画。
 * 返回 - 无 */
void OLED_Clear(void)
{
    uint8_t i;
    for(i = 0; i < 16; i++) ui_fields[i].valid = 0;
    progress_valid = 0;
    ui_progress_status[0].valid = ui_progress_status[1].valid = 0;
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
            if(bitmap) LCD_BlitMonoColor(x, y, 16, 16, 2, bitmap, 0, scale, mode, ui_text_color);
            else
            {
                /* 缺字占完整汉字单元；先清除右半格，避免旧16像素字留下残影。 */
                UI_Fill(x, y, advance, size, (uint8_t)!mode);
                LCD_BlitMonoColor(x, y, 8, 16, 8, asc2_1608['?' - ' '], 1, scale, mode, ui_text_color);
            }
            s += 2;
        }
        else
        {
            uint8_t ch = (uint8_t)*s++;
            if(ch < ' ' || ch > '~') ch = '?';
            LCD_BlitMonoColor(x, y, 8, 16, 8, asc2_1608[ch - ' '], 1, scale, mode, ui_text_color);
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

/* 显式前景颜色只在本次调用生效，不改变后续单色文字/图标。 */
void UI_TextColor(uint16_t x, uint16_t y, const char *text, uint8_t size,
                  uint8_t mode, uint16_t color)
{
    uint16_t previous = ui_text_color;
    ui_text_color = color;
    UI_Text(x, y, text, size, mode);
    ui_text_color = previous;
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
        if(height < size + UI_LINE_GAP) break;
        y += size + UI_LINE_GAP;
        height -= size + UI_LINE_GAP;
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
    LCD_Fill(x, y, width, height, on ? ui_text_color : LCD_BLACK);
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
        LCD_BlitMonoColor(x, y, 18, 8, 18, logo[index], 1, 2, 1, LCD_YELLOW);
#else
    (void)x; (void)y; (void)index;
#endif
}

/* 电池左侧独立竖闪电，8x16行式单色点阵；拔掉充电器时只清除此矩形。 */
void UI_ChargingBolt(uint16_t x, uint16_t y, uint8_t visible)
{
    static const uint8_t bolt[16] = {
        0x06, 0x0C, 0x0C, 0x18, 0x18, 0x30, 0x3E, 0x7C,
        0x1C, 0x18, 0x18, 0x30, 0x30, 0x60, 0x60, 0x40
    };
    if(visible) LCD_BlitMonoColor(x, y, 8, 16, 1, bolt, 0, 1, 1, LCD_YELLOW);
    else LCD_Fill(x, y, 8, 16, LCD_BLACK);
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
        LCD_Fill(8, y + 4, 220, 24, LCD_BLUE);
        UI_Fill(10, y + 6, 216, 20, 0);
        progress_percent = 0;
    }
    before = (uint16_t)(216U * progress_percent / 100);
    after = (uint16_t)(216U * percent / 100);
    if(after > before) LCD_Fill(10 + before, y + 6, after - before, 20, LCD_YELLOW);
    else if(after < before) UI_Fill(10 + after, y + 6, before - after, 20, 0);
    text[0] = percent == 100 ? '1' : ' ';
    text[1] = percent >= 10 ? '0' + (percent / 10) % 10 : ' ';
    text[2] = '0' + percent % 10;
    text[3] = '%'; text[4] = 0;
    UI_FieldColor(9, 244, y + 8, 64, 16, text, 16, 1, LCD_YELLOW);
    progress_y = y; progress_percent = percent; progress_valid = 1;
}

/* 叠字进度仅缓存可见文字。变长/变短、GBK换字及填充移动都合成同一脏区，
 * 先恢复该区的正确双色背景，再在相同裁剪区内重新写入相交字模。 */
void UI_ProgressStatus(uint16_t x, uint16_t y, uint16_t width,
                       uint8_t percent, uint8_t state, const char *status,
                       uint8_t row)
{
    ui_progress_status_t *cache = &ui_progress_status[row ? 1U : 0U];
    char text[LCD_WIDTH / 8 + 1];
    uint16_t fill, color, first, last, before, after, col, n, bytes, old_n;
    const uint8_t *bitmap;
    const char *p;
    uint8_t ch;
    if(!width || x >= LCD_WIDTH || y >= LCD_HEIGHT || 16U > LCD_HEIGHT - y) return;
    if(width > LCD_WIDTH - x) width = LCD_WIDTH - x;
    if(percent > 100) percent = 100;
    if(state > UI_PROGRESS_SUCCESS) state = UI_PROGRESS_IDLE;
    color = state == UI_PROGRESS_FAILED ? LCD_RED :
            state == UI_PROGRESS_SUCCESS ? LCD_GREEN : LCD_YELLOW;
    fill = state == UI_PROGRESS_RUNNING ? (uint16_t)(width * percent / 100U) :
           state == UI_PROGRESS_IDLE ? 0 : width;
    n = 0;
    if(state == UI_PROGRESS_RUNNING)
    {
        if(percent == 100) text[n++] = '1';
        if(percent >= 10) text[n++] = '0' + (percent / 10) % 10;
        text[n++] = '0' + percent % 10;
        text[n++] = '%';
        if(status && *status) text[n++] = ' ';
    }
    if(n > width / 8) n = width / 8;
    p = status ? status : "";
    while(*p)
    {
        bytes = gbk(p) ? 2 : 1;
        if((n + bytes) * 8U > width) break;
        memcpy(text + n, p, bytes); n += bytes; p += bytes;
    }
    text[n] = 0;
    first = width; last = 0;
    if(!cache->valid || cache->x != x || cache->y != y ||
       cache->width != width || cache->color != color)
    {
        first = 0; last = width;
    }
    else
    {
        /* 相同字节数的字符串按完整字逐项比较，保留未变化的前后文字。 */
        old_n = (uint16_t)strlen(cache->text);
        for(col = 0; col < n || col < old_n; )
        {
            bytes = col < n && gbk(text + col) ? 2 : 1;
            if(col >= n || col >= old_n ||
               bytes != (gbk(cache->text + col) ? 2U : 1U) ||
               memcmp(text + col, cache->text + col, bytes))
            {
                if(first > col * 8U) first = col * 8U;
                if(n != old_n || (col < old_n && bytes != (gbk(cache->text + col) ? 2U : 1U)))
                {
                    last = (n > old_n ? n : old_n) * 8U;
                    break;
                }
                last = (col + bytes) * 8U;
            }
            col += bytes;
        }
        before = cache->fill; after = fill;
        if(before > after) { before = fill; after = cache->fill; }
        if(before != after)
        {
            if(first > before) first = before;
            if(last < after) last = after;
        }
    }
    if(first < last)
    {
        if(first < fill)
        {
            after = last < fill ? last : fill;
            LCD_Fill(x + first, y, after - first, 16, color);
        }
        if(last > fill)
        {
            before = first > fill ? first : fill;
            LCD_Fill(x + before, y, last - before, 16, LCD_BLACK);
        }
        for(col = 0, p = text; *p; p += bytes, col += bytes * 8U)
        {
            bytes = gbk(p) ? 2 : 1;
            if(col >= last || col + bytes * 8U <= first) continue;
            if(bytes == 2)
                bitmap = find_chinese(((uint16_t)(uint8_t)p[0] << 8) | (uint8_t)p[1]);
            else bitmap = NULL;
            if(bitmap)
                LCD_BlitMonoSplit(x + col, y, 16, 16, 2, bitmap, 0,
                                  x + fill, color, x + first, last - first);
            else
            {
                ch = bytes == 2 ? '?' : (uint8_t)*p;
                if(ch < ' ' || ch > '~') ch = '?';
                LCD_BlitMonoSplit(x + col, y, 8, 16, 8, asc2_1608[ch - ' '], 1,
                                  x + fill, color, x + first, last - first);
            }
        }
    }
    strcpy(cache->text, text);
    cache->x = x; cache->y = y; cache->width = width;
    cache->fill = fill; cache->color = color; cache->valid = 1;
}

/* 带缓存的文字字段刷新：逐字比较新旧内容，只重绘变化字符，短文本只擦尾部。
 * id     - 字段编号0~15，须在当前页内唯一（16个缓存槽）
 * x      - 左上角列坐标（原生像素）
 * y      - 左上角行坐标（原生像素）
 * width  - 字段宽度（像素），超出部分截断
 * height - 字段高度（像素），按size+UI_LINE_GAP逐行换行
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
             field->height != height || field->size != size || field->mode != mode ||
             field->color != ui_text_color;
    if(!redraw && !strcmp(field->text, current)) return;
    /* 字段缩小/移动时先清旧范围，避免32像素空目录提示缩成28像素行后留下边缘。 */
    if(field->valid && (field->x != x || field->y != y || field->width != width || field->height != height))
        UI_Fill(field->x, field->y, field->width, field->height, 0);
    if(redraw && (field->valid || !mode)) UI_Fill(x, y, width, height, (uint8_t)!mode);
    now = current; old = redraw ? "" : field->text;
    for(row = 0; row + size <= height; row += size + UI_LINE_GAP)
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
    field->size = size; field->mode = mode; field->color = ui_text_color; field->valid = 1;
}

/* 彩色字段共用原有逐字缓存；颜色也属于字段身份，支持只改颜色。 */
void UI_FieldColor(uint8_t id, uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                   const char *text, uint8_t size, uint8_t mode, uint16_t color)
{
    uint16_t previous = ui_text_color;
    ui_text_color = color;
    UI_Field(id, x, y, width, height, text, size, mode);
    ui_text_color = previous;
}

/* 几何布局变化前统一清旧字段，避免逐项清理擦到已按新位置画出的内容。 */
void UI_ClearFields(uint8_t first, uint8_t count)
{
    uint8_t i;
    for(i = first; i < 16 && i - first < count; i++)
    {
        ui_field_t *field = &ui_fields[i];
        if(field->valid) UI_Fill(field->x, field->y, field->width, field->height, 0);
        field->valid = 0;
    }
}

/* 编号与名称紧接；续行对齐名称，最长31字节名称最多占两行。 */
uint16_t UI_NumberedItemHeight(uint32_t number, const char *name)
{
    char prefix[16];
    uint16_t indent;
    sprintf(prefix, "%lu.", (unsigned long)number);
    indent = (uint16_t)strlen(prefix) * 8U;
    return UI_TextWidth(name, 16) > 224U - indent ? 36U : 16U;
}

/* 列表沿用ui_fields缓存，不分配新文字表或帧缓冲；只重画内容/选中态改变的项。
 * 同页上下移动只更新原选中项和新选中项；跨页先由调用方UI_ClearFields清旧几何。 */
uint16_t UI_NumberedItem(uint8_t id, uint16_t y, uint32_t number,
                         const char *name, uint8_t selected)
{
    char prefix[16], text[64];
    const char *next;
    uint16_t indent, height, previous;
    uint8_t mode = selected ? 0 : 1;
    ui_field_t *field;
    if(id >= 16 || !name || !name[0]) return 0;
    sprintf(prefix, "%lu.", (unsigned long)number);
    sprintf(text, "%s%.31s", prefix, name);
    indent = (uint16_t)strlen(prefix) * 8U;
    height = UI_TextWidth(name, 16) > 224U - indent ? 36U : 16U;
    field = &ui_fields[id];
    if(field->valid && field->x == 8 && field->y == y && field->width == 224 &&
       field->height == height && field->mode == mode && field->color == LCD_YELLOW &&
       !strcmp(field->text, text)) return height;
    previous = ui_text_color; ui_text_color = LCD_YELLOW;
    if(field->valid && (field->y != y || field->height != height))
        UI_Fill(field->x, field->y, field->width, field->height, 0);
    UI_Fill(8, y, 224, height, selected);
    UI_TextLine(8, y, indent, prefix, 16, mode);
    next = UI_TextLine(8 + indent, y, 224 - indent, name, 16, mode);
    if(height > 16) UI_TextLine(8 + indent, y + 20, 224 - indent, next, 16, mode);
    ui_text_color = previous;
    strcpy(field->text, text);
    field->x = 8; field->y = y; field->width = 224; field->height = height;
    field->size = 16; field->mode = mode; field->color = LCD_YELLOW; field->valid = 1;
    return height;
}

/* 镜像列表项：三位号 + 空格 + 名称，固定16px单行；黄底黑字选中。
 * 与UI_NumberedItem共用ui_fields缓存，不分配新文字表或帧缓冲。 */
uint16_t UI_ImageItem(uint8_t id, uint16_t y, uint16_t number,
                      const char *name, uint8_t selected)
{
    char prefix[8], text[64];
    uint16_t indent, previous;
    uint8_t mode = selected ? 0 : 1;
    ui_field_t *field;
    if(id >= 16 || !name) return 0;
    sprintf(prefix, "%03u", (unsigned)number);
    sprintf(text, "%s %.31s", prefix, name);
    indent = 4U * 8U;
    field = &ui_fields[id];
    if(field->valid && field->x == 8 && field->y == y && field->width == 224 &&
       field->height == 16 && field->mode == mode && field->color == LCD_YELLOW &&
       !strcmp(field->text, text)) return 16;
    previous = ui_text_color; ui_text_color = LCD_YELLOW;
    if(field->valid && (field->y != y || field->height != 16))
        UI_Fill(field->x, field->y, field->width, field->height, 0);
    UI_Fill(8, y, 224, 16, selected);
    UI_TextLine(8, y, indent, prefix, 16, mode);
    UI_TextLine(8 + indent, y, 224 - indent, name, 16, mode);
    ui_text_color = previous;
    strcpy(field->text, text);
    field->x = 8; field->y = y; field->width = 224; field->height = 16;
    field->size = 16; field->mode = mode; field->color = LCD_YELLOW; field->valid = 1;
    return 16;
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
