#include "mailbox.h"
#include <stdio.h>

static volatile uint32_t *sh = (volatile uint32_t *) SHARED_MEM_BASE;

void mailbox_init(void) {
    sh[SH_IDX_CMD]  = CMD_NONE;
    sh[SH_IDX_HEAD] = 0;
    sh[SH_IDX_TAIL] = 0;
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
