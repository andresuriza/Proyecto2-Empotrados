#include "storage_manager.h"
#include "wav_parser.h"
#include "circular_buffer.h"
#include <stdio.h>
#include <stdlib.h>

// Buffer en memoria privada del ARM (simulado)
static CircularBuffer audio_buf;

int main(void) {
    printf("=== Test Storage + WAV Parser + Buffer ===\n\n");

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

    // Inicializar buffer
    circ_buf_init(&audio_buf);

    // Parsear y cargar primera canción
    printf("\n=== Cargando %s ===\n", songs[0].filename);
    WavInfo info;
    if (wav_parse(songs[0].filename, &info) != 0) {
        printf("Error parseando WAV\n");
        return 1;
    }
    wav_print_info(&info);

    // Configurar info de audio en el buffer
    audio_buf.sample_rate     = info.sample_rate;
    audio_buf.num_channels    = info.num_channels;
    audio_buf.bits_per_sample = info.bits_per_sample;

    // Leer datos de audio en bloques y llenar buffer
    printf("\n=== Llenando buffer circular ===\n");
    if (storage_open_song(songs[0].filename) != 0) {
        printf("Error abriendo cancion\n");
        return 1;
    }

    // Saltar el header WAV
    uint8_t header_skip[256];
    storage_read_bytes(header_skip, info.data_offset);

    // Leer bloques de audio
    uint8_t block[BLOCK_SIZE];
    int blocks_written = 0;
    int bytes_read;

    while ((bytes_read = storage_read_bytes(block, BLOCK_SIZE)) > 0) {
        if (circ_buf_write_block(&audio_buf, block, bytes_read) < 0) {
            printf("[TEST] Buffer lleno en bloque %d\n", blocks_written);
            break;
        }
        blocks_written++;
    }

    storage_close_song();

    printf("[TEST] Bloques escritos:  %d\n", blocks_written);
    printf("[TEST] Bloques libres:    %d\n", circ_buf_free_blocks(&audio_buf));
    printf("[TEST] Buffer lleno:      %s\n",
           circ_buf_is_full(&audio_buf) ? "si" : "no");

    // Simular lectura del NIOS
    printf("\n=== Simulando lectura NIOS ===\n");
    uint8_t out[BLOCK_SIZE];
    int blocks_read = 0;

    while (circ_buf_read_block(&audio_buf, out, BLOCK_SIZE) > 0)
        blocks_read++;

    printf("[TEST] Bloques leidos por NIOS: %d\n", blocks_read);
    printf("[TEST] Buffer vacio:            %s\n",
           circ_buf_is_empty(&audio_buf) ? "si" : "no");

    return 0;
}