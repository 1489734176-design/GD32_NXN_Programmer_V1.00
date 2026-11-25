/* 中文注释补充（2026-10-26）：外部Flash改用统一SPI0总线接口，与LCD共享硬件SPI，保留Flash读写及忙检测行为。 */
#include "bsp_gd25qxx.h"
#include "main.h"

gd25qxx_TypeDef gd25qxx;

/* 初始化共享SPI0总线并唤醒外部Flash，读取型号，必要时启用四字节地址。
 * GD25Q256容量超过16MB，需先读状态寄存器3确认ADS位并发送使能四字节地址命令。
 * 返回 - 无（型号写入全局gd25qxx.gd25qxx_type） */
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

/* 通过共享SPI0总线与Flash双向收发一个字节，保留原驱动接口。
 * TxData - 要发送的字节
 * 返回   - 同时收到的字节；总线故障时返回0xFF */
uint8_t SPI_ReadWriteByte(uint8_t TxData)
{
    return SPI0_FlashExchange(TxData);
}

/* 读取GD25QXX状态寄存器（共3个）。
 * 寄存器1：BIT7 SPR / 6 RV / 5 TB / 4 BP2 / 3 BP1 / 2 BP0 / 1 WEL / 0 BUSY，
 *   BP2~BP0为区域写保护，WEL为写使能锁定，BUSY=1表示忙、0表示空闲，默认0x00。
 * 寄存器2：BIT7 SUS / 6 CMP / 5 LB3 / 4 LB2 / 3 LB1 / 2 保留 / 1 QE / 0 SRP1。
 * 寄存器3：BIT7 HOLD/RST / 6 DRV1 / 5 DRV0 / 4~3 保留 / 2 WPS / 1 ADP / 0 ADS。
 * regno - 状态寄存器号，1~3；其它值按寄存器1处理
 * 返回  - 该寄存器的8位值 */
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

/* 写GD25QXX状态寄存器（本函数不发写使能，调用前须先置WEL）。
 * regno - 状态寄存器号，1~3；其它值按寄存器1处理
 * sr    - 要写入的8位值
 * 返回  - 无 */
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

/* Flash写使能：把状态寄存器1的WEL位置1，擦写前必须先调用。
 * 返回 - 无 */
void GD25QXX_Write_Enable(void)   
{
	GD25QXX_CS_LOW;                           //使能器件   
    SPI_ReadWriteByte(GD25X_WriteEnable);   //发送写使能  
	GD25QXX_CS_HIGH;                           //取消片选     	      
} 

/* Flash写禁止：把状态寄存器1的WEL位清零。
 * 返回 - 无 */
void GD25QXX_Write_Disable(void)   
{  
	GD25QXX_CS_LOW;                           //使能器件   
    SPI_ReadWriteByte(GD25X_WriteDisable);  //发送写禁止指令    
	GD25QXX_CS_HIGH;                           //取消片选     	      
} 

/* 读取芯片ID，用于识别容量型号。
 * 返回值 - 0xC813=GD25Q80，0xC814=GD25Q16，0xC815=GD25Q32，
 *          0xC816=GD25Q64，0xC817=GD25Q128，0xC818=GD25Q256 */
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

/* 从指定地址开始连续读取指定长度的数据，内部按256字节分块释放总线。
 * pBuffer        - 接收数据缓冲区，容量不小于NumByteToRead
 * ReadAddr       - 起始地址；GD25Q256为32位地址，其余型号只用低24位
 * NumByteToRead  - 要读取的字节数，最大65535
 * 返回           - 无 */
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

/* 在一页内写入数据，写完后等待芯片内部编程结束。
 * pBuffer        - 源数据缓冲区
 * WriteAddr      - 起始地址；GD25Q256为32位地址，其余型号只用低24位
 * NumByteToWrite - 写入字节数，最大256，且不得超过该页剩余字节数
 * 返回           - 无 */
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

/* 不带擦除检查的写入，自动跨页拆分。
 * 必须确保目标区域已全部为0xFF，否则非0xFF处的数据写不进去。
 * pBuffer        - 源数据缓冲区
 * WriteAddr      - 起始地址（24位有效），须保证不越界
 * NumByteToWrite - 写入字节数，最大65535
 * 返回           - 无 */
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

/* 4KB扇区暂存区也用于读取program_target_t算法头，须满足Cortex-M3的LDRD字对齐。 */
__align(4) uint8_t DATA_BUFFER[4096];
/* 带擦除的写入：按4KB扇区读回-判断-擦除-改写，可跨扇区。
 * pBuffer        - 源数据缓冲区
 * WriteAddr      - 起始地址（24位有效）
 * NumByteToWrite - 写入字节数，最大65535
 * 返回           - 无（擦写耗时长，调用方需注意任务调度） */
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

/* 整片擦除，等待时间很长（数十秒级），会阻塞当前任务。
 * 返回 - 无 */
void GD25QXX_Erase_Chip(void)   
{                                   
    GD25QXX_Write_Enable();                  //置位写使能(WEL) 
    GD25QXX_Wait_Busy();   
  	GD25QXX_CS_LOW;                           //使能器件   
    SPI_ReadWriteByte(GD25X_ChipErase);        //发送片擦除命令  
	GD25QXX_CS_HIGH;                           //取消片选     	      
	GD25QXX_Wait_Busy();   				   //等待芯片擦除结束
}   

/* 擦除一个4KB扇区，最少耗时约150ms。
 * Dst_Addr - 扇区号（不是字节地址），函数内部乘以4096换算
 * 返回     - 无 */
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

/* 轮询状态寄存器1的BUSY位，直到芯片空闲；带超时，避免SPI/Flash异常时主任务永久卡死。
 * 返回 - 1=空闲，0=超时 */
uint8_t GD25QXX_Wait_Busy(void)
{
	uint32_t timeout = 200000U;
	while((GD25QXX_ReadSR(1)&0x01U)==0x01U)
	{
		if(--timeout == 0U) return 0U;
	}
	return 1U;
}  

/* 发送掉电命令使Flash进入低功耗，之后须用GD25QXX_WAKEUP唤醒。
 * 返回 - 无（内部延时5ms等待TPD） */
void GD25QXX_PowerDown(void)   
{ 
  	GD25QXX_CS_LOW;                           //使能器件   
    SPI_ReadWriteByte(GD25X_PowerDown);     //发送掉电命令  
		GD25QXX_CS_HIGH;                           //取消片选     	      
    vTaskDelay(5);                            //等待TPD  
    
}   

/* 发送释放掉电命令（0xAB）唤醒Flash。
 * 返回 - 无（内部延时5ms等待TRES1） */
void GD25QXX_WAKEUP(void)   
{  
  	GD25QXX_CS_LOW;                               //使能器件   
    SPI_ReadWriteByte(GD25X_ReleasePowerDown);  //发送释放掉电命令0xAB    
		GD25QXX_CS_HIGH;                               //取消片选     	      
    vTaskDelay(5);                                //等待TRES1
}   

/* 粗略微秒级忙等，按空转__nop计数，未做精确标定。
 * us   - 延时长度，单位微秒
 * 返回 - 无 */
void GD25QXX_Delay(uint16_t us)
{
	while(us)
	{
		us--;
		__nop();
	}
}

