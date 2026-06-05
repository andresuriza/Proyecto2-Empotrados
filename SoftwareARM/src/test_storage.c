#include "storage_manager.h"
#include "wav_parser.h"
#include "circular_buffer.h"
#include "mailbox.h"
#include <stdio.h>

static CircularBuffer audio_buf;

int main(void) {
    printf("=== Test completo ARM ===\n\n");

    // Init
    if (storage_init() != 0) return 1;
    circ_buf_init(&audio_buf);
    mailbox_init();

    // Listar canciones
    SongInfo songs[MAX_SONGS];
    int count = storage_list_songs(songs, MAX_SONGS);
    if (count <= 0) return 1;

    // Parsear primera canción
    printf("\n=== Cargando %s ===\n", songs[0].filename);
    WavInfo info;
    if (wav_parse(songs[0].filename, &info) != 0) return 1;
    wav_print_info(&info);

    // Configurar buffer
    audio_buf.sample_rate     = info.sample_rate;
    audio_buf.num_channels    = info.num_channels;
    audio_buf.bits_per_sample = info.bits_per_sample;

    // Cargar audio en buffer
    storage_open_song(songs[0].filename);
    uint8_t skip[256];
    storage_read_bytes(skip, info.data_offset);

    uint8_t block[BLOCK_SIZE];
    int bytes_read;
    while ((bytes_read = storage_read_bytes(block, BLOCK_SIZE)) > 0)
        if (circ_buf_write_block(&audio_buf, block, bytes_read) < 0) break;
    storage_close_song();

    // Probar mailbox
    printf("\n=== Test Mailbox ===\n");

    mailbox_send_command(CMD_PLAY, 0, 0);
    mailbox_wait_ack(1000);

    mailbox_send_command(CMD_PAUSE, 0, 0);
    mailbox_wait_ack(1000);

    mailbox_send_command(CMD_STOP, 0, 0);
    mailbox_wait_ack(1000);

    printf("\n=== Estado final ===\n");
    printf("Buffer libre: %d bloques\n", circ_buf_free_blocks(&audio_buf));
    printf("Status NIOS:  %s\n",
           mailbox_status_str(mailbox_read_status()));

    return 0;
}