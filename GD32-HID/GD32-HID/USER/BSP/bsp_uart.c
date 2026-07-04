#include "bsp_uart.h"
#include "main.h"
 
#define USART2_DATA_ADDRESS    ((uint32_t)&USART_DATA(USART2))

uart_TypeDef cdc_uart = 
{
	.rx_len = 0,
	.in_bulk = 0,
	.out_bulk = 0,
	.rx_bulk = 0
};

void uart_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_USART2);

    /* connect port to USARTx_Tx */
    gpio_init(UART_TX_GPIO_Port, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, UART_TX_Pin);

    /* connect port to USARTx_Rx */
    gpio_init(UART_RX_GPIO_Port, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, UART_RX_Pin);

    /* USART configure */
    usart_deinit(USART2);
    usart_word_length_set(USART2, USART_WL_8BIT);
    usart_stop_bit_set(USART2, USART_STB_1BIT);
    usart_baudrate_set(USART2, 115200);
    usart_receive_config(USART2, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART2, USART_TRANSMIT_ENABLE);
	
    usart_enable(USART2);	
		
		nvic_irq_enable(USART2_IRQn, 4, 0);
		usart_interrupt_enable(USART2, USART_INT_RBNE);
		
}


void uart_tx(uint8_t *buff, uint8_t len)
{
		uint16_t i;
		for(i = 0; i < len; i++)
		{
			usart_data_transmit(USART2, (uint8_t)buff[i]);
			while(RESET == usart_flag_get(USART2, USART_FLAG_TBE));
		}
}

void USART2_IRQHandler(void)
{
	uint8_t data;
  if(RESET != usart_interrupt_flag_get(USART2, USART_INT_FLAG_RBNE))
	{
		usart_interrupt_flag_clear(USART2, USART_INT_FLAG_RBNE);
    data = (uint8_t)usart_data_receive(USART2);

		if(cdc_uart.rx_bulk < UART_BUFF_BULK)
		{
			cdc_uart.rx_buff[cdc_uart.in_bulk][cdc_uart.rx_len] = data;
			cdc_uart.rx_len ++;
			if(cdc_uart.rx_len == USB_CDC_TX_LEN)
			{
				cdc_uart.rx_len = 0;
				cdc_uart.rx_bulk ++;
				cdc_uart.in_bulk ++;
				if(cdc_uart.in_bulk == UART_BUFF_BULK)
					cdc_uart.in_bulk = 0;
			}
		}
		
		if(!time.time_start_flag)
		{
			timer_enable(TIMER0);
			time.time_start_flag = 1;
		}
		
	}
}

void uart_tx_dma(uint8_t *buff, uint8_t len)
{
		dma_parameter_struct dma_init_struct;

    dma_deinit(DMA0, DMA_CH1);
		dma_struct_para_init(&dma_init_struct);
    dma_init_struct.direction = DMA_MEMORY_TO_PERIPHERAL;
    dma_init_struct.memory_addr = (uint32_t)buff;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.memory_width = DMA_MEMORY_WIDTH_8BIT;
    dma_init_struct.number = len;
    dma_init_struct.periph_addr = USART2_DATA_ADDRESS;
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_width = DMA_PERIPHERAL_WIDTH_8BIT;
    dma_init_struct.priority = DMA_PRIORITY_ULTRA_HIGH;
    dma_init(DMA0, DMA_CH1, &dma_init_struct);
    /* configure DMA mode */
    dma_circulation_disable(DMA0, DMA_CH1);
		dma_memory_to_memory_disable(DMA0, DMA_CH1);
		
    usart_dma_transmit_config(USART2, USART_DENT_ENABLE);
    dma_interrupt_enable(DMA0, DMA_CH1, DMA_INT_FTF);
    dma_channel_enable(DMA0, DMA_CH1);
}

void DMA0_Channel1_IRQHandler(void)
{
		usb_cdc_handler *cdc = (usb_cdc_handler *)usb_hid.class_data[CDC_COM_INTERFACE];
    if(dma_interrupt_flag_get(DMA0, DMA_CH1, DMA_INT_FLAG_FTF))
		{     
			dma_interrupt_flag_clear(DMA0, DMA_CH1, DMA_INT_FLAG_G);
			dma_interrupt_disable(DMA0, DMA_CH1, DMA_INT_FTF);
			usbd_ep_recev(&usb_hid, CDC_OUT_EP, (uint8_t*)(cdc->data), USB_CDC_RX_LEN);
    }
}





