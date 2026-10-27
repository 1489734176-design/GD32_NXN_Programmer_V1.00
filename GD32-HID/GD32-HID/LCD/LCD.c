#include "platform.h"
#include "LCD.h"
#include "LCD_Font.h"
#include "LCD_Chinese.h"

/*控制引脚*/
#define LCD_DC_PORT			GPIOA
#define LCD_DC_PIN			GPIO_Pin_0
#define LCD_CS_PORT			GPIOA
#define LCD_CS_PIN			GPIO_Pin_4
#define LCD_RES_PORT		GPIOA
#define LCD_RES_PIN			GPIO_Pin_15

#define LCD_DC(x)			GPIO_WriteBit(LCD_DC_PORT, LCD_DC_PIN, (BitAction)(x))
#define LCD_CS(x)			GPIO_WriteBit(LCD_CS_PORT, LCD_CS_PIN, (BitAction)(x))
#define LCD_RES(x)			GPIO_WriteBit(LCD_RES_PORT, LCD_RES_PIN, (BitAction)(x))

/*SPI时钟分频*/
#define LCD_SPI_PRESCALER	SPI_BaudratePrescaler_4

/*颜色显示*/
#define LCD_INVERT			1
#define LCD_BGR				0

/*往SPI发送缓冲区写一个字节*/
#define LCD_SPI_WRITE(Data)	do { while (SPI1->CSTAT & SPI_FLAG_TXFULL); SPI1->TXREG = (Data); } while (0)

/* 等待SPI1把发送缓冲区里的数据全部发完（TXEPT置位）。
 * 返回 - 无（死等，无超时保护） */
static void LCD_SPI_Wait(void)
{
	while (SPI_GetFlagStatus(SPI1, SPI_FLAG_TXEPT) == RESET);
}

/* 写一条命令：切换DC前后都等待发送完成，结束后DC恢复为数据模式。
 * Command - 要发送的ST7789命令码
 * 返回    - 无（调用方须自行拉低片选） */
static void LCD_WriteCommand(uint8_t Command)
{
	LCD_SPI_Wait();
	LCD_DC(0);
	LCD_SPI_WRITE(Command);
	LCD_SPI_Wait();
	LCD_DC(1);
}

/* 往SPI发送缓冲区写一个数据字节，不等待发送完成。
 * Data - 要发送的字节
 * 返回 - 无 */
static void LCD_WriteData(uint8_t Data)
{
	LCD_SPI_WRITE(Data);
}

/* 设置矩形显示窗口并启动显存写入（发0x2A/0x2B/0x2C）。
 * X0 - 窗口左上角列坐标
 * Y0 - 窗口左上角行坐标
 * X1 - 窗口右下角列坐标（含）
 * Y1 - 窗口右下角行坐标（含）
 * 返回 - 无（调用方须自行拉低片选） */
static void LCD_SetWindow(uint16_t X0, uint16_t Y0, uint16_t X1, uint16_t Y1)
{
	LCD_WriteCommand(0x2A);			//列地址
	LCD_WriteData(X0 >> 8);
	LCD_WriteData(X0 & 0xFF);
	LCD_WriteData(X1 >> 8);
	LCD_WriteData(X1 & 0xFF);
	LCD_WriteCommand(0x2B);			//行地址
	LCD_WriteData(Y0 >> 8);
	LCD_WriteData(Y0 & 0xFF);
	LCD_WriteData(Y1 >> 8);
	LCD_WriteData(Y1 & 0xFF);
	LCD_WriteCommand(0x2C);			//开始写显存
}

/* 初始化LCD：配置DC/CS/RES控制引脚与SPI1（主机、8位、模式0、只发送），
 * 硬件复位后依次下发ST7789初始化命令，最后清屏并开显示。
 * 返回 - 无 */
void LCD_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	SPI_InitTypeDef SPI_InitStructure;
	uint8_t Madctl;

	RCC_AHBPeriphClockCmd(RCC_AHBPERIPH_GPIOA, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1PERIPH_SPI1, ENABLE);

	/*控制引脚*/
	LCD_CS(1);
	LCD_DC(1);
	LCD_RES(1);
	GPIO_StructInit(&GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_High;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin = LCD_DC_PIN;
	GPIO_Init(LCD_DC_PORT, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin = LCD_CS_PIN;
	GPIO_Init(LCD_CS_PORT, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin = LCD_RES_PIN;
	GPIO_Init(LCD_RES_PORT, &GPIO_InitStructure);

	/*SPI引脚：PA5 = SPI1_SCK，PA6 = SPI1_MOSI，复用功能AF0*/
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource5, GPIO_AF_0);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource6, GPIO_AF_0);
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	/*SPI1：主机、8位、模式0（空闲低电平，第1个边沿采样）、高位在前、只发送*/
	SPI_StructInit(&SPI_InitStructure);
	SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
	SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
	SPI_InitStructure.SPI_DataWidth = 8;
	SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
	SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
	SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
	SPI_InitStructure.SPI_BaudRatePrescaler = LCD_SPI_PRESCALER;
	SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
	SPI_Init(SPI1, &SPI_InitStructure);
	SPI_BiDirectionalLineConfig(SPI1, SPI_Enable_TX);
	SPI_CSInternalSelected(SPI1, ENABLE);		//片选用PA4控制
	SPI_Cmd(SPI1, ENABLE);

	/*硬件复位*/
	LCD_RES(0);
	PLATFORM_DelayMS(20);
	LCD_RES(1);
	PLATFORM_DelayMS(120);

	LCD_CS(0);

	LCD_WriteCommand(0x11);			//退出睡眠
	PLATFORM_DelayMS(120);

	/*显示方向：MY(0x80)、MX(0x40)、MV(0x20，行列交换)，RGB/BGR(0x08)*/
#if (LCD_DIRECTION == 0)
	Madctl = 0x00;					//竖屏
#elif (LCD_DIRECTION == 1)
	Madctl = 0x60;					//横屏：MX + MV
#elif (LCD_DIRECTION == 2)
	Madctl = 0xC0;					//竖屏旋转180度：MY + MX
#else
	Madctl = 0xA0;					//横屏旋转180度：MY + MV
#endif
#if LCD_BGR
	Madctl |= 0x08;
#endif
	LCD_WriteCommand(0x36);
	LCD_WriteData(Madctl);

	LCD_WriteCommand(0x3A);			//像素格式：16位RGB565
	LCD_WriteData(0x05);

	LCD_WriteCommand(0xB2);			//Porch设置
	LCD_WriteData(0x0C);
	LCD_WriteData(0x0C);
	LCD_WriteData(0x00);
	LCD_WriteData(0x33);
	LCD_WriteData(0x33);

	LCD_WriteCommand(0xB7);			//Gate控制
	LCD_WriteData(0x35);

	LCD_WriteCommand(0xBB);			//VCOM
	LCD_WriteData(0x1F);

	LCD_WriteCommand(0xC0);			//LCM控制
	LCD_WriteData(0x2C);

	LCD_WriteCommand(0xC2);			//VDV、VRH由命令设置
	LCD_WriteData(0x01);

	LCD_WriteCommand(0xC3);			//VRH
	LCD_WriteData(0x12);

	LCD_WriteCommand(0xC4);			//VDV
	LCD_WriteData(0x20);

	LCD_WriteCommand(0xC6);			//帧率60Hz
	LCD_WriteData(0x0F);

	LCD_WriteCommand(0xD0);			//电源控制
	LCD_WriteData(0xA4);
	LCD_WriteData(0xA1);

	LCD_WriteCommand(0xE0);			//正极性Gamma
	LCD_WriteData(0xD0);
	LCD_WriteData(0x08);
	LCD_WriteData(0x11);
	LCD_WriteData(0x08);
	LCD_WriteData(0x0C);
	LCD_WriteData(0x15);
	LCD_WriteData(0x39);
	LCD_WriteData(0x33);
	LCD_WriteData(0x50);
	LCD_WriteData(0x36);
	LCD_WriteData(0x13);
	LCD_WriteData(0x14);
	LCD_WriteData(0x29);
	LCD_WriteData(0x2D);

	LCD_WriteCommand(0xE1);			//负极性Gamma
	LCD_WriteData(0xD0);
	LCD_WriteData(0x08);
	LCD_WriteData(0x10);
	LCD_WriteData(0x08);
	LCD_WriteData(0x06);
	LCD_WriteData(0x06);
	LCD_WriteData(0x39);
	LCD_WriteData(0x44);
	LCD_WriteData(0x51);
	LCD_WriteData(0x0B);
	LCD_WriteData(0x16);
	LCD_WriteData(0x14);
	LCD_WriteData(0x2F);
	LCD_WriteData(0x31);

	LCD_WriteCommand(LCD_INVERT ? 0x21 : 0x20);		//反色开/关

	LCD_SPI_Wait();
	LCD_CS(1);

	LCD_Clear(LCD_BLACK);			//先清屏再开显示

	LCD_CS(0);
	LCD_WriteCommand(0x29);			//开显示
	LCD_SPI_Wait();
	LCD_CS(1);
}

/* 填充矩形区域，超出屏幕的部分自动裁掉。
 * X      - 左上角列坐标，>=LCD_WIDTH时直接返回
 * Y      - 左上角行坐标，>=LCD_HEIGHT时直接返回
 * Width  - 宽度（像素），为0时直接返回
 * Height - 高度（像素），为0时直接返回
 * Color  - RGB565颜色值
 * 返回   - 无 */
void LCD_Fill(uint16_t X, uint16_t Y, uint16_t Width, uint16_t Height, uint16_t Color)
{
	uint32_t i;
	uint8_t High = Color >> 8;
	uint8_t Low = Color & 0xFF;

	if ((X >= LCD_WIDTH) || (Y >= LCD_HEIGHT) || (Width == 0) || (Height == 0))
	{
		return;
	}
	if (X + Width > LCD_WIDTH)			//超出屏幕的部分裁掉
	{
		Width = LCD_WIDTH - X;
	}
	if (Y + Height > LCD_HEIGHT)
	{
		Height = LCD_HEIGHT - Y;
	}

	LCD_CS(0);
	LCD_SetWindow(X, Y, X + Width - 1, Y + Height - 1);
	for (i = (uint32_t)Width * Height; i > 0; i--)
	{
		LCD_SPI_WRITE(High);
		LCD_SPI_WRITE(Low);
	}
	LCD_SPI_Wait();
	LCD_CS(1);
}

/* 全屏填充一种颜色。
 * Color - RGB565颜色值
 * 返回  - 无 */
void LCD_Clear(uint16_t Color)
{
	LCD_Fill(0, 0, LCD_WIDTH, LCD_HEIGHT, Color);
}

/* 画一个像素点，复用矩形填充的边界检查。
 * X     - 列坐标
 * Y     - 行坐标
 * Color - RGB565颜色值
 * 返回  - 无 */
void LCD_DrawPoint(uint16_t X, uint16_t Y, uint16_t Color)
{
	LCD_Fill(X, Y, 1, 1, Color);
}

/* 显示一个8×16 ASCII字符，按Size整数倍放大。
 * X         - 左上角列坐标，放不下整字时不显示
 * Y         - 左上角行坐标，放不下整字时不显示
 * Char      - 字符，范围0x20~0x7F；越界按空格处理
 * Color     - 字的颜色，RGB565
 * BackColor - 背景色，RGB565
 * Size      - 放大倍数，1=8×16，2=16×32，3=24×48……；传0按1处理
 * 返回      - 无 */
void LCD_ShowChar(uint16_t X, uint16_t Y, char Char, uint16_t Color, uint16_t BackColor, uint8_t Size)
{
	const uint8_t *Font;
	uint8_t Row, Col, i, j, Bits;
	uint16_t Pixel;

	if (((uint8_t)Char < ' ') || ((uint8_t)Char > 0x7F))
	{
		Char = ' ';
	}
	if (Size == 0)
	{
		Size = 1;
	}
	if ((X + 8 * Size > LCD_WIDTH) || (Y + 16 * Size > LCD_HEIGHT))		//超出屏幕不显示
	{
		return;
	}
	Font = LCD_F8x16[Char - ' '];

	LCD_CS(0);
	LCD_SetWindow(X, Y, X + 8 * Size - 1, Y + 16 * Size - 1);
	for (Row = 0; Row < 16; Row++)
	{
		for (i = 0; i < Size; i++)			//每一行重复Size次（纵向放大）
		{
			for (Col = 0; Col < 8; Col++)
			{
				Bits = (Row < 8) ? (Font[Col] >> Row) : (Font[Col + 8] >> (Row - 8));
				Pixel = (Bits & 0x01) ? Color : BackColor;
				for (j = 0; j < Size; j++)	//每个点重复Size次（横向放大）
				{
					LCD_SPI_WRITE(Pixel >> 8);
					LCD_SPI_WRITE(Pixel & 0xFF);
				}
			}
		}
	}
	LCD_SPI_Wait();
	LCD_CS(1);
}

/* 显示一个32×32点阵汉字，字库中没有的汉字填背景色。
 * X         - 左上角列坐标，放不下整字时不显示
 * Y         - 左上角行坐标，放不下整字时不显示
 * Code      - 汉字的GBK内码，如“长”为0xB3A4
 * Color     - 字的颜色，RGB565
 * BackColor - 背景色，RGB565
 * 返回      - 无 */
void LCD_ShowChinese(uint16_t X, uint16_t Y, uint16_t Code, uint16_t Color, uint16_t BackColor)
{
	const uint8_t *Font = 0;
	uint8_t i, Bit;
	uint16_t Pixel;

	if ((X + LCD_CN_SIZE > LCD_WIDTH) || (Y + LCD_CN_SIZE > LCD_HEIGHT))	//超出屏幕不显示
	{
		return;
	}
	for (i = 0; i < sizeof(LCD_F32x32) / sizeof(LCD_F32x32[0]); i++)
	{
		if (LCD_F32x32[i].Code == Code)
		{
			Font = LCD_F32x32[i].Data;
			break;
		}
	}
	if (Font == 0)
	{
		LCD_Fill(X, Y, LCD_CN_SIZE, LCD_CN_SIZE, BackColor);
		return;
	}

	LCD_CS(0);
	LCD_SetWindow(X, Y, X + LCD_CN_SIZE - 1, Y + LCD_CN_SIZE - 1);
	for (i = 0; i < LCD_CN_SIZE * LCD_CN_SIZE / 8; i++)
	{
		for (Bit = 0x80; Bit != 0; Bit >>= 1)
		{
			Pixel = (Font[i] & Bit) ? Color : BackColor;
			LCD_SPI_WRITE(Pixel >> 8);
			LCD_SPI_WRITE(Pixel & 0xFF);
		}
	}
	LCD_SPI_Wait();
	LCD_CS(1);
}

/* 显示字符串，超出屏幕右边的部分不显示。
 * 字符串中的GBK汉字（首字节>=0x81）按32×32显示，占两个Size=2字符的宽度。
 * X         - 起始列坐标
 * Y         - 起始行坐标
 * String    - 以空字符结尾的字符串，GBK编码
 * Color     - 字的颜色，RGB565
 * BackColor - 背景色，RGB565
 * Size      - ASCII放大倍数；传0按1处理
 * 返回      - 无 */
void LCD_ShowString(uint16_t X, uint16_t Y, char *String, uint16_t Color, uint16_t BackColor, uint8_t Size)
{
	if (Size == 0)
	{
		Size = 1;
	}
	while (*String)
	{
		if (((uint8_t)String[0] >= 0x81) && (String[1] != '\0'))		//GBK汉字，两个字节
		{
			LCD_ShowChinese(X, Y, ((uint16_t)(uint8_t)String[0] << 8) | (uint8_t)String[1], Color, BackColor);
			String += 2;
			X += LCD_CN_SIZE;
		}
		else
		{
			LCD_ShowChar(X, Y, *String++, Color, BackColor, Size);
			X += 8 * Size;
		}
	}
}

/* 显示十进制正整数，位数不足时前面补0。
 * X         - 起始列坐标
 * Y         - 起始行坐标
 * Number    - 要显示的数字，超出位数的高位被舍弃
 * Length    - 显示位数，1~10；超过10按10处理
 * Color     - 字的颜色，RGB565
 * BackColor - 背景色，RGB565
 * Size      - ASCII放大倍数；传0按1处理
 * 返回      - 无 */
void LCD_ShowNum(uint16_t X, uint16_t Y, uint32_t Number, uint8_t Length, uint16_t Color, uint16_t BackColor, uint8_t Size)
{
	char String[11];
	uint8_t i;

	if (Length > 10)
	{
		Length = 10;
	}
	String[Length] = '\0';
	for (i = Length; i > 0; i--)
	{
		String[i - 1] = '0' + Number % 10;
		Number /= 10;
	}
	LCD_ShowString(X, Y, String, Color, BackColor, Size);
}
