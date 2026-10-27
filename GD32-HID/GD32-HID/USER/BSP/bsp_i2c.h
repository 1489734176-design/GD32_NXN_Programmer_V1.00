#ifndef __BSP_I2C_
#define __BSP_I2C_

#include "bsp_oled.h"
#include "main.h"

typedef enum
{
    I2C_START = 0,
    I2C_SEND_ADDRESS,
    I2C_CLEAR_ADDRESS_FLAG,
    I2C_TRANSMIT_DATA,
    I2C_STOP,
}i2c_process_enum;

#define I2C_TIME_OUT     (uint16_t)(5000)
#define EEP_FIRST_PAGE  0x00

#define I2C_OK      1
#define I2C_FAIL    0
#define I2C_END     1

#define i2c_speed 1000000

/* 初始化I2C0主机：PB6/PB7重映射为SCL/SDA，配置速率与OLED从机地址。
 * 返回 - 无 */
void i2c_init(void);
/* 预留接口：当前实现未提供该函数，仅保留声明以兼容旧调用。
 * 返回 - 无 */
void i2c_config(void);
/* 总线异常恢复：复位I2C0，把SCL/SDA临时切成普通输出拉高释放总线后重新初始化。
 * 返回 - 无 */
void i2c_bus_reset(void);
/* 以查询方式向OLED连续写入一段数据（阻塞发送，无超时保护）。
 * buffer - 待发送数据缓冲区
 * len    - 发送字节数
 * 返回   - 无 */
void i2c_write(uint8_t* buffer, uint16_t len);
/* 用DMA把数据写入OLED：先发从机内部地址，再由DMA0通道5搬运数据。
 * buffer        - 待发送数据缓冲区
 * write_address - OLED内部寄存器/命令地址，先于数据发出
 * len           - 数据字节数
 * 返回          - I2C_END（流程结束）；成功失败均返回该值 */
uint8_t i2c_dma_write(uint8_t* buffer, uint8_t write_address, uint16_t len);


#endif


