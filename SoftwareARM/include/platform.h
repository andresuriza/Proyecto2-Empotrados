#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

// ── LW AXI Bridge HPS→FPGA ────────────────────────────────
#define LW_BRIDGE_BASE    0xFF200000UL

// ── shared_mem.s2 en offset 0x00000 del LW bridge ─────────
#define SHARED_MEM_BASE   (LW_BRIDGE_BASE + 0x00000UL)

// ── Mailbox layout (coincide exactamente con NIOS hello_world_small.c) ──
#define SH_IDX_CMD        0       // ARM escribe: 0=idle 1=play 2=stop
#define SH_IDX_HEAD       1       // ARM escribe: indice escritura buffer
#define SH_IDX_TAIL       2       // NIOS escribe: indice lectura buffer
#define SH_BUF_START      64      // primer word del buffer circular
#define SH_BUF_WORDS      16320   // capacidad del buffer en words

// ── pio_key via LW bridge (para acceso ARM directo si se necesita) ────
#define BTN_BASE          (LW_BRIDGE_BASE + 0x31070UL)

// ── GIC del Cortex-A9 en Cyclone V HPS ────────────────────
#define GICD_BASE         0xFFFED000UL
#define GICC_BASE         0xFFFEC100UL

// ── IRQ ID del botón via f2h_irq0[0] (requiere routing en Platform Designer) ──
#define BTN_IRQ_ID        72

#endif
