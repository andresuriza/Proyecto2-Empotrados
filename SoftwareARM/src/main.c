#include "platform.h"
#include "player.h"
#include "bsp/bsp_buttons.h"
#include <stdio.h>

int main(void) {
    printf("\n=== Audio Player ARM ===\n\n");

    player_init();
    buttons_init();

    // Arrancar reproducción automáticamente si hay canciones
    player_play_pause();

    while (1) {
        buttons_poll();
        player_update();  // escribe un frame PCM al buffer shared_mem
    }

    return 0;
}
