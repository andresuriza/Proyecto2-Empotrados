#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <stdint.h>

#define MAX_SONGS    10
#define MAX_NAME_LEN 64

typedef struct {
    char     filename[MAX_NAME_LEN];
    uint32_t size_bytes;
    uint32_t duration_sec;
} SongInfo;

int  storage_init(void);
int  storage_list_songs(SongInfo songs[], int max_songs);
int  storage_open_song(const char* filename);
int  storage_read_bytes(uint8_t* buf, uint32_t len);
void storage_close_song(void);
int  storage_get_song_count(void);

#endif