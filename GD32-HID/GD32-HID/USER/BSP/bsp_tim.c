#include "bsp_tim.h"
#include "gd32f10x_timer.h"

time_TypeDef time =
{
	.time_start_flag = 0
};

void time_init(void)
{
	timer_parameter_struct timer_initpara;
	
	rcu_periph_clock_enable(RCU_TIMER0);
	
	timer_initpara.prescaler         = 96 - 1;
  timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
  timer_initpara.counterdirection  = TIMER_COUNTER_UP;
  timer_initpara.period            = 1000 - 1;
  timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
  timer_initpara.repetitioncounter = 0;
  timer_init(TIMER0,&timer_initpara);
	
	timer_disable(TIMER0);
	nvic_irq_enable(TIMER0_UP_IRQn, 3, 0);
	timer_interrupt_enable(TIMER0,TIMER_INT_UP);
	
}

void memset_s(uint8_t *d, uint8_t c, uint8_t len)
{
	uint8_t i;
	for(i = 0; i < len; i++)
	{
		d[i] = c;
	}
}

void TIMER0_UP_IRQHandler(void)
{
	if(RESET != timer_interrupt_flag_get(TIMER0, TIMER_INT_FLAG_UP))
	{
		timer_interrupt_flag_clear(TIMER0,TIMER_INT_UP);
		if(cdc_uart.rx_bulk > 0)
		{
			usbd_ep_send(&usb_hid, CDC_IN_EP, cdc_uart.rx_buff[cdc_uart.out_bulk], USB_CDC_TX_LEN);
			memset_s(cdc_uart.rx_buff[cdc_uart.out_bulk], 0, USB_CDC_TX_LEN);
			cdc_uart.rx_bulk --;
			cdc_uart.out_bulk ++;
			if(cdc_uart.out_bulk == UART_BUFF_BULK)
				cdc_uart.out_bulk = 0;
		}
		else
		{
			if(cdc_uart.rx_len)
			{
				usbd_ep_send(&usb_hid, CDC_IN_EP, cdc_uart.rx_buff[cdc_uart.out_bulk], cdc_uart.rx_len);
				memset_s(cdc_uart.rx_buff[cdc_uart.out_bulk], 0, cdc_uart.rx_len);
				cdc_uart.rx_len = 0;
			}
			timer_disable(TIMER0);
			time.time_start_flag = 0;
		}
	}
	
}
