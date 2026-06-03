ENTRY(_start)

SECTIONS {
    . = 0x60000000;

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
    . = . + 0x4000;
    __stack_top = .;
}