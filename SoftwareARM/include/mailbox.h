#ifndef MAILBOX_H
#define MAILBOX_H

#include <stdint.h>
#include "platform.h"

// ── Comandos ARM → NIOS ───────────────────────────────────
#define CMD_NONE    0x00
#define CMD_PLAY    0x01
#define CMD_STOP    0x02

// ── Funciones ─────────────────────────────────────────────
void     mailbox_init(void);
void     mailbox_play(void);
void     mailbox_stop(void);
uint32_t mailbox_get_tail(void);
void     mailbox_set_head(uint32_t head);
uint32_t mailbox_get_req(void);
void     mailbox_clear_req(void);
void     mailbox_wait_ack(void);   // espera a que el NIOS procese el STOP (CMD->0)

#endif