#include "storage_manager.h"
#include "../lib/fatfs/ff.h"
#include <stdio.h>
#include <string.h>

static FATFS fs;
static FIL   current_file;
static int   song_count = 0;
static int   mounted    = 0;

int storage_init(void) {
    FRESULT res = f_mount(&fs, "", 1);
    if (res != FR_OK) {
        printf("[STORAGE] Error montando FAT: %d\n", res);
        return -1;
    }
    printf("[STORAGE] FAT montado correctamente\n");
    mounted = 1;
    return 0;
}

int storage_list_songs(SongInfo songs[], int max_songs) {
    if (!mounted) return -1;

    DIR     dir;
    FILINFO fno;
    int     count = 0;

    FRESULT res = f_opendir(&dir, "/");
    if (res != FR_OK) {
        printf("[STORAGE] Error abriendo directorio: %d\n", res);
        return -1;
    }

    printf("[STORAGE] Buscando archivos WAV...\n");

    while (count < max_songs) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        if (fno.fattrib & AM_DIR) continue;

        char* ext = strrchr(fno.fname, '.');
        if (!ext) continue;

        if (ext[1]=='w'||ext[1]=='W') {
            strncpy(songs[count].filename, fno.fname, MAX_NAME_LEN - 1);
            songs[count].size_bytes   = fno.fsize;
            songs[count].duration_sec = 0;
            count++;
            printf("[STORAGE]  [%d] %s (%lu bytes)\n",
                   count, fno.fname, (unsigned long)fno.fsize);
        }
    }

    f_closedir(&dir);
    song_count = count;
    printf("[STORAGE] Total: %d canciones encontradas\n", count);
    return count;
}

int storage_open_song(const char* filename) {
    FRESULT res = f_open(&current_file, filename, FA_READ);
    if (res != FR_OK) {
        printf("[STORAGE] Error abriendo %s: %d\n", filename, res);
        return -1;
    }
    return 0;
}

int storage_read_bytes(uint8_t* buf, uint32_t len) {
    UINT bytes_read;
    FRESULT res = f_read(&current_file, buf, len, &bytes_read);
    if (res != FR_OK) return -1;
    return (int)bytes_read;
}

void storage_close_song(void) {
    f_close(&current_file);
}

int storage_get_song_count(void) {
    return song_count;
}