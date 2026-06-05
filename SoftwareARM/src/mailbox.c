#include "mailbox.h"
#include <stdio.h>
#include <string.h>

// ── Acceso a memoria compartida ───────────────────────────
// En simulación usamos structs estáticos
// En hardware apuntarán a MAILBOX_ARM2NIOS y MAILBOX_NIOS2ARM

#ifdef TARGET_SIMULATION
  static MailboxARM2NIOS  _sim_arm2nios;
  static MailboxNIOS2ARM  _sim_nios2arm;
  static MailboxARM2NIOS* mb_arm = &_sim_arm2nios;
  static MailboxNIOS2ARM* mb_nios = &_sim_nios2arm;
#else
  // Hardware: apuntar directo a memoria compartida
  static MailboxARM2NIOS* mb_arm  =
      (MailboxARM2NIOS*) MAILBOX_ARM2NIOS;
  static MailboxNIOS2ARM* mb_nios =
      (MailboxNIOS2ARM*) MAILBOX_NIOS2ARM;
#endif

// ── Init ──────────────────────────────────────────────────

void mailbox_init(void) {
    mb_arm->command = CMD_NONE;
    mb_arm->ack     = ACK_CLEAR;
    mb_arm->param1  = 0;
    mb_arm->param2  = 0;

    mb_nios->status = NIOS_IDLE;
    mb_nios->ack    = ACK_CLEAR;
    mb_nios->param1 = 0;
    mb_nios->param2 = 0;

    printf("[MAILBOX] Inicializado\n");
}

// ── ARM → NIOS ────────────────────────────────────────────

int mailbox_send_command(uint32_t cmd, uint32_t p1, uint32_t p2) {
    // Verificar que el NIOS ya procesó el comando anterior
    if (mb_arm->ack == ACK_CLEAR && mb_arm->command != CMD_NONE) {
        printf("[MAILBOX] Comando anterior aun pendiente\n");
        return -1;
    }

    mb_arm->param1  = p1;
    mb_arm->param2  = p2;
    mb_arm->ack     = ACK_CLEAR;
    mb_arm->command = cmd;

    printf("[MAILBOX] ARM → NIOS: %s\n", mailbox_cmd_str(cmd));
    return 0;
}

int mailbox_wait_ack(uint32_t timeout_ms) {
    uint32_t count = 0;
    while (mb_arm->ack != ACK_SET) {
        count++;
        if (count >= timeout_ms) {
            printf("[MAILBOX] Timeout esperando ACK del NIOS\n");
            return -1;
        }

        // En simulación: auto-ack para pruebas
        #ifdef TARGET_SIMULATION
            mb_arm->ack = ACK_SET;
            printf("[MAILBOX] [SIM] NIOS hizo ACK del comando\n");
        #endif
    }

    mb_arm->command = CMD_NONE;
    return 0;
}

// ── NIOS → ARM ────────────────────────────────────────────

uint32_t mailbox_read_status(void) {
    return mb_nios->status;
}

void mailbox_ack_status(void) {
    mb_nios->ack    = ACK_SET;
    mb_nios->status = NIOS_IDLE;
    printf("[MAILBOX] ARM → ACK status NIOS\n");
}

// ── Helpers ───────────────────────────────────────────────

const char* mailbox_cmd_str(uint32_t cmd) {
    switch (cmd) {
        case CMD_NONE:  return "NONE";
        case CMD_PLAY:  return "PLAY";
        case CMD_PAUSE: return "PAUSE";
        case CMD_STOP:  return "STOP";
        default:        return "UNKNOWN";
    }
}

const char* mailbox_status_str(uint32_t status) {
    switch (status) {
        case NIOS_IDLE:      return "IDLE";
        case NIOS_BUF_READY: return "BUF_READY";
        case NIOS_SONG_DONE: return "SONG_DONE";
        case NIOS_PLAYING:   return "PLAYING";
        case NIOS_PAUSED:    return "PAUSED";
        default:             return "UNKNOWN";
    }
}