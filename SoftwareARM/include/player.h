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
PlayerState  player_get_state(void);
int          player_get_track(void);

#endif