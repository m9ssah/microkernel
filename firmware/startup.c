/* Cortex-M3 startup: vector table + reset handler. */
#include <stdint.h>


extern uint32_t _sidata;   /* .data's initial values, stored in FLASH   */
extern uint32_t _sdata;    /* .data start in SRAM                        */
extern uint32_t _edata;    /* .data end   in SRAM                        */
extern uint32_t _sbss;     /* .bss  start in SRAM                        */
extern uint32_t _ebss;     /* .bss  end   in SRAM                        */
extern uint32_t _estack;   /* top of SRAM = initial stack pointer        */

int main(void);

void vPortSVCHandler(void);
void xPortPendSVHandler(void);
void xPortSysTickHandler(void);

void UART0_Handler(void);

void Reset_Handler(void);

void Default_Handler(void)
{
    for (;;)
    {
    }
}

#define WEAK_ALIAS(name) void name(void) __attribute__((weak, alias("Default_Handler")))
WEAK_ALIAS(NMI_Handler);
WEAK_ALIAS(HardFault_Handler);
WEAK_ALIAS(MemManage_Handler);
WEAK_ALIAS(BusFault_Handler);
WEAK_ALIAS(UsageFault_Handler);
WEAK_ALIAS(DebugMon_Handler);

typedef void (*vector_t)(void);

/* Placed at 0x00000000 by the .isr_vector rule in lm3s6965.ld. */

__attribute__((section(".isr_vector"), used))
const vector_t g_pfnVectors[] = {
    (vector_t)(&_estack),   /*  0: initial stack pointer                 */
    Reset_Handler,          /*  1: reset                                 */
    NMI_Handler,            /*  2                                        */
    HardFault_Handler,      /*  3                                        */
    MemManage_Handler,      /*  4                                        */
    BusFault_Handler,       /*  5                                        */
    UsageFault_Handler,     /*  6                                        */
    0, 0, 0, 0,             /*  7-10: reserved                           */
    vPortSVCHandler,        /* 11: SVCall     -> FreeRTOS                */
    DebugMon_Handler,       /* 12                                        */
    0,                      /* 13: reserved                              */
    xPortPendSVHandler,     /* 14: PendSV     -> FreeRTOS context switch */
    xPortSysTickHandler,    /* 15: SysTick    -> FreeRTOS tick           */

    /* ---- LM3S6965 peripheral interrupts, IRQ 0 upward ---- */
    Default_Handler,        /* IRQ  0: GPIO Port A                       */
    Default_Handler,        /* IRQ  1: GPIO Port B                       */
    Default_Handler,        /* IRQ  2: GPIO Port C                       */
    Default_Handler,        /* IRQ  3: GPIO Port D                       */
    Default_Handler,        /* IRQ  4: GPIO Port E                       */
    UART0_Handler,          /* IRQ  5: UART0  <-- the only one we enable */
    Default_Handler,        /* IRQ  6: UART1                             */
    Default_Handler,        /* IRQ  7: SSI0                              */
    Default_Handler,        /* IRQ  8: I2C0                              */
    Default_Handler,        /* IRQ  9: PWM fault                         */
    Default_Handler,        /* IRQ 10: PWM generator 0                   */
    Default_Handler,        /* IRQ 11: PWM generator 1                   */
    Default_Handler,        /* IRQ 12: PWM generator 2                   */
    Default_Handler,        /* IRQ 13: QEI0                              */
    Default_Handler,        /* IRQ 14: ADC sequence 0                    */
    Default_Handler,        /* IRQ 15: ADC sequence 1                    */
    Default_Handler,        /* IRQ 16: ADC sequence 2                    */
    Default_Handler,        /* IRQ 17: ADC sequence 3                    */
    Default_Handler,        /* IRQ 18: watchdog                          */
    Default_Handler,        /* IRQ 19: timer 0 A                         */
    Default_Handler,        /* IRQ 20: timer 0 B                         */
    Default_Handler,        /* IRQ 21: timer 1 A                         */
    Default_Handler,        /* IRQ 22: timer 1 B                         */
    Default_Handler,        /* IRQ 23: timer 2 A                         */
    Default_Handler,        /* IRQ 24: timer 2 B                         */
    Default_Handler,        /* IRQ 25: analog comparator 0               */
    Default_Handler,        /* IRQ 26: analog comparator 1               */
    Default_Handler,        /* IRQ 27: reserved                          */
    Default_Handler,        /* IRQ 28: system control                    */
    Default_Handler,        /* IRQ 29: flash control                     */
};

void Reset_Handler(void)
{
    uint32_t *src;
    uint32_t *dst;

    src = &_sidata;
    for (dst = &_sdata; dst < &_edata;)
    {
        *dst++ = *src++;
    }


    for (dst = &_sbss; dst < &_ebss;)
    {
        *dst++ = 0;
    }

    main();

    for (;;)
    {
    }
}
