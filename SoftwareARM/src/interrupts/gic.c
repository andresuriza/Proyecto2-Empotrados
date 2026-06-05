#include "interrupts/gic.h"
#include "platform.h"

void gic_init(void) {
    // 1. Deshabilitá el distribuidor para configurarlo
    GICD_CTLR = 0;

    // 2. Configurá la prioridad mínima del CPU Interface
    //    0xFF = acepta interrupciones de cualquier prioridad
    GICC_PMR = 0xFF;

    // 3. Habilitá el CPU Interface
    GICC_CTLR = 1;

    // 4. Habilitá el Distribuidor
    GICD_CTLR = 1;
}

void gic_enable_irq(uint32_t irq_id) {
    // Cada registro ISENABLER maneja 32 IRQs
    // irq_id 72 → registro ISENABLER2, bit 8
    uint32_t reg   = irq_id / 32;
    uint32_t bit   = irq_id % 32;

    // Dirección del registro correcto
    volatile uint32_t* isenabler = 
        (volatile uint32_t*)(GICD_BASE + 0x100 + (reg * 4));

    *isenabler = (1 << bit);

    // Apuntar la interrupción al CPU 0
    GICD_ITARGETSR[irq_id / 4] |= (0x01 << ((irq_id % 4) * 8));
}

void gic_set_priority(uint32_t irq_id, uint8_t priority) {
    GICD_IPRIORITY[irq_id / 4] |= (priority << ((irq_id % 4) * 8));
}