#include "player.h"
#include "mailbox.h"
#include "storage_manager.h"
#include "wav_parser.h"
#include <stdio.h>
#include <stdint.h>

static PlayerState state         = PLAYER_STOPPED;
static int         current_track = 0;
static int         total_tracks  = 0;

static SongInfo    songs[MAX_SONGS];
static WavInfo     wav_info;
static uint32_t    head = 0;

static volatile uint32_t *sh = (volatile uint32_t *) SHARED_MEM_BASE;

static void open_track(int idx) {
    storage_close_song();

    if (wav_parse(songs[idx].filename, &wav_info) != WAV_OK) {
        printf("[PLAYER] Error parseando %s\n", songs[idx].filename);
        state = PLAYER_STOPPED;
        return;
    }

    wav_print_info(&wav_info);

    // Reabrir y saltar al inicio de los datos PCM
    if (storage_open_song(songs[idx].filename) != 0 ||
        storage_seek(wav_info.data_offset) != 0) {
        printf("[PLAYER] Error abriendo datos de %s\n", songs[idx].filename);
        state = PLAYER_STOPPED;
        return;
    }

    head = 0;
    mailbox_init();
    mailbox_play();
    printf("[PLAYER] Reproduciendo [%d] %s\n", idx + 1, songs[idx].filename);
}

void player_init(void) {
    state         = PLAYER_STOPPED;
    current_track = 0;

    if (storage_init() != 0) {
        printf("[PLAYER] Error: no se pudo montar FAT\n");
        return;
    }

    total_tracks = storage_list_songs(songs, MAX_SONGS);
    if (total_tracks <= 0) {
        printf("[PLAYER] No se encontraron archivos WAV\n");
        return;
    }

    printf("[PLAYER] Iniciado. %d canciones disponibles\n", total_tracks);
}

void player_play_pause(void) {
    if (total_tracks <= 0) return;

    switch (state) {
        case PLAYER_STOPPED:
            state = PLAYER_PLAYING;
            open_track(current_track);
            break;
        case PLAYER_PAUSED:
            state = PLAYER_PLAYING;
            mailbox_play();
            printf("[PLAYER] Reanudando cancion %d\n", current_track + 1);
            break;
        case PLAYER_PLAYING:
            state = PLAYER_PAUSED;
            mailbox_stop();
            printf("[PLAYER] Pausado en cancion %d\n", current_track + 1);
            break;
    }
}

void player_next_track(void) {
    if (total_tracks <= 0) return;
    current_track = (current_track + 1) % total_tracks;
    printf("[PLAYER] Siguiente → cancion %d\n", current_track + 1);
    if (state == PLAYER_PLAYING) open_track(current_track);
}

void player_prev_track(void) {
    if (total_tracks <= 0) return;
    current_track = (current_track - 1 + total_tracks) % total_tracks;
    printf("[PLAYER] Anterior → cancion %d\n", current_track + 1);
    if (state == PLAYER_PLAYING) open_track(current_track);
}

void player_stop(void) {
    mailbox_stop();
    storage_close_song();
    state = PLAYER_STOPPED;
    head  = 0;
    printf("[PLAYER] Detenido\n");
}

// Llamar en el main loop — escribe un frame PCM al buffer si hay espacio
void player_update(void) {
    if (state != PLAYER_PLAYING) return;

    // Verificar espacio en buffer circular
    uint32_t tail = mailbox_get_tail();
    uint32_t next = (head + 1) % SH_BUF_WORDS;
    if (next == tail) return; // buffer lleno

    // Leer un frame (2 bytes por canal)
    int16_t sl = 0, sr = 0;
    if (storage_read_bytes((uint8_t *)&sl, 2) < 2) {
        // Fin del archivo
        printf("[PLAYER] Fin de cancion %d\n", current_track + 1);
        player_stop();
        return;
    }

    if (wav_info.num_channels == 2)
        storage_read_bytes((uint8_t *)&sr, 2);
    else
        sr = sl; // mono → duplicar

    // Empacar L y R: bits[31:16]=L bits[15:0]=R
    sh[SH_BUF_START + head] = ((uint32_t)(uint16_t)sl << 16) | (uint16_t)sr;
    head = next;
    mailbox_set_head(head);
}

PlayerState player_get_state(void) { return state; }
int         player_get_track(void) { return current_track; }
