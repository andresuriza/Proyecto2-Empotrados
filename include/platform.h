#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

// ── Memoria compartida ARM/NIOS  ──────
// CAMBIAR con el mapa de Platform Designer
#define SHARED_MEM_BASE     0x00000000  // temporal

#define MAILBOX_ARM2NIOS    (SHARED_MEM_BASE + 0x0000)  // 256 bytes
#define MAILBOX_NIOS2ARM    (SHARED_MEM_BASE + 0x0100)  // 256 bytes
#define SHARED_METADATA     (SHARED_MEM_BASE + 0x0200)  // 512 bytes
#define SHARED_AUDIO_BUF    (SHARED_MEM_BASE + 0x0400)  // ~63KB

// ── Memoria privada ARM ────────────────────────────────────
// CAMBIAR con el mapa de Platform Designer
#define ARM_PRIVATE_MEM_BASE  0x00010000  // temporal
#define ARM_PRIVATE_MEM_SIZE  0x10000     // 64KB

#endif 