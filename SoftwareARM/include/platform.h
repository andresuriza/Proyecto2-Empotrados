#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

// ══════════════════════════════════════════════════════════
// HARDWARE — direcciones reales DE1-SoC
// ══════════════════════════════════════════════════════════
#ifdef TARGET_HARDWARE

  // ── LW AXI Bridge HPS→FPGA ──────────────────────────────
  #define LW_BRIDGE_BASE    0xFF200000UL

  // ── shared_mem en offset 0x00000 del LW bridge ──────────
  #define SHARED_MEM_BASE   (LW_BRIDGE_BASE + 0x00000UL)

  // ── pio_key via LW bridge ────────────────────────────────
  #define BTN_BASE          (LW_BRIDGE_BASE + 0x31070UL)

  // ── GIC del Cortex-A9 ────────────────────────────────────
  #define GICD_BASE         0xFFFED000UL
  #define GICC_BASE         0xFFFEC100UL
  #define BTN_IRQ_ID        72

// ══════════════════════════════════════════════════════════
// SIMULACIÓN PC — make pc
// ══════════════════════════════════════════════════════════
#elif defined(TARGET_SIMULATION)

  // Memoria simulada estática en mailbox.c
  #define SHARED_MEM_BASE   0x00000000UL
  #define BTN_BASE          0x00000000UL

#endif

// ── Mailbox layout (compartido hw y sim) ──────────────────
#define SH_IDX_CMD        0       // ARM escribe: 0=idle 1=play 2=stop
#define SH_IDX_HEAD       1       // ARM escribe: indice escritura buffer
#define SH_IDX_TAIL       2       // NIOS escribe: indice lectura buffer
#define SH_IDX_REQ        3       // NIOS escribe: 0=none 1=next 2=prev
#define SH_BUF_START      64      // primer word del buffer circular
#define SH_BUF_WORDS      16320   // capacidad del buffer en words

// Valores de SH_IDX_REQ
#define REQ_NONE          0
#define REQ_NEXT          1
#define REQ_PREV          2

// ── VGA Text Controller ───────────────────────────────────
// Offset correcto = 0x10000 -> 0xFF210000. Descomentar cuando se implementen metadatos.
// #define VGA_BASE         (LW_BRIDGE_BASE + 0x10000UL) // 0xFF210000
#define VGA_COLS          80
#define VGA_ROWS          30

#endif