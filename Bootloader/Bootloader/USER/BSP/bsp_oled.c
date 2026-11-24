#include "bsp_oled.h"
#include "font.h"
#include "gd32f10x_i2c.h"
#include "bsp_lcd.h"

uint8_t OLED_GRAM[8][128];

oled_TypeDef oled = 
{
    .oled_Read_addr = 0X79,
    .oled_Write_addr = 0X78,
		.Refresh_flag = 0,
};

void OLED_Init(void)
{
    LCD_Init();
    OLED_Clear();
}

//发送一个字节
//mode:数据/命令标志 0,表示命令;1,表示数据;
void OLED_WR_Byte(uint8_t dat, uint8_t mode)
{
    /* Legacy SSD1306 raw commands have no equivalent in the LCD bridge. */
    (void)dat;
    (void)mode;
}


//开启OLED显示 
void OLED_DisPlay_On(void) { LCD_DisplayOn(); }

//关闭OLED显示 
void OLED_DisPlay_Off(void) { LCD_DisplayOff(); }

//清屏函数
void OLED_Clear(void)
{
	uint8_t i,j;
	for(i=0;i<8;i++)
	{
	    for(j=0;j<128;j++)
	    {
			 OLED_GRAM[i][j]=0;//清除所有数据
		}
    }
//	OLED_Refresh();//更新显示
}


//更新显存到OLED	
void OLED_Refresh(void) { LCD_BlitGRAM((const uint8_t (*)[128])OLED_GRAM); }
//反显函数
void OLED_ColorTurn(uint8_t i) { LCD_SetInvert(i); }

//屏幕旋转180度
void OLED_DisplayTurn(uint8_t i)
{
    /* Legacy value 1 was the normal board mounting. */
    LCD_SetFlip((uint8_t)(i == 0));
}
//画点 
//x:0~63
//y:0~127
//t:1 填充 0,清空	
void OLED_DrawPoint(uint8_t x,uint8_t y,uint8_t t)
{
	uint8_t i,m,n;
	i=x/8;
	m=x%8;
	n=1<<m;
	if(t){OLED_GRAM[i][y]|=n;}
	else
	{
		OLED_GRAM[i][y]=~OLED_GRAM[i][y];
		OLED_GRAM[i][y]|=n;
		OLED_GRAM[i][y]=~OLED_GRAM[i][y];
	}
}

//画线
//x1,y1:起点坐标
//x2,y2:结束坐标
void OLED_DrawLine(uint8_t x1,uint8_t y1,uint8_t x2,uint8_t y2,uint8_t mode)
{
	uint16_t t; 
	int xerr=0,yerr=0,delta_x,delta_y,distance;
	int incx,incy,uRow,uCol;
	delta_x=x2-x1; //计算坐标增量 
	delta_y=y2-y1;
	uRow=x1;//画线起点坐标
	uCol=y1;
	if(delta_x>0)incx=1; //设置单步方向 
	else if (delta_x==0)incx=0;//垂直线 
	else {incx=-1;delta_x=-delta_x;}
	if(delta_y>0)incy=1;
	else if (delta_y==0)incy=0;//水平线 
	else {incy=-1;delta_y=-delta_x;}
	if(delta_x>delta_y)distance=delta_x; //选取基本增量坐标轴 
	else distance=delta_y;
	for(t=0;t<distance+1;t++)
	{
		OLED_DrawPoint(uRow,uCol,mode);//画点
		xerr+=delta_x;
		yerr+=delta_y;
		if(xerr>distance)
		{
			xerr-=distance;
			uRow+=incx;
		}
		if(yerr>distance)
		{
			yerr-=distance;
			uCol+=incy;
		}
	}
}
//x,y:圆心坐标
//r:圆的半径
void OLED_DrawCircle(uint8_t x,uint8_t y,uint8_t r)
{
	int a, b,num;
    a = 0;
    b = r;
    while(2 * b * b >= r * r)      
    {
        OLED_DrawPoint(x + a, y - b,1);
        OLED_DrawPoint(x - a, y - b,1);
        OLED_DrawPoint(x - a, y + b,1);
        OLED_DrawPoint(x + a, y + b,1);
 
        OLED_DrawPoint(x + b, y + a,1);
        OLED_DrawPoint(x + b, y - a,1);
        OLED_DrawPoint(x - b, y - a,1);
        OLED_DrawPoint(x - b, y + a,1);
        
        a++;
        num = (a * a + b * b) - r*r;//计算画的点离圆心的距离
        if(num > 0)
        {
            b--;
            a--;
        }
    }
}

//x,y：起点坐标
//sizex,sizey,图片长宽
//BMP[]：要写入的图片数组
//mode:0,反色显示;1,正常显示
void OLED_ShowPicture(uint8_t x,uint8_t y,uint8_t sizex,uint8_t sizey,uint8_t BMP[],uint8_t mode)
{
	uint16_t j=0;
	uint8_t i,n,temp,m;
	uint8_t x0=x,y0=y;
	sizey=sizey/8+((sizey%8)?1:0);
	for(n=0;n<sizey;n++)
	{
		for(i=0;i<sizex;i++)
		{
			temp=BMP[j];
			j++;
			for(m=0;m<8;m++)
			{
				if(temp&0x01)OLED_DrawPoint(x,y,mode);
				else OLED_DrawPoint(x,y,!mode);
				temp>>=1;
				y++;
			}
			x++;
			if((x-x0)==sizex)
			{
				x=x0;
				y0=y0+8;
			}
			y=y0;
    }
	}
}
//m^n
uint32_t OLED_Pow(uint8_t m,uint8_t n)
{
	uint32_t result=1;
	while(n--)
	{
	  result*=m;
	}
	return result;
}
//在指定位置显示一个字符,包括部分字符
//x:0~127
//y:0~63
//size1:选择字体 6x8/6x12/8x16/12x24
//mode:0,反色显示;1,正常显示
void OLED_ShowChar(uint8_t x,uint8_t y,uint8_t chr,uint8_t size1,uint8_t mode)
{
	uint8_t i,m,temp,size2,chr1;
	uint8_t x0=x,y0=y;
	if(size1==8)size2=6;
	else size2=(size1/8+((size1%8)?1:0))*(size1/2);  //得到字体一个字符对应点阵集所占的字节数
	chr1=chr-' ';  //计算偏移后的值
	for(i=0;i<size2;i++)
	{
		if(size1==8)
			  {temp=asc2_0806[chr1][i];} //调用0806字体
		else if(size1==12)
        {temp=asc2_1206[chr1][i];} //调用1206字体
		else if(size1==16)
        {temp=asc2_1608[chr1][i];} //调用1608字体
		else return;
		for(m=0;m<8;m++)
		{
			if(temp&0x01)OLED_DrawPoint(x,y,mode);
			else OLED_DrawPoint(x,y,!mode);
			temp>>=1;
			x++;
		}
		y++;
		if((size1!=8)&&((y-y0)==size1/2))
		{y=y0;x0=x0+8;}
		x=x0;
  }
}


//显示字符串
//x,y:起点坐标  
//size1:字体大小 
//*chr:字符串起始地址 
//mode:0,反色显示;1,正常显示
void OLED_ShowString(uint8_t x,uint8_t y,uint8_t *chr,uint8_t size1,uint8_t mode)
{
	while((*chr>=' ')&&(*chr<='~'))//判断是不是非法字符!
	{
		OLED_ShowChar(x,y,*chr,size1,mode);
		if(size1==8)y+=6;
		else y+=size1/2;
		chr++;
  }
}

//显示数字
//x,y :起点坐标
//num :要显示的数字
//len :数字的位数
//size:字体大小
//mode:0,反色显示;1,正常显示
void OLED_ShowNum(uint8_t x,uint8_t y,uint32_t num,uint8_t len,uint8_t size1,uint8_t mode)
{
	uint8_t t,temp,m=0;
	if(size1==8)m=2;
	for(t=0;t<len;t++)
	{
		temp=(num/OLED_Pow(10,len-t-1))%10;
			if(temp==0)
			{
				OLED_ShowChar(x,y+(size1/2+m)*t,'0',size1,mode);
      }
			else 
			{
			  OLED_ShowChar(x,y+(size1/2+m)*t,temp+'0',size1,mode);
			}
  }
}

//显示数字
//x,y :起点坐标
//index :要显示保存的汉字
//len :汉字的位数
//mode:0,反色显示;1,正常显示
void OLED_ShowChinese(uint8_t x, uint8_t y, uint8_t index, uint8_t len, uint8_t mode)
{
	uint8_t i,j,z;
	uint8_t x1 = x, x2 = x, y1 = y, y2 = y;
	uint8_t temp;
	
	for(i = 0; i < len; i++)
	{
		for(j = 0; j < 32; j++)
		{
			switch(index)
			{
				case 1: temp = string_1[i][j];break;
				case 2: temp = string_2[i][j];break;
				case 3: temp = string_3[i][j];break;
				case 4: temp = string_4[i][j];break;
			}
			
			for(z = 0; z < 8; z++)
			{
				if(temp&0x01)OLED_DrawPoint(x1,y1,mode);
				else OLED_DrawPoint(x1,y1,!mode);
				temp>>=1;
				x1++;
			}
			if(j == 15)  x2 += 8, y1 = y2;
			x1 = x2;
			y1++;
			
		}
		x2 = x;
		y2 = y + 16*(i + 1);
	}
	
}








