/*
 * link.ld — template linker script for MBJS (libmochiberry) on ARM
 * Cortex-M-class bare-metal targets (no OS).
 *
 * Layout:
 *   FLASH: vectors, .text, .rodata
 *   RAM:   .data, .bss, then a fixed MBJS heap region
 *
 * The MBJS heap region is where the embedder seeds the engine's memory
 * arena (mbjs_baremetal_init(&bm, &__mbjs_heap_start, heap_size, 0)).
 * Adjust FLASH/RAM sizes to your MCU.
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : {
        KEEP(*(.isr_vector))
    } > FLASH

    .text : {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .data : {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> FLASH

    .bss : {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* The engine's entire memory budget ("memory management kosongan"). */
    . = ALIGN(8);
    __mbjs_heap_start = .;
    .mbjs_heap : {
        . = ALIGN(8);
        KEEP(*(.mbjs_heap))
        . = ALIGN(8);
    } > RAM
    __mbjs_heap_end = ORIGIN(RAM) + LENGTH(RAM);

    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
