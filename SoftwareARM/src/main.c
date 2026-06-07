#include "platform.h"
#include "player.h"
#include "mailbox.h"
#include <stdio.h>

int main(void) {
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