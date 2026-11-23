/* 显示接口：应用默认240×320竖屏，中文16/32点，ASCII半宽；直接分块绘制，不维护整屏缓存。 */
#ifndef _BSP_OLED_H__
#define _BSP_OLED_H__
#include "main.h"
typedef struct { uint8_t oled_Read_addr, oled_Write_addr, Refresh_flag; } oled_TypeDef;
extern oled_TypeDef oled;
/* 相邻文字行之间留4像素；16像素文字的行起点相隔20像素。 */
#define UI_LINE_GAP 4U
/* 初始化显示（转调LCD_Init），保留原OLED接口名。
 * 返回 - 无 */
void OLED_Init(void);
/* 兼容旧OLED写字节接口，改用原生像素直绘后为空实现。
 * dat  - 原OLED数据字节（已忽略）
 * mode - 原OLED命令/数据选择（已忽略）
 * 返回 - 无 */
void OLED_WR_Byte(uint8_t dat, uint8_t mode);
/* 开显示（转调LCD_DisplayOn）。
 * 返回 - 无 */
void OLED_DisPlay_On(void);
/* 关显示（转调LCD_DisplayOff）。
 * 返回 - 无 */
void OLED_DisPlay_Off(void);
/* 全屏清黑，并使字段缓存与进度条状态失效。
 * 返回 - 无 */
void OLED_Clear(void);
/* 切换页面：只有页面号变化才清屏，同页刷新不擦屏。
 * page - 页面编号0~254；255为初始值
 * 返回 - 无 */
void UI_EnterPage(uint8_t page);
/* 带缓存的文字字段刷新：逐字比较，只重绘变化字符。
 * id     - 字段编号0~15，当前页内须唯一
 * x      - 左上角列坐标（原生像素）
 * y      - 左上角行坐标（原生像素）
 * width  - 字段宽度（像素）
 * height - 字段高度（像素），按size+UI_LINE_GAP逐行换行
 * text   - 要显示的文字，GBK编码，最长63字节
 * size   - 字号16或32
 * mode   - 1=白字黑底，0=黑字白底
 * 返回   - 无 */
void UI_Field(uint8_t id, uint16_t x, uint16_t y, uint16_t width, uint16_t height,
              const char *text, uint8_t size, uint8_t mode);
/* 彩色字段：参数与UI_Field相同；color为RGB565笔画色。
 * mode=1为彩字黑底，mode=0为黑字彩底。颜色变化也触发缓存更新。 */
void UI_FieldColor(uint8_t id, uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                   const char *text, uint8_t size, uint8_t mode, uint16_t color);
/* 清除一段缓存字段及其旧像素，适用于列表跨页/高度变化前；不影响其他字段。 */
void UI_ClearFields(uint8_t first, uint8_t count);
/* 224px宽编号列表项的实际文字高度（16/36），编号后紧接名称，续行对齐名称。 */
uint16_t UI_NumberedItemHeight(uint32_t number, const char *name);
/* 绘制x=8的编号列表项，返回实际高度；使用指定字段缓存，选中项为黄底黑字。 */
uint16_t UI_NumberedItem(uint8_t id, uint16_t y, uint32_t number,
                         const char *name, uint8_t selected);
/* 镜像列表项：固定16px单行，前缀为三位镜像号 000-099；选中黄底黑字。
 * id     - 字段缓存编号0~15
 * y      - 行顶坐标
 * number - 镜像号0~99
 * name   - 说明/固件名，超宽截断
 * selected - 非0选中
 * 返回   - 固定16 */
uint16_t UI_ImageItem(uint8_t id, uint16_t y, uint16_t number,
                      const char *name, uint8_t selected);
/* 彩色单行文本，参数与UI_Text相同；超出右边界截断。 */
void UI_TextColor(uint16_t x, uint16_t y, const char *text, uint8_t size,
                  uint8_t mode, uint16_t color);
/* 兼容旧OLED刷新接口，改为直绘后为空实现。
 * 返回 - 无 */
void OLED_Refresh(void);
/* 兼容旧OLED反显接口，转调LCD_SetInvert。
 * invert - 1=反显，0=正常
 * 返回   - 无 */
void OLED_ColorTurn(uint8_t invert);
/* 兼容旧OLED旋转接口，转调LCD_SetFlip；入参极性与LCD_SetFlip相反。
 * normal - 非0=恢复原方向，0=旋转180度
 * 返回   - 无 */
void OLED_DisplayTurn(uint8_t normal);
/* 计算混合汉字/ASCII文本的显示宽度。
 * s    - 以空字符结尾的GBK字符串
 * size - 字号16或32；汉字占size像素，ASCII占size/2像素
 * 返回 - 文本总宽度（像素）；参数非法时返回0 */
uint16_t UI_TextWidth(const char *s, uint8_t size);
/* 在一行限定宽度内显示文本，不自动换行。
 * x     - 起始列坐标（原生像素）
 * y     - 起始行坐标（原生像素）
 * width - 本行可用宽度（像素）
 * s     - 以空字符结尾的GBK字符串
 * size  - 字号16或32
 * mode  - 1=白字黑底，0=黑字白底
 * 返回  - 指向第一个未显示字符的指针 */
const char *UI_TextLine(uint16_t x, uint16_t y, uint16_t width, const char *s, uint8_t size, uint8_t mode);
/* 显示单行文本，超出右边界自动截断。
 * x    - 起始列坐标（原生像素）
 * y    - 起始行坐标（原生像素）
 * s    - 以空字符结尾的GBK字符串
 * size - 字号16或32
 * mode - 1=白字黑底，0=黑字白底
 * 返回 - 无 */
void UI_Text(uint16_t x, uint16_t y, const char *s, uint8_t size, uint8_t mode);
/* 在指定矩形内换行显示文字，超出高度后停止。
 * x      - 左上角列坐标（原生像素）
 * y      - 左上角行坐标（原生像素）
 * width  - 文本框宽度（像素）
 * height - 文本框高度（像素）
 * s      - 以空字符结尾的GBK字符串
 * size   - 字号16或32
 * mode   - 1=白字黑底，0=黑字白底
 * 返回   - 无 */
void UI_TextBox(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const char *s, uint8_t size, uint8_t mode);
/* 按屏幕像素填充矩形，不申请整屏显存。
 * x      - 左上角列坐标
 * y      - 左上角行坐标
 * width  - 宽度（像素）
 * height - 高度（像素）
 * on     - 1=白色，0=黑色
 * 返回   - 无 */
void UI_Fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t on);
/* 显示黄色状态小图标（18×8点阵放大两倍）。
 * x     - 左上角列坐标
 * y     - 左上角行坐标
 * index - 图标序号，对应font.h中的logo表；越界不绘制
 * 返回  - 无 */
void UI_Logo(uint16_t x, uint16_t y, uint8_t index);
/* 8x16竖闪电：visible非0画黄色闪电，0清黑；供充电和充满两种状态使用。 */
void UI_ChargingBolt(uint16_t x, uint16_t y, uint8_t visible);
/* 将128×64单色开机图标放大两倍显示。
 * data - 128×64点阵数据，列页式存储（每行128字节、共8页）
 * 返回 - 无 */
void UI_BootLogo(const uint8_t *data);
/* 绘制百分比进度条和数字，只补差量不整屏刷新。
 * y       - 进度条顶部的行坐标（原生像素）
 * percent - 进度百分比0~100，超过100按100处理
 * 返回    - 无 */
void UI_Progress(uint16_t y, uint8_t percent);
enum {
    UI_PROGRESS_IDLE = 0,
    UI_PROGRESS_RUNNING,
    UI_PROGRESS_FAILED,
    UI_PROGRESS_SUCCESS
};
/* 固定16像素高的单行状态进度条。运行时前缀百分比并按比例填黄；
 * 失败/成功整条填红/绿；待机空底。GBK按完整字符裁剪，不自动换行。
 * 文字在填充内黑色、填充外黄色，同内容零写屏；颜色/填充变化同步重画叠字。
 * OLED_Clear使缓存失效；x/y/width为原生像素，status允许NULL。 */
void UI_ProgressStatus(uint16_t x, uint16_t y, uint16_t width,
                       uint8_t percent, uint8_t state, const char *status);
/* 兼容旧接口：在128×64虚拟坐标系中画点，放大两倍显示。
 * row  - 虚拟行坐标0~63
 * col  - 虚拟列坐标0~127
 * on   - 1=白点，0=黑点
 * 返回 - 无 */
void OLED_DrawPoint(uint8_t row, uint8_t col, uint8_t on);
/* 兼容旧接口：在128×64虚拟坐标系中画直线（Bresenham算法）。
 * r1   - 起点行坐标
 * c1   - 起点列坐标
 * r2   - 终点行坐标
 * c2   - 终点列坐标
 * on   - 1=白线，0=黑线
 * 返回 - 无 */
void OLED_DrawLine(uint8_t r1, uint8_t c1, uint8_t r2, uint8_t c2, uint8_t on);
/* 兼容旧在线图表接口，内部换算成原生像素后转调UI_Text。
 * row  - 虚拟行坐标，换算为y = 32 + row*2
 * col  - 虚拟列坐标，换算为x = 8 + col*2
 * s    - 以空字符结尾的GBK字符串
 * size - 旧接口字号：16按32像素显示，其它按16像素显示
 * mode - 1=白字黑底，0=黑字白底
 * 返回 - 无 */
void OLED_ShowString(uint8_t row, uint8_t col, uint8_t *s, uint8_t size, uint8_t mode);
#endif
