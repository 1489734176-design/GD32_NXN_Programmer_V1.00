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

/* 初始化USART2：115200-8-N-1，使能收发与接收中断。
 * 返回 - 无 */
void uart_init(void);
/* 以查询方式阻塞发送一段数据。
 * buff - 待发送数据缓冲区
 * len  - 发送字节数，0~255
 * 返回 - 无 */
void uart_tx(uint8_t *buff, uint8_t len);
/* 用DMA0通道1发送一段数据，搬完在中断里重新挂接收。
 * buff - 待发送数据缓冲区（函数返回后仍会被DMA读取，须保持有效）
 * len  - 发送字节数，0~255
 * 返回 - 无 */
void uart_tx_dma(uint8_t *buff, uint8_t len);

#endif

