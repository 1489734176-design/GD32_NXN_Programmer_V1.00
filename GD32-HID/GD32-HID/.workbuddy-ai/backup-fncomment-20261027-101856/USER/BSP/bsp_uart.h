#ifndef _BSP_UART_H__
#define _BSP_UART_H__

#include "main.h"
#include "cdc_acm_core.h"

#define UART_BUFF_MAX 128
#define UART_BUFF_BULK UART_BUFF_MAX/USB_CDC_TX_LEN

typedef struct
{
	uint8_t rx_buff[UART_BUFF_BULK][USB_CDC_TX_LEN];
	uint8_t rx_len;
	uint8_t rx_bulk;
	uint8_t in_bulk;
	uint8_t out_bulk;
}uart_TypeDef;

extern uart_TypeDef cdc_uart;

void uart_init(void);
void uart_tx(uint8_t *buff, uint8_t len);
void uart_tx_dma(uint8_t *buff, uint8_t len);

#endif

