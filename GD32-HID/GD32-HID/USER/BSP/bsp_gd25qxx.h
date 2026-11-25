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

/* 通过共享SPI0总线与Flash双向收发一个字节。
 * TxData - 要发送的字节
 * 返回   - 同时收到的字节；总线故障时返回0xFF */
uint8_t SPI_ReadWriteByte(uint8_t TxData);
/* 初始化共享SPI0总线并唤醒外部Flash，读取型号，必要时启用四字节地址。
 * 返回 - 无 */
void GD25QXX_Init(void);
/* 读取Flash芯片ID，用于识别容量型号。
 * 返回 - 0xC813=GD25Q80，0xC814=GD25Q16，0xC815=GD25Q32，
 *        0xC816=GD25Q64，0xC817=GD25Q128，0xC818=GD25Q256 */
uint16_t  GD25QXX_ReadID(void);
/* 读取状态寄存器（共3个，位定义见bsp_gd25qxx.c）。
 * regno - 寄存器号1~3；其它值按1处理
 * 返回  - 该寄存器的8位值 */
uint8_t GD25QXX_ReadSR(uint8_t regno);
/* 使能4字节地址模式（声明保留，当前实现由GD25QXX_Init内部完成）。
 * 返回 - 无 */
void GD25QXX_4ByteAddr_Enable(void);
/* 写状态寄存器（不发写使能，调用前须先置WEL）。
 * regno - 寄存器号1~3；其它值按1处理
 * sr    - 要写入的8位值
 * 返回  - 无 */
void GD25QXX_Write_SR(uint8_t regno,uint8_t sr);
/* 写使能：把状态寄存器1的WEL位置1，擦写前必须调用。
 * 返回 - 无 */
void GD25QXX_Write_Enable(void);
/* 写禁止：把状态寄存器1的WEL位清零。
 * 返回 - 无 */
void GD25QXX_Write_Disable(void);
/* 不带擦除检查的写入，自动跨页拆分。
 * pBuffer        - 源数据缓冲区
 * WriteAddr      - 起始地址（24位有效），目标区域须已为0xFF
 * NumByteToWrite - 写入字节数，最大65535
 * 返回           - 无 */
void GD25QXX_Write_NoCheck(uint8_t* pBuffer,uint32_t WriteAddr,uint16_t NumByteToWrite);
/* 从指定地址连续读取数据，内部按256字节分块释放总线。
 * pBuffer       - 接收数据缓冲区
 * ReadAddr      - 起始地址；GD25Q256为32位地址
 * NumByteToRead - 读取字节数，最大65535
 * 返回          - 无 */
void GD25QXX_Read(uint8_t* pBuffer,uint32_t ReadAddr,uint16_t NumByteToRead);
/* 带擦除的写入：按4KB扇区读回-判断-擦除-改写，可跨扇区。
 * pBuffer        - 源数据缓冲区
 * WriteAddr      - 起始地址（24位有效）
 * NumByteToWrite - 写入字节数，最大65535
 * 返回           - 无 */
void GD25QXX_Write(uint8_t* pBuffer,uint32_t WriteAddr,uint16_t NumByteToWrite);
/* 整片擦除，耗时数十秒且阻塞当前任务。
 * 返回 - 无 */
void GD25QXX_Erase_Chip(void);
/* 擦除一个4KB扇区，最少耗时约150ms。
 * Dst_Addr - 扇区号（不是字节地址），内部乘以4096换算
 * 返回     - 无 */
void GD25QXX_Erase_Sector(uint32_t Dst_Addr);
/* 轮询状态寄存器1的BUSY位直到空闲，无超时保护。
 * 返回 - 无 */
uint8_t GD25QXX_Wait_Busy(void);
/* 进入掉电低功耗模式，之后须用GD25QXX_WAKEUP唤醒。
 * 返回 - 无 */
void GD25QXX_PowerDown(void);
/* 发送释放掉电命令（0xAB）唤醒Flash。
 * 返回 - 无 */
void GD25QXX_WAKEUP(void);

/* 粗略微秒级忙等，按空转__nop计数，未做精确标定。
 * us   - 延时长度，单位微秒
 * 返回 - 无 */
void GD25QXX_Delay(uint16_t us);



#endif


