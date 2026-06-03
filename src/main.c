#include "platform.h"
#include "player.h"
#include "bsp/bsp_buttons.h"
#include <stdio.h>

int main(void) {
    printf("\n=== Audio Player SIM ===\n\n");

    player_init();
    buttons_init();

    while (1) {
        buttons_poll();
    }

    return 0;
}