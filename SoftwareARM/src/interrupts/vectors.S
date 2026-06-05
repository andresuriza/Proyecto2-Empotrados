.section .vectors, "ax"
.global _start

_start:
    ldr pc, =reset_handler          @ Reset
    ldr pc, =undefined_handler      @ Instrucción indefinida
    ldr pc, =swi_handler            @ Software interrupt
    ldr pc, =prefetch_handler       @ Prefetch abort
    ldr pc, =data_abort_handler     @ Data abort
    nop                             @ Reservado
    ldr pc, =irq_handler            @ IRQ  ← el importante
    ldr pc, =fiq_handler            @ FIQ

@ Handlers vacíos para los que no usamos
undefined_handler:  b undefined_handler
swi_handler:        b swi_handler
prefetch_handler:   b prefetch_handler
data_abort_handler: b data_abort_handler
fiq_handler:        b fiq_handler

@ IRQ Handler — guarda contexto y llama a C
irq_handler:
    sub   lr, lr, #4                @ Ajuste del PC en IRQ
    stmfd sp!, {r0-r12, lr}         @ Guardar registros
    bl    irq_dispatch              @ Llamar al dispatcher en C
    ldmfd sp!, {r0-r12, pc}^        @ Restaurar registros y retornar