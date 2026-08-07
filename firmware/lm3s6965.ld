/* Linker script for the TI Stellaris LM3S6965 (Cortex-M3) as emulated by
 * QEMU's 'lm3s6965evb' machine.
 *
 *   FLASH at 0x00000000 -- 256 KB, read-only at runtime. Code and constants.
 *   SRAM  at 0x20000000 --  64 KB, read/write. Variables and stacks.
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

_estack = ORIGIN(SRAM) + LENGTH(SRAM);

_Min_Stack_Size = 0x800;   /* 2 KB for the main/interrupt stack (MSP) */

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)   *(.text*)
        *(.rodata) *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } >FLASH

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } >FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data) *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >SRAM AT> FLASH

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss) *(.bss*) *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } >SRAM

    ._stack_guard :
    {
        . = ALIGN(8);
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } >SRAM
}
