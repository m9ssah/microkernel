/* UART0 driver: receive by interrupt, transmit by polling.
*/

#include "uart.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/* ---- register map (TI LM3S6965 datasheet, UART chapter) ----------------- */

#define UART0_BASE 0x4000C000u
#define UART1_BASE 0x4000D000u

#define UART_REG(base, off) (*(volatile uint32_t *)((base) + (off)))

#define UART_O_DR    0x000u  /* data                                  */
#define UART_O_FR    0x018u  /* flags                                 */
#define UART_O_IBRD  0x024u  /* baud rate divisor, integer part       */
#define UART_O_FBRD  0x028u  /* baud rate divisor, fractional part    */
#define UART_O_LCRH  0x02Cu  /* line control                          */
#define UART_O_CTL   0x030u  /* control                               */
#define UART_O_IM    0x038u  /* interrupt mask (i.e. enable)          */
#define UART_O_ICR   0x044u  /* interrupt clear                       */

#define UART_FR_RXFE (1u << 4)  /* receive buffer empty  */
#define UART_FR_TXFF (1u << 5)  /* transmit buffer full  */

#define UART_LCRH_WLEN_8 (3u << 5)  /* 8 data bits          */
#define UART_LCRH_FEN    (1u << 4)  /* enable the 16-byte FIFOs */

#define UART_CTL_UARTEN (1u << 0)
#define UART_CTL_TXE    (1u << 8)
#define UART_CTL_RXE    (1u << 9)

#define UART_INT_RX (1u << 4)   /* receive interrupt, in IM/ICR */

/* System control: peripheral clock gating. */
#define SYSCTL_RCGC1 (*(volatile uint32_t *)0x400FE104u)
#define SYSCTL_RCGC1_UART0 (1u << 0)
#define SYSCTL_RCGC1_UART1 (1u << 1)

/* Cortex-M3 NVIC. Priority registers are byte-addressable, one byte per IRQ. */
#define NVIC_ISER0 (*(volatile uint32_t *)0xE000E100u)
#define NVIC_IPR   ((volatile uint8_t *)0xE000E400u)

#define IRQ_UART0 5 
#define UART_IBRD_115200 27u
#define UART_FBRD_115200 8u

// state
static QueueHandle_t g_rx_queue;
static volatile uint32_t g_rx_dropped;

QueueHandle_t uart0_rx_queue(void) { return g_rx_queue; }
uint32_t uart0_rx_dropped(void) { return g_rx_dropped; }

// init
void uart0_init(void)
{
    g_rx_queue = xQueueCreate(UART_RX_QUEUE_LEN, sizeof(uint8_t));
    configASSERT(g_rx_queue != NULL);

    SYSCTL_RCGC1 |= SYSCTL_RCGC1_UART0;
    (void)SYSCTL_RCGC1;
    (void)SYSCTL_RCGC1;
    UART_REG(UART0_BASE, UART_O_CTL) = 0;

    UART_REG(UART0_BASE, UART_O_IBRD) = UART_IBRD_115200;
    UART_REG(UART0_BASE, UART_O_FBRD) = UART_FBRD_115200;


    UART_REG(UART0_BASE, UART_O_LCRH) = UART_LCRH_WLEN_8;

    UART_REG(UART0_BASE, UART_O_ICR) = UART_INT_RX;  // clear anything stale
    UART_REG(UART0_BASE, UART_O_IM) = UART_INT_RX;

    NVIC_IPR[IRQ_UART0] = (uint8_t)configMAX_SYSCALL_INTERRUPT_PRIORITY;
    NVIC_ISER0 = (1u << IRQ_UART0);

    UART_REG(UART0_BASE, UART_O_CTL) =
        UART_CTL_UARTEN | UART_CTL_TXE | UART_CTL_RXE;
}

void UART0_Handler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    while ((UART_REG(UART0_BASE, UART_O_FR) & UART_FR_RXFE) == 0)
    {
        uint8_t byte = (uint8_t)(UART_REG(UART0_BASE, UART_O_DR) & 0xFFu);

        if (xQueueSendFromISR(g_rx_queue, &byte, &xHigherPriorityTaskWoken) != pdTRUE)
        {
            g_rx_dropped++;
        }
    }


    UART_REG(UART0_BASE, UART_O_ICR) = UART_INT_RX;


    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void uart0_write(const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;

    for (size_t i = 0; i < len; i++)
    {
        while (UART_REG(UART0_BASE, UART_O_FR) & UART_FR_TXFF)
        {
            /* wait for room */
        }
        UART_REG(UART0_BASE, UART_O_DR) = p[i];
    }
}


void dbg_init(void)
{
    SYSCTL_RCGC1 |= SYSCTL_RCGC1_UART1;
    (void)SYSCTL_RCGC1;
    (void)SYSCTL_RCGC1;

    UART_REG(UART1_BASE, UART_O_CTL) = 0;
    UART_REG(UART1_BASE, UART_O_IBRD) = UART_IBRD_115200;
    UART_REG(UART1_BASE, UART_O_FBRD) = UART_FBRD_115200;

    UART_REG(UART1_BASE, UART_O_LCRH) = UART_LCRH_WLEN_8 | UART_LCRH_FEN;
    UART_REG(UART1_BASE, UART_O_CTL) = UART_CTL_UARTEN | UART_CTL_TXE;
}

void dbg_puts(const char *s)
{
    while (*s)
    {
        while (UART_REG(UART1_BASE, UART_O_FR) & UART_FR_TXFF)
        {
        }
        UART_REG(UART1_BASE, UART_O_DR) = (uint8_t)*s++;
    }
}

void dbg_putu(uint32_t v)
{
    char tmp[11];
    int i = 0;

    if (v == 0)
    {
        dbg_puts("0");
        return;
    }
    while (v > 0 && i < (int)sizeof(tmp))
    {
        tmp[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i-- > 0)
    {
        while (UART_REG(UART1_BASE, UART_O_FR) & UART_FR_TXFF)
        {
        }
        UART_REG(UART1_BASE, UART_O_DR) = (uint8_t)tmp[i];
    }
}
