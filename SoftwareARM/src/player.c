#include "player.h"
#include "mailbox.h"
#include "storage_manager.h"
#include "wav_parser.h"
#include <stdio.h>
#include <stdint.h>

// Frames por bloque — fix del tono grave
// 2048 frames * 2 canales * 2 bytes = 8192 bytes por lectura
#define FRAMES_PER_READ   2048

static PlayerState state         = PLAYER_STOPPED;
static int         current_track = 0;
static int         total_tracks  = 0;
static SongInfo    songs[MAX_SONGS];
static WavInfo     wav_info;
static uint32_t    head          = 0;

#ifdef TARGET_SIMULATION
  // En simulación sh[] es el array interno de mailbox.c
  // Accedemos via funciones del mailbox únicamente
  static uint32_t _sim_buf[SH_BUF_START + SH_BUF_WORDS];
  static volatile uint32_t* sh = _sim_buf;
#else
  static volatile uint32_t* sh = (volatile uint32_t*) SHARED_MEM_BASE;
#endif

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
        printf("[PLAYER] Error abriendo datos de %s\n",
               songs[idx].filename);
        state = PLAYER_STOPPED;
        return;
    }

    // Resync limpio: STOP → esperar ACK del NIOS (resetea su tail y limpia FIFO)
    // → resetear punteros → PLAY. Sin el wait, el NIOS puede no ver el CMD=2 y
    // quedar desincronizado al cambiar de cancion.
    mailbox_stop();
    mailbox_wait_ack();
    head = 0;
    mailbox_set_head(0);
    mailbox_play();

    printf("[PLAYER] Reproduciendo [%d] %s\n",
           idx + 1, songs[idx].filename);
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
            // Pausa la maneja el NIOS — no hacer nada desde ARM
            state = PLAYER_PLAYING;
            printf("[PLAYER] Reanudando cancion %d\n", current_track + 1);
            break;
        case PLAYER_PLAYING:
            // Pausa la maneja el NIOS con backpressure
            state = PLAYER_PAUSED;
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

// Llamar en el main loop — escribe bloques de audio al buffer
void player_update(void) {
    if (state != PLAYER_PLAYING) return;

    // Leer bloque de FRAMES_PER_READ frames
    int16_t frames[FRAMES_PER_READ * 2]; // stereo
    uint32_t bytes_to_read = FRAMES_PER_READ
                           * wav_info.num_channels
                           * (wav_info.bits_per_sample / 8);

    int bytes_read = storage_read_bytes(
        (uint8_t*)frames, bytes_to_read);

    if (bytes_read <= 0) {
        // Fin del archivo → auto-siguiente
        printf("[PLAYER] Fin de cancion %d → siguiente\n",
               current_track + 1);
        player_next_track();
        return;
    }

    // Calcular frames reales leídos
    int frames_read = bytes_read
                    / (wav_info.num_channels
                    * (wav_info.bits_per_sample / 8));

    // Escribir frames al buffer circular de shared memory
    for (int i = 0; i < frames_read; i++) {
        uint32_t next = (head + 1) % SH_BUF_WORDS;

        // Backpressure: esperar espacio SIN perder frames (antes hacia break y
        // descartaba el resto del bloque -> se perdia ~99% del audio).
        // Si el NIOS pide next/prev, salir y dejar que el main loop lo maneje
        // (al cambiar de cancion se hace resync, asi que no importa el frame actual).
        while (next == mailbox_get_tail()) {
            if (mailbox_get_req() != REQ_NONE) return;
        }

        int16_t sl = frames[i * wav_info.num_channels];
        int16_t sr = (wav_info.num_channels == 2)
                   ? frames[i * wav_info.num_channels + 1]
                   : sl;

        // Empacar L y R: bits[31:16]=L bits[15:0]=R
        sh[SH_BUF_START + head] =
            ((uint32_t)(uint16_t)sl << 16) | (uint16_t)sr;

        head = next;
        mailbox_set_head(head);
    }
}

PlayerState player_get_state(void) { return state; }
int         player_get_track(void) { return current_track; }