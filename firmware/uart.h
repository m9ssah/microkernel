#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stddef.h>

#include "FreeRTOS.h"
#include "queue.h"

#define UART_RX_QUEUE_LEN 256


void uart0_init(void);

QueueHandle_t uart0_rx_queue(void);

void uart0_write(const void *buf, size_t len);

uint32_t uart0_rx_dropped(void);

void dbg_init(void);
void dbg_puts(const char *s);
void dbg_putu(uint32_t v);

#endif
