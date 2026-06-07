#include "mailbox.h"
#include <stdio.h>
#include <string.h>

// ── Acceso a shared memory ────────────────────────────────
#ifdef TARGET_SIMULATION
  // Simulación: array estático en lugar de memoria mapeada
  static uint32_t _sim_shared[SH_BUF_START + SH_BUF_WORDS];
  static volatile uint32_t* sh = _sim_shared;
#else
  // Hardware: puntero directo a memoria compartida
  static volatile uint32_t* sh = (volatile uint32_t*) SHARED_MEM_BASE;
#endif

void mailbox_init(void) {
#ifdef TARGET_SIMULATION
    memset(_sim_shared, 0, sizeof(_sim_shared));
#endif
    sh[SH_IDX_CMD]  = CMD_NONE;
    sh[SH_IDX_HEAD] = 0;
    sh[SH_IDX_TAIL] = 0;
    sh[SH_IDX_REQ]  = REQ_NONE;
    printf("[MAILBOX] Inicializado en 0x%08X\n", (unsigned)SHARED_MEM_BASE);
}

void mailbox_play(void) {
    sh[SH_IDX_CMD] = CMD_PLAY;
    printf("[MAILBOX] CMD_PLAY\n");
}

void mailbox_stop(void) {
    sh[SH_IDX_CMD] = CMD_STOP;
    printf("[MAILBOX] CMD_STOP\n");
}

uint32_t mailbox_get_tail(void) {
    return sh[SH_IDX_TAIL];
}

void mailbox_set_head(uint32_t head) {
    sh[SH_IDX_HEAD] = head;
}

uint32_t mailbox_get_req(void) {
    return sh[SH_IDX_REQ];
}

void mailbox_clear_req(void) {
    sh[SH_IDX_REQ] = REQ_NONE;
}