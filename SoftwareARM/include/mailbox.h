#ifndef MAILBOX_H
#define MAILBOX_H

#include <stdint.h>
#include "platform.h"

// ── Comandos ARM → NIOS ───────────────────────────────────
#define CMD_NONE        0x00
#define CMD_PLAY        0x01
#define CMD_PAUSE       0x02
#define CMD_STOP        0x03

// ── Respuestas NIOS → ARM ─────────────────────────────────
#define NIOS_IDLE       0x00
#define NIOS_BUF_READY  0x01    // NIOS listo para más datos
#define NIOS_SONG_DONE  0x02    // canción terminada
#define NIOS_PLAYING    0x03    // confirmación de reproducción
#define NIOS_PAUSED     0x04    // confirmación de pausa

// ── Flags de acknowledge ──────────────────────────────────
#define ACK_CLEAR       0x00
#define ACK_SET         0x01

// ── Estructura del mailbox ARM → NIOS (en memoria compartida)
typedef struct {
    volatile uint32_t command;      // CMD_PLAY, CMD_PAUSE, CMD_STOP
    volatile uint32_t ack;          // NIOS escribe ACK_SET cuando recibe
    volatile uint32_t param1;       // parámetro extra (ej: track number)
    volatile uint32_t param2;       // parámetro extra
} MailboxARM2NIOS;

// ── Estructura del mailbox NIOS → ARM (en memoria compartida)
typedef struct {
    volatile uint32_t status;       // NIOS_BUF_READY, NIOS_SONG_DONE, etc.
    volatile uint32_t ack;          // ARM escribe ACK_SET cuando recibe
    volatile uint32_t param1;       // parámetro extra
    volatile uint32_t param2;       // parámetro extra
} MailboxNIOS2ARM;

// ── Funciones ─────────────────────────────────────────────
void mailbox_init(void);

// ARM → NIOS
int  mailbox_send_command(uint32_t cmd, uint32_t p1, uint32_t p2);
int  mailbox_wait_ack(uint32_t timeout_ms);

// NIOS → ARM
uint32_t mailbox_read_status(void);
void     mailbox_ack_status(void);

// Helpers
const char* mailbox_cmd_str(uint32_t cmd);
const char* mailbox_status_str(uint32_t status);

#endif