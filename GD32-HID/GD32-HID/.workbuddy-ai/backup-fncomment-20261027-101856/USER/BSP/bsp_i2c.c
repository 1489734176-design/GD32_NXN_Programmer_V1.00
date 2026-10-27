#include "bsp_i2c.h"
#include "bsp_oled.h"
#include "main.h"

void i2c_init(void)
{
	rcu_periph_clock_enable(RCU_AF);
	rcu_periph_clock_enable(RCU_GPIOB);
	rcu_periph_clock_enable(RCU_I2C0);
	
	gpio_init(IIC_SCL_GPIO_Port, GPIO_MODE_AF_OD, GPIO_OSPEED_50MHZ, IIC_SCL_Pin);
	gpio_init(IIC_SDA_GPIO_Port, GPIO_MODE_AF_OD, GPIO_OSPEED_50MHZ, IIC_SDA_Pin);
	gpio_pin_remap_config(GPIO_I2C0_REMAP, ENABLE);
	
	/* I2C时钟配置 */
  i2c_clock_config(I2C0, i2c_speed, I2C_DTCY_2);
  /* I2C地址配置（7位地址，主机模式） */
  i2c_mode_addr_config(I2C0, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS, oled.oled_Write_addr);
  /* 使能I2C0 */
  i2c_enable(I2C0);
  /* 使能应答 */
  i2c_ack_config(I2C0, I2C_ACK_ENABLE);
}

void i2c_write(uint8_t* buffer, uint16_t len)
{
		uint8_t i;
//		taskENTER_CRITICAL();
	  /* 等待I2C总线空闲 */
    while(i2c_flag_get(I2C0, I2C_FLAG_I2CBSY));
    /* 发送起始条件 */
    i2c_start_on_bus(I2C0);
    /* 等待SBSEND（起始已发送）置位 */
    while(!i2c_flag_get(I2C0, I2C_FLAG_SBSEND));
    /* 发送从机地址 */
    i2c_master_addressing(I2C0, oled.oled_Write_addr, I2C_TRANSMITTER);
    /* 等待ADDSEND（地址已发送）置位 */
    while(!i2c_flag_get(I2C0, I2C_FLAG_ADDSEND));
    /* 清除ADDSEND标志 */
    i2c_flag_clear(I2C0, I2C_FLAG_ADDSEND);
    /* 等待发送数据缓冲区空 */
    while(!i2c_flag_get(I2C0, I2C_FLAG_TBE));

    for(i=0; i<len; i++){
        /* 发送一个字节数据 */
        i2c_data_transmit(I2C0, buffer[i]);
        /* 等待TBE（发送缓冲区空）置位 */
        while(!i2c_flag_get(I2C0, I2C_FLAG_TBE));
    }
    /* 发送停止条件 */
    i2c_stop_on_bus(I2C0);
    while(I2C_CTL0(I2C0)&0x0200);
//		taskEXIT_CRITICAL();
}

uint8_t i2c_dma_write(uint8_t* buffer, uint8_t write_address, uint16_t len)
{
		dma_parameter_struct dma_init_struct;
    
    uint8_t state = I2C_START;
    uint16_t timeout = 0;
    uint8_t i2c_timeout_flag = 0;
    
    while(!(i2c_timeout_flag)){
        switch(state){
        case I2C_START:
            /* 只有总线空闲时主机才发送起始信号 */
            while(i2c_flag_get(I2C0, I2C_FLAG_I2CBSY)&&(timeout < I2C_TIME_OUT)){
                timeout++;
            }
            if(timeout < I2C_TIME_OUT){
                i2c_start_on_bus(I2C0);
                timeout = 0;
                state = I2C_SEND_ADDRESS;
            }else{
                i2c_bus_reset();
                timeout = 0;
                state = I2C_START;
            }
            break;
        case I2C_SEND_ADDRESS:
            /* 主机已成功发送起始信号 */
            while((! i2c_flag_get(I2C0, I2C_FLAG_SBSEND))&&(timeout < I2C_TIME_OUT)){
                timeout++;
            }
            if(timeout < I2C_TIME_OUT){
                i2c_master_addressing(I2C0, oled.oled_Write_addr, I2C_TRANSMITTER);
                timeout = 0;
                state = I2C_CLEAR_ADDRESS_FLAG;
            }else{
                timeout = 0;
                state = I2C_START;
            }
            break;        
        case I2C_CLEAR_ADDRESS_FLAG:
            /* 地址标志置位表示从机已回ACK */
            while((! i2c_flag_get(I2C0, I2C_FLAG_ADDSEND))&&(timeout < I2C_TIME_OUT)){
                timeout++; 
            }
            if(timeout < I2C_TIME_OUT){
                i2c_flag_clear(I2C0, I2C_FLAG_ADDSEND);
                timeout = 0;
                state = I2C_TRANSMIT_DATA;
            }else{
                timeout = 0;
                state = I2C_START;
            }
            break;
        case I2C_TRANSMIT_DATA:
            /* 等待发送数据缓冲区空 */
            while((! i2c_flag_get(I2C0, I2C_FLAG_TBE))&&(timeout < I2C_TIME_OUT)){
                timeout++;
            }
            if(timeout < I2C_TIME_OUT){
                /* 先发一个字节的从机内部地址 */
                i2c_data_transmit(I2C0, write_address);
                timeout = 0;
            }else{
                timeout = 0;
                state = I2C_START;
            }
            /* 等待BTC（字节传输完成）置位 */
            while(!i2c_flag_get(I2C0, I2C_FLAG_BTC));
            dma_deinit(DMA0, DMA_CH5);
            dma_init_struct.direction = DMA_MEMORY_TO_PERIPHERAL;
            dma_init_struct.memory_addr = (uint32_t)buffer;
            dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
            dma_init_struct.memory_width = DMA_MEMORY_WIDTH_8BIT;
            dma_init_struct.number = len;
            dma_init_struct.periph_addr = (uint32_t)&I2C_DATA(I2C0);
            dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
            dma_init_struct.periph_width = DMA_PERIPHERAL_WIDTH_8BIT;
            dma_init_struct.priority = DMA_PRIORITY_ULTRA_HIGH;
            dma_init(DMA0, DMA_CH5, &dma_init_struct);
            /* 使能I2C0的DMA请求 */
            i2c_dma_enable(I2C0, I2C_DMA_ON);
            /* 使能DMA0通道5 */
            dma_channel_enable(DMA0, DMA_CH5);
            /* 等待DMA传输完成 */
            while(!dma_flag_get(DMA0, DMA_CH5, DMA_FLAG_FTF));
            /* 等待BTC置位 */
            while((!i2c_flag_get(I2C0, I2C_FLAG_BTC)) && (timeout < I2C_TIME_OUT)){
                timeout++;
            }
            if(timeout < I2C_TIME_OUT){
                timeout = 0;
                state = I2C_STOP;
            }else{
                timeout = 0;
                state = I2C_START;
            }
            break;
        case I2C_STOP:
            /* 发送停止条件 */
            i2c_stop_on_bus(I2C0);
            /* 主机已成功发送停止信号 */
            while((I2C_CTL0(I2C0)&0x0200)&&(timeout < I2C_TIME_OUT)){
                timeout++;
            }
            if(timeout < I2C_TIME_OUT){
                timeout = 0;
                state = I2C_END;
                i2c_timeout_flag = I2C_OK;
            }else{
                timeout = 0;
                state = I2C_START;
            }
            break;
        default:
            state = I2C_START;
            i2c_timeout_flag = I2C_OK;
            timeout = 0;
            break;
        }
    }
    return I2C_END;

}

void i2c_bus_reset(void)
{
    i2c_deinit(I2C0);
    /* 把SDA/SCL临时切成普通GPIO输出 */
    GPIO_BC(GPIOB) |= IIC_SCL_Pin|IIC_SDA_Pin;
    gpio_init(GPIOB,GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, IIC_SCL_Pin|IIC_SDA_Pin);
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    GPIO_BOP(GPIOB) |= IIC_SCL_Pin;
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    GPIO_BOP(GPIOB) |= IIC_SDA_Pin;
    /* PB6复用为I2C0_SCL */
    /* PB7复用为I2C0_SDA */
    gpio_init(GPIOB, GPIO_MODE_AF_OD, GPIO_OSPEED_50MHZ, IIC_SCL_Pin|IIC_SDA_Pin);
    /* 重新配置I2C0接口 */
    i2c_init();
}







