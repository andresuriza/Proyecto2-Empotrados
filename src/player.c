#include "player.h"
#include <stdio.h>

static PlayerState state       = PLAYER_STOPPED;
static int         current_track = 0;
static const int   total_tracks  = 5;  // simulamos 5 canciones

void player_init(void) {
    state         = PLAYER_STOPPED;
    current_track = 0;
    printf("[PLAYER] Iniciado. %d canciones disponibles\n", total_tracks);
}

void player_play_pause(void) {
    switch (state) {
        case PLAYER_STOPPED:
        case PLAYER_PAUSED:
            state = PLAYER_PLAYING;
            printf("[PLAYER] Reproduciendo cancion %d\n", current_track + 1);
            break;
        case PLAYER_PLAYING:
            state = PLAYER_PAUSED;
            printf("[PLAYER] Pausado en cancion %d\n", current_track + 1);
            break;
    }
}

void player_next_track(void) {
    current_track = (current_track + 1) % total_tracks;
    printf("[PLAYER] Siguiente → cancion %d\n", current_track + 1);
    if (state == PLAYER_PLAYING)
        printf("[PLAYER] Reproduciendo cancion %d\n", current_track + 1);
}

void player_prev_track(void) {
    current_track = (current_track - 1 + total_tracks) % total_tracks;
    printf("[PLAYER] Anterior → cancion %d\n", current_track + 1);
    if (state == PLAYER_PLAYING)
        printf("[PLAYER] Reproduciendo cancion %d\n", current_track + 1);
}

void player_stop(void) {
    state = PLAYER_STOPPED;
    printf("[PLAYER] Detenido\n");
}

PlayerState player_get_state(void) { return state; }
int         player_get_track(void) { return current_track; }