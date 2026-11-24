#include "bsp_uart.h"
#include "main.h"
 


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
		
		nvic_irq_enable(USART2_IRQn, 8, 0);
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




