.section .text.startup
.global _start

_start:
    @ Configurar stack pointer
    ldr sp, =__stack_top

    @ Limpiar BSS (variables globales en 0)
    ldr r0, =__bss_start__
    ldr r1, =__bss_end__
    mov r2, #0
.zero_bss:
    cmp r0, r1
    bge .bss_done
    str r2, [r0], #4
    b   .zero_bss
.bss_done:

    @ Llamar a main
    bl main

.hang:
    b .hang