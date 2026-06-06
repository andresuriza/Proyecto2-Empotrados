#ifndef PLAYER_H
#define PLAYER_H

typedef enum {
    PLAYER_STOPPED,
    PLAYER_PLAYING,
    PLAYER_PAUSED
} PlayerState;

void         player_init(void);
void         player_play_pause(void);
void         player_next_track(void);
void         player_prev_track(void);
void         player_stop(void);
void         player_update(void);   // llamar en el main loop — escribe un frame al buffer
PlayerState  player_get_state(void);
int          player_get_track(void);

#endif
