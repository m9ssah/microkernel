/* FreeRTOS configuration for LM3S6965 (Cortex-M3) under QEMU.
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

// scheduler
#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_TIMERS 0
#define configMAX_PRIORITIES 5
#define configMAX_TASK_NAME_LEN 12
#define configUSE_16_BIT_TICKS 0 // 32-bit tick counter
#define configIDLE_SHOULD_YIELD 1

#define configCPU_CLOCK_HZ 50000000UL
#define configTICK_RATE_HZ 1000

// memory allocation
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configSUPPORT_STATIC_ALLOCATION 0
#define configTOTAL_HEAP_SIZE (12 * 1024)

#define configMINIMAL_STACK_SIZE ((uint16_t)128)

// stack overflow detection
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK 1
#define configRECORD_STACK_HIGH_ADDRESS 1

#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskDelayUntil 1
#define INCLUDE_vTaskDelete 0
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1

#define configPRIO_BITS 3

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 7
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))       /* 0xE0 */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))  /* 0xA0 */

// assertions
#ifndef __ASSEMBLER__
void vAssertCalled(const char *pcFile, unsigned long ulLine);
#define configASSERT(x)                                                        \
  if ((x) == 0)                                                                \
  vAssertCalled(__FILE__, __LINE__)
#endif

#endif
