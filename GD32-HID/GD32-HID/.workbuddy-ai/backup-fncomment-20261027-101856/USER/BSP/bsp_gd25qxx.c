/* 中文注释补充（2026-10-26）：外部Flash改用统一SPI0总线接口，与LCD共享硬件SPI，保留Flash读写及忙检测行为。 */
#include "bsp_gd25qxx.h"
#include "main.h"

gd25qxx_TypeDef gd25qxx;

/* 初始化共享SPI0总线并唤醒外部Flash，读取容量型号，必要时启用四字节地址。 */
void GD25QXX_Init(void)
{	
    uint8_t temp;
    SPI0_BusInit();
    GD25QXX_WAKEUP();
    gd25qxx.gd25qxx_type = GD25QXX_ReadID();	        
    if(gd25qxx.gd25qxx_type == GD25Q256)                
    {
        temp=GD25QXX_ReadSR(3);              
        if((temp & 0X01) == 0)			       
				{
						GD25QXX_CS_LOW;			        
						SPI_ReadWriteByte(GD25X_Enable4ByteAddr);
						GD25QXX_CS_HIGH;       		          
				}
    }

}  
/* 通过共享SPI0总线执行Flash双向单字节收发，保留原驱动接口。 */
uint8_t SPI_ReadWriteByte(uint8_t TxData)
{
    return SPI0_FlashExchange(TxData);
}

//读取GD25QXX的状态寄存器，GD25QXX一共有3个状态寄存器
//状态寄存器1：
//BIT7  6   5   4   3   2   1   0
//SPR   RV  TB BP2 BP1 BP0 WEL BUSY
//SPR:默认0,状态寄存器保护位,配合WP使用
//TB,BP2,BP1,BP0:FLASH区域写保护设置
//WEL:写使能锁定
//BUSY:忙标记位(1,忙;0,空闲)
//默认:0x00
//状态寄存器2：
//BIT7  6   5   4   3   2   1   0
//SUS   CMP LB3 LB2 LB1 (R) QE  SRP1
//状态寄存器3：
//BIT7      6    5    4   3   2   1   0
//HOLD/RST  DRV1 DRV0 (R) (R) WPS ADP ADS
//regno:状态寄存器号，范:1~3
//返回值:状态寄存器值
uint8_t GD25QXX_ReadSR(uint8_t regno)   
{  
	uint8_t byte=0,command=0; 
    switch(regno)
    {
        case 1:
            command=GD25X_ReadStatusReg1;    //读状态寄存器1指令
            break;
        case 2:
            command=GD25X_ReadStatusReg2;    //读状态寄存器2指令
            break;
        case 3:
            command=GD25X_ReadStatusReg3;    //读状态寄存器3指令
            break;
        default:
            command=GD25X_ReadStatusReg1;    
            break;
    }    
	GD25QXX_CS_LOW;                           //使能器件   
	SPI_ReadWriteByte(command);            //发送读取状态寄存器命令    
	byte=SPI_ReadWriteByte(0Xff);          //读取一个字节  
	GD25QXX_CS_HIGH;                           //取消片选     
	return byte;   
} 
//写GD25QXX状态寄存器
void GD25QXX_Write_SR(uint8_t regno,uint8_t sr)   
{   
    uint8_t command=0;
    switch(regno)
    {
        case 1:
            command=GD25X_WriteStatusReg1;    //写状态寄存器1指令
            break;
        case 2:
            command=GD25X_WriteStatusReg2;    //写状态寄存器2指令
            break;
        case 3:
            command=GD25X_WriteStatusReg3;    //写状态寄存器3指令
            break;
        default:
            command=GD25X_WriteStatusReg1;    
            break;
    }   
	GD25QXX_CS_LOW;                           //使能器件   
	SPI_ReadWriteByte(command);            //发送写取状态寄存器命令    
	SPI_ReadWriteByte(sr);                 //写入一个字节  
	GD25QXX_CS_HIGH;                           //取消片选     	      
}   
//GD25QXX写使能	
//将WEL置位   
void GD25QXX_Write_Enable(void)   
{
	GD25QXX_CS_LOW;                           //使能器件   
    SPI_ReadWriteByte(GD25X_WriteEnable);   //发送写使能  
	GD25QXX_CS_HIGH;                           //取消片选     	      
} 
//GD25QXX写禁止	
//将WEL清零  
void GD25QXX_Write_Disable(void)   
{  
	GD25QXX_CS_LOW;                           //使能器件   
    SPI_ReadWriteByte(GD25X_WriteDisable);  //发送写禁止指令    
	GD25QXX_CS_HIGH;                           //取消片选     	      
} 

//读取芯片ID
//返回值如下:				   
//0XC813,表示芯片型号为GD25Q80  
//0XC814,表示芯片型号为GD25Q16    
//0XC815,表示芯片型号为GD25Q32  
//0XC816,表示芯片型号为GD25Q64 
//0XC817,表示芯片型号为GD25Q128 	  
//0XC818,表示芯片型号为GD25Q256
uint16_t GD25QXX_ReadID(void)
{
	uint16_t Temp = 0;	  
	GD25QXX_CS_LOW;				    
	SPI_ReadWriteByte(0x90);//发送读取ID命令	    
	SPI_ReadWriteByte(0x00); 	    
	SPI_ReadWriteByte(0x00); 	    
	SPI_ReadWriteByte(0x00); 	 			   
	Temp|=SPI_ReadWriteByte(0xFF)<<8;  
	Temp|=SPI_ReadWriteByte(0xFF);	 
	GD25QXX_CS_HIGH;				    
	return Temp;
}   		    
//读取SPI FLASH  
//在指定地址开始读取指定长度的数据
//pBuffer:数据存储区
//ReadAddr:开始读取的地址(24bit)
//NumByteToRead:要读取的字节数(最大65535)
void GD25QXX_Read(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead)
{
    uint16_t i, count;
    /* 限制单次临界区长度，同时保持硬件SPI吞吐。 */
    while (NumByteToRead) {
        count = NumByteToRead > 256U ? 256U : NumByteToRead;
        GD25QXX_CS_LOW;
        SPI_ReadWriteByte(GD25X_ReadData);
        if (gd25qxx.gd25qxx_type == GD25Q256)
            SPI_ReadWriteByte((uint8_t)(ReadAddr >> 24));
        SPI_ReadWriteByte((uint8_t)(ReadAddr >> 16));
        SPI_ReadWriteByte((uint8_t)(ReadAddr >> 8));
        SPI_ReadWriteByte((uint8_t)ReadAddr);
        for (i = 0; i < count; i++) pBuffer[i] = SPI_ReadWriteByte(0xFF);
        GD25QXX_CS_HIGH;
        pBuffer += count;
        ReadAddr += count;
        NumByteToRead -= count;
    }
}  
//SPI在一页(0~65535)内写入少于256个字节的数据
//在指定地址开始写入最大256字节的数据
//pBuffer:数据存储区
//WriteAddr:开始写入的地址(24bit)
//NumByteToWrite:要写入的字节数(最大256),该数不应该超过该页的剩余字节数!!!	 
void GD25QXX_Write_Page(uint8_t* pBuffer,uint32_t WriteAddr,uint16_t NumByteToWrite)
{
 	uint16_t i;  
    GD25QXX_Write_Enable();                  //置位写使能(WEL) 
	GD25QXX_CS_LOW;                           //使能器件   
    SPI_ReadWriteByte(GD25X_PageProgram);   //发送写页命令   
    if(gd25qxx.gd25qxx_type==GD25Q256)                //如果是GD25Q256的话地址为4字节的，要发送最高8位
    {
        SPI_ReadWriteByte((uint8_t)((WriteAddr)>>24)); 
    }
    SPI_ReadWriteByte((uint8_t)((WriteAddr)>>16)); //发送24bit地址    
    SPI_ReadWriteByte((uint8_t)((WriteAddr)>>8));   
    SPI_ReadWriteByte((uint8_t)WriteAddr);   
    for(i=0;i<NumByteToWrite;i++)SPI_ReadWriteByte(pBuffer[i]);//循环写数  
	GD25QXX_CS_HIGH;                           //取消片选 
	GD25QXX_Wait_Busy();					   //等待写入结束
} 
//无检验写SPI FLASH 
//必须确保所写的地址范围内的数据全部为0XFF,否则在非0XFF处写入的数据将失败!
//具有自动换页功能 
//在指定地址开始写入指定长度的数据,但是要确保地址不越界!
//pBuffer:数据存储区
//WriteAddr:开始写入的地址(24bit)
//NumByteToWrite:要写入的字节数(最大65535)
//校验通过
void GD25QXX_Write_NoCheck(uint8_t* pBuffer,uint32_t WriteAddr,uint16_t NumByteToWrite)   
{ 			 		 
	uint16_t pageremain;	   
	pageremain=256-WriteAddr%256; //单页剩余的字节数		 	    
	if(NumByteToWrite<=pageremain)pageremain=NumByteToWrite;//不大于256个字节
	while(1)
	{	   
		GD25QXX_Write_Page(pBuffer,WriteAddr,pageremain);
		if(NumByteToWrite==pageremain)break;//写入结束了
	 	else //剩余待写字节数 > 本页剩余字节数
		{
			pBuffer+=pageremain;
			WriteAddr+=pageremain;	

			NumByteToWrite-=pageremain;			  //减去已经写入了的字节数
			if(NumByteToWrite>256)pageremain=256; //一次可以写入256个字节
			else pageremain=NumByteToWrite; 	  //不够256个字节了
		}
	};	    
} 
//写SPI FLASH  
//在指定地址开始写入指定长度的数据
//该函数带擦除操作!
//pBuffer:数据存储区
//WriteAddr:开始写入的地址(24bit)						
//NumByteToWrite:要写入的字节数(最大65535)   
uint8_t DATA_BUFFER[4096];		 
void GD25QXX_Write(uint8_t* pBuffer,uint32_t WriteAddr,uint16_t NumByteToWrite)   
{ 
	uint32_t secpos;
	uint16_t secoff;
	uint16_t secremain;	   
 	uint16_t i;    
	uint8_t * GD25QXX_BUF;	  
   	GD25QXX_BUF=DATA_BUFFER;	     
 	secpos=WriteAddr/4096;//扇区地址  
	secoff=WriteAddr%4096;//在扇区内的偏移
	secremain=4096-secoff;//扇区剩余空间大小   
 	//printf("ad:%X,nb:%X\r\n",WriteAddr,NumByteToWrite);//测试用
 	if(NumByteToWrite<=secremain)secremain=NumByteToWrite;//不大于4096个字节
	while(1) 
	{	
		GD25QXX_Read(GD25QXX_BUF,secpos*4096,4096);//读出整个扇区的内容
		for(i=0;i<secremain;i++)//校验数据
		{
			if(GD25QXX_BUF[secoff+i]!=0XFF)break;//需要擦除  	  
		}
		if(i<secremain)//需要擦除
		{
			GD25QXX_Erase_Sector(secpos);//擦除这个扇区
			for(i=0;i<secremain;i++)	   //复制
			{
				GD25QXX_BUF[i+secoff]=pBuffer[i];	  
			}
			GD25QXX_Write_NoCheck(GD25QXX_BUF,secpos*4096,4096);//写入整个扇区  

		}else GD25QXX_Write_NoCheck(pBuffer,WriteAddr,secremain);//写已经擦除了的,直接写入扇区剩余区间. 				   
		if(NumByteToWrite==secremain)break;//写入结束了
		else//写入未结束
		{
			secpos++;//扇区地址增1
			secoff=0;//偏移位置为0 	 

		   	pBuffer+=secremain;  //指针偏移
			WriteAddr+=secremain;//写地址偏移	   
		   	NumByteToWrite-=secremain;				//字节数递减
			if(NumByteToWrite>4096)secremain=4096;	//下一个扇区还是写不完
			else secremain=NumByteToWrite;			//下一个扇区可以写完了
		}	 
	};	 
}
//擦除整个芯片		  
//等待时间超长...
void GD25QXX_Erase_Chip(void)   
{                                   
    GD25QXX_Write_Enable();                  //置位写使能(WEL) 
    GD25QXX_Wait_Busy();   
  	GD25QXX_CS_LOW;                           //使能器件   
    SPI_ReadWriteByte(GD25X_ChipErase);        //发送片擦除命令  
	GD25QXX_CS_HIGH;                           //取消片选     	      
	GD25QXX_Wait_Busy();   				   //等待芯片擦除结束
}   
//擦除一个扇区
//Dst_Addr:扇区地址 根据实际容量设置
//擦除一个扇区的最少时间:150ms
void GD25QXX_Erase_Sector(uint32_t Dst_Addr)   
{  
	//监视falsh擦除情况,测试用   
 	//printf("fe:%x\r\n",Dst_Addr);	  
 	Dst_Addr*=4096;
    GD25QXX_Write_Enable();                  //置位写使能(WEL) 	 
    GD25QXX_Wait_Busy();   
  	GD25QXX_CS_LOW;                           //使能器件   
    SPI_ReadWriteByte(GD25X_SectorErase);   //发送扇区擦除指令 
    if(gd25qxx.gd25qxx_type==GD25Q256)                //如果是GD25Q256的话地址为4字节的，要发送最高8位
    {
        SPI_ReadWriteByte((uint8_t)((Dst_Addr)>>24)); 
    }
    SPI_ReadWriteByte((uint8_t)((Dst_Addr)>>16));  //发送24bit地址    
    SPI_ReadWriteByte((uint8_t)((Dst_Addr)>>8));   
    SPI_ReadWriteByte((uint8_t)Dst_Addr);  
	GD25QXX_CS_HIGH;                           //取消片选     	      
    GD25QXX_Wait_Busy();   				    //等待擦除完成
}  
//等待空闲
void GD25QXX_Wait_Busy(void)   
{   
	while((GD25QXX_ReadSR(1)&0x01)==0x01);   // 等待BUSY位清空
}  
//进入掉电模式
void GD25QXX_PowerDown(void)   
{ 
  	GD25QXX_CS_LOW;                           //使能器件   
    SPI_ReadWriteByte(GD25X_PowerDown);     //发送掉电命令  
		GD25QXX_CS_HIGH;                           //取消片选     	      
    vTaskDelay(5);                            //等待TPD  
    
}   
//唤醒
void GD25QXX_WAKEUP(void)   
{  
  	GD25QXX_CS_LOW;                               //使能器件   
    SPI_ReadWriteByte(GD25X_ReleasePowerDown);  //发送释放掉电命令0xAB    
		GD25QXX_CS_HIGH;                               //取消片选     	      
    vTaskDelay(5);                                //等待TRES1
}   

void GD25QXX_Delay(uint16_t us)
{
	while(us)
	{
		us--;
		__nop();
	}
}

