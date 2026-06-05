#include "bsp/bsp_buttons.h"
#include "player.h"
#include <stdio.h>

void buttons_init(void) {
    printf("=== Controles ===\n");
    printf("  p + Enter = play/pause\n");
    printf("  n + Enter = siguiente\n");
    printf("  b + Enter = anterior\n");
    printf("  s + Enter = stop\n");
    printf("=================\n\n");
}

void buttons_poll(void) {
    int c = getchar();
    if (c == EOF) return;

    switch ((char)c) {
        case 'p': player_play_pause(); break;
        case 'n': player_next_track();  break;
        case 'b': player_prev_track();  break;
        case 's': player_stop();        break;
        case '\n': break;
        default:
            printf("[BTN] Tecla desconocida: '%c'\n", (char)c);
            break;
    }
}