#ifndef MAILBOX_H
#define MAILBOX_H

#include <stdint.h>
#include "platform.h"

// ── Comandos ARM → NIOS (coincide con NIOS hello_world_small.c) ──
#define CMD_NONE    0x00
#define CMD_PLAY    0x01
#define CMD_STOP    0x02

// ── Acceso al mailbox: sh[] sobre SHARED_MEM_BASE ─────────
//   sh[SH_IDX_CMD]  = CMD_NONE / CMD_PLAY / CMD_STOP
//   sh[SH_IDX_HEAD] = indice escritura (ARM escribe)
//   sh[SH_IDX_TAIL] = indice lectura  (NIOS escribe)
//   sh[SH_BUF_START + head] = muestra PCM packed (L<<16 | R)

// ── Funciones ─────────────────────────────────────────────
void     mailbox_init(void);
void     mailbox_play(void);
void     mailbox_stop(void);
uint32_t mailbox_get_tail(void);
void     mailbox_set_head(uint32_t head);

#endif
