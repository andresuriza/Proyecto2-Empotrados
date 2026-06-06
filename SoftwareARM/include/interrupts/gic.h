#ifndef GIC_H
#define GIC_H

#include <stdint.h>

// Registros del Distribuidor (GICD)
#define GICD_CTLR        (*(volatile uint32_t*)(GICD_BASE + 0x000))
#define GICD_ISENABLER0  (*(volatile uint32_t*)(GICD_BASE + 0x100))
#define GICD_ISENABLER1  (*(volatile uint32_t*)(GICD_BASE + 0x104))
#define GICD_ISENABLER2  (*(volatile uint32_t*)(GICD_BASE + 0x108))
#define GICD_IPRIORITY   ((volatile uint32_t*) (GICD_BASE + 0x400))
#define GICD_ITARGETSR   ((volatile uint32_t*) (GICD_BASE + 0x800))
#define GICD_ICFGR       ((volatile uint32_t*) (GICD_BASE + 0xC00))

// Registros del CPU Interface (GICC)
#define GICC_CTLR        (*(volatile uint32_t*)(GICC_BASE + 0x000))
#define GICC_PMR         (*(volatile uint32_t*)(GICC_BASE + 0x004))
#define GICC_IAR         (*(volatile uint32_t*)(GICC_BASE + 0x00C))
#define GICC_EOIR        (*(volatile uint32_t*)(GICC_BASE + 0x010))

// Funciones
void gic_init(void);
void gic_enable_irq(uint32_t irq_id);
void gic_set_priority(uint32_t irq_id, uint8_t priority);

#endif