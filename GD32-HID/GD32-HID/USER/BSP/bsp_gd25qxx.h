/* 中文注释补充（2026-10-26）：Flash片选接口委托共享SPI0总线，确保LCD和Flash访问互斥。 */
#include "bsp_spi0_bus.h"
#ifndef _BSP_GD25QXX_H__
#define _BSP_GD25QXX_H__

#include "main.h"


#define GD25Q80 	0XC813 	
#define GD25Q16 	0XC814
#define GD25Q32 	0XC815
#define GD25Q64 	0XC816
#define GD25Q128	0XC817
#define GD25Q256  0XC818

#define GD25X_WriteEnable					0x06 
#define GD25X_WriteDisable				0x04 
#define GD25X_ReadStatusReg1			0x05 
#define GD25X_ReadStatusReg2			0x35 
#define GD25X_ReadStatusReg3			0x15 
#define GD25X_WriteStatusReg1    	0x01 
#define GD25X_WriteStatusReg2    	0x31 
#define GD25X_WriteStatusReg3    	0x11 
#define GD25X_ReadData						0x03 
#define GD25X_FastReadData				0x0B 
#define GD25X_FastReadDual				0x3B 
#define GD25X_PageProgram					0x02 
#define GD25X_BlockErase					0xD8 
#define GD25X_SectorErase					0x20 
#define GD25X_ChipErase						0xC7 
#define GD25X_PowerDown						0xB9 
#define GD25X_ReleasePowerDown		0xAB 
#define GD25X_DeviceID						0xAB 
#define GD25X_ManufactDeviceID		0x90 
#define GD25X_JedecDeviceID				0x9F 
#define GD25X_Enable4ByteAddr    	0xB7
#define GD25X_Exit4ByteAddr      	0xE9


#define GD25QXX_CS_HIGH SPI0_FlashRelease()
#define GD25QXX_CS_LOW SPI0_FlashSelect()


typedef struct
{
    uint16_t gd25qxx_type;
		

}gd25qxx_TypeDef;

extern gd25qxx_TypeDef gd25qxx;
extern uint8_t DATA_BUFFER[4096];

/* 通过共享SPI0总线执行Flash双向单字节收发，保留原驱动接口。 */
uint8_t SPI_ReadWriteByte(uint8_t TxData);
/* 初始化共享SPI0总线并唤醒外部Flash，读取容量型号，必要时启用四字节地址。 */
void GD25QXX_Init(void);
uint16_t  GD25QXX_ReadID(void);  	    		                                								//读取FLASH ID
uint8_t GD25QXX_ReadSR(uint8_t regno);                                           					//读取状态寄存器 
void GD25QXX_4ByteAddr_Enable(void);                                             					//使能4字节地址模式
void GD25QXX_Write_SR(uint8_t regno,uint8_t sr);                                 					//写状态寄存器
void GD25QXX_Write_Enable(void);  		                                        						//写使能 
void GD25QXX_Write_Disable(void);		                                        							//写保护
void GD25QXX_Write_NoCheck(uint8_t* pBuffer,uint32_t WriteAddr,uint16_t NumByteToWrite);
void GD25QXX_Read(uint8_t* pBuffer,uint32_t ReadAddr,uint16_t NumByteToRead);            	//读取flash
void GD25QXX_Write(uint8_t* pBuffer,uint32_t WriteAddr,uint16_t NumByteToWrite);         	//写入flash
void GD25QXX_Erase_Chip(void);    	  	                                                	//整片擦除
void GD25QXX_Erase_Sector(uint32_t Dst_Addr);	                                        		//扇区擦除
void GD25QXX_Wait_Busy(void);           	                                                //等待空闲
void GD25QXX_PowerDown(void);        	                                                		//进入掉电模式
void GD25QXX_WAKEUP(void);				                                                				//唤醒

void GD25QXX_Delay(uint16_t us);



#endif


