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

#define i2c_speed 400000

void i2c_init(void);
void i2c_config(void);
void i2c_bus_reset(void);
void i2c_write(uint8_t* buffer, uint16_t len);
uint8_t i2c_dma_write(uint8_t* buffer, uint8_t write_address, uint16_t len);


#endif


