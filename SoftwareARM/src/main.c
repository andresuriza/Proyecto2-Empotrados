#include "platform.h"
#include "player.h"
#include "mailbox.h"
#include <stdio.h>
#include <stdint.h>

// Habilita los puentes HPS<->FPGA. El ARM bare-metal accede a 0xFF200000+
// (shared_mem, VGA); U-Boot NO los deja habilitados para bare-metal, asi que
// sin esto el primer acceso a la shared_mem da "data abort".
// rstmgr.brgmodrst (0xFFD0501C): bit0=h2f, bit1=lwh2f, bit2=f2h. 0 = fuera de reset.
static void hps_bridges_enable(void) {
    *(volatile uint32_t *)0xFFD0501C = 0x0;
}

int main(void) {
    hps_bridges_enable();

    printf("\n=== Audio Player ARM ===\n\n");

    player_init();

    // Arrancar reproducción automáticamente
    player_play_pause();

    while (1) {
        // Leer solicitudes del NIOS (next/prev)
        uint32_t req = mailbox_get_req();
        if (req == REQ_NEXT) {
            player_next_track();
            mailbox_clear_req();
        } else if (req == REQ_PREV) {
            player_prev_track();
            mailbox_clear_req();
        }

        player_update();
    }

    return 0;
}