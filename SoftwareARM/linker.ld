ENTRY(_start)

SECTIONS {
    . = 0x00100000;     /* DDR3 real de la DE1-SoC (antes 0x60000000 = QEMU) */

    .text : {
        *(.text.startup)
        *(.text*)
    }

    .rodata : { *(.rodata*) }

    .data : { *(.data*) }

    .bss : {
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        __bss_end__ = .;
    }

    . = ALIGN(8);
    . = . + 0x8000;     /* stack 32KB (player_update usa ~8KB de buffer en stack) */
    __stack_top = .;
}