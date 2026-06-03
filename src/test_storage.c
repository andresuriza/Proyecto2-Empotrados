#include "storage_manager.h"
#include "wav_parser.h"
#include <stdio.h>

int main(void) {
    printf("=== Test Storage + WAV Parser ===\n\n");

    if (storage_init() != 0) {
        printf("Error iniciando storage\n");
        return 1;
    }

    SongInfo songs[MAX_SONGS];
    int count = storage_list_songs(songs, MAX_SONGS);
    if (count <= 0) {
        printf("No se encontraron canciones\n");
        return 1;
    }

    printf("\n=== Parseando headers WAV ===\n");
    for (int i = 0; i < count; i++) {
        printf("\n[%d] %s\n", i + 1, songs[i].filename);

        WavInfo info;
        if (wav_parse(songs[i].filename, &info) == WAV_OK) {
            wav_print_info(&info);
            songs[i].duration_sec = info.duration_sec;
        } else {
            printf("    Error parseando archivo\n");
        }
    }

    return 0;
}