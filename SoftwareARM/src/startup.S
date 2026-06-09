.section .text.startup
.global _start

_start:
    @ Permitir accesos no alineados a memoria normal (SCTLR.A = 0).
    @ U-Boot deja el chequeo de alineacion activo; sin esto, memcpy/strcpy de
    @ newlib (precompilados, hacen accesos de palabra no alineados) provocan
    @ "data abort" al copiar p.ej. nombres de archivo de longitud impar.
    mrc p15, 0, r0, c1, c0, 0     @ leer SCTLR
    bic r0, r0, #(1 << 1)         @ limpiar bit A (alignment fault enable)
    mcr p15, 0, r0, c1, c0, 0     @ escribir SCTLR
    isb

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