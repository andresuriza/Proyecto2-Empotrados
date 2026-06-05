#include "wav_parser.h"
#include "storage_manager.h"
#include <stdio.h>
#include <string.h>

// ── Helpers para leer bytes little-endian ────────────────

static uint16_t read_u16(uint8_t* buf) {
    return (uint16_t)(buf[0] | (buf[1] << 8));
}

static uint32_t read_u32(uint8_t* buf) {
    return (uint32_t)(buf[0] | (buf[1] << 8)
                    | (buf[2] << 16) | (buf[3] << 24));
}

// ── Saltar N bytes ───────────────────────────────────────

static void skip_bytes(uint32_t count) {
    uint8_t skip[256];
    while (count > 0) {
        uint32_t to_read = count < 256 ? count : 256;
        storage_read_bytes(skip, to_read);
        count -= to_read;
    }
}

// ── Parser principal ─────────────────────────────────────

int wav_parse(const char* filename, WavInfo* info) {
    uint8_t buf[64];
    memset(info, 0, sizeof(WavInfo));

    if (storage_open_song(filename) != 0)
        return WAV_ERR_READ;

    // ── RIFF Header (12 bytes) ───────────────────────────
    if (storage_read_bytes(buf, 12) != 12) {
        storage_close_song();
        return WAV_ERR_READ;
    }

    if (buf[0]!='R' || buf[1]!='I' || buf[2]!='F' || buf[3]!='F') {
        printf("[WAV] Error: no es un archivo RIFF\n");
        storage_close_song();
        return WAV_ERR_FORMAT;
    }

    if (buf[8]!='W' || buf[9]!='A' || buf[10]!='V' || buf[11]!='E') {
        printf("[WAV] Error: no es formato WAVE\n");
        storage_close_song();
        return WAV_ERR_FORMAT;
    }

    // ── Recorrer chunks ──────────────────────────────────
    int found_fmt  = 0;
    int found_data = 0;
    uint32_t offset = 12;

    while (!found_data) {
        // Leer ID + tamaño del chunk
        if (storage_read_bytes(buf, 8) != 8) break;

        char chunk_id[5];
        memcpy(chunk_id, buf, 4);
        chunk_id[4] = '\0';
        uint32_t chunk_size = read_u32(buf + 4);
        offset += 8;

        // ── Chunk fmt ────────────────────────────────────
        if (memcmp(chunk_id, "fmt ", 4) == 0) {

            // Leer los 16 bytes base del fmt
            uint8_t fmt_buf[40];
            memset(fmt_buf, 0, sizeof(fmt_buf));
            uint32_t to_read = chunk_size < 40 ? chunk_size : 40;
            if (storage_read_bytes(fmt_buf, to_read) != (int)to_read) {
                storage_close_song();
                return WAV_ERR_READ;
            }

            uint16_t audio_format = read_u16(fmt_buf);

            // Aceptar PCM (1) y EXTENSIBLE (0xFFFE)
            if (audio_format != 1 && audio_format != 0xFFFE) {
                printf("[WAV] Error: formato no soportado (format=%d)\n",
                       audio_format);
                storage_close_song();
                return WAV_ERR_FORMAT;
            }

            info->num_channels    = read_u16(fmt_buf + 2);
            info->sample_rate     = read_u32(fmt_buf + 4);
            info->byte_rate       = read_u32(fmt_buf + 8);
            info->block_align     = read_u16(fmt_buf + 12);
            info->bits_per_sample = read_u16(fmt_buf + 14);

            // Si el chunk era más grande que lo que leímos, saltar el resto
            if (chunk_size > to_read)
                skip_bytes(chunk_size - to_read);

            offset += chunk_size;
            found_fmt = 1;
        }

        // ── Chunk LIST/INFO (metadatos) ───────────────────
        else if (memcmp(chunk_id, "LIST", 4) == 0) {
            uint8_t list_type[4];
            if (storage_read_bytes(list_type, 4) != 4) break;

            if (memcmp(list_type, "INFO", 4) == 0) {
                uint32_t remaining = chunk_size - 4;

                while (remaining >= 8) {
                    uint8_t sub[8];
                    if (storage_read_bytes(sub, 8) != 8) break;

                    char sub_id[5];
                    memcpy(sub_id, sub, 4);
                    sub_id[4] = '\0';
                    uint32_t sub_size = read_u32(sub + 4);
                    remaining -= 8;

                    if (sub_size == 0) continue;

                    uint8_t value[64];
                    memset(value, 0, sizeof(value));
                    uint32_t val_read = sub_size < 63 ? sub_size : 63;
                    storage_read_bytes(value, val_read);
                    value[val_read] = '\0';

                    // Saltar bytes restantes si el valor era más largo
                    if (sub_size > val_read)
                        skip_bytes(sub_size - val_read);

                    remaining -= sub_size;

                    // Padding a par
                    if (sub_size % 2 != 0 && remaining > 0) {
                        uint8_t pad;
                        storage_read_bytes(&pad, 1);
                        remaining--;
                    }

                    if      (memcmp(sub_id, "INAM", 4) == 0)
                        strncpy(info->title,  (char*)value, 63);
                    else if (memcmp(sub_id, "IART", 4) == 0)
                        strncpy(info->artist, (char*)value, 63);
                    else if (memcmp(sub_id, "IPRD", 4) == 0)
                        strncpy(info->album,  (char*)value, 63);
                }

                // Saltar bytes sobrantes del LIST
                if (remaining > 0) skip_bytes(remaining);

            } else {
                // LIST de otro tipo — saltar
                skip_bytes(chunk_size - 4);
            }

            offset += chunk_size;
        }

        // ── Chunk data ───────────────────────────────────
        else if (memcmp(chunk_id, "data", 4) == 0) {
            info->data_offset  = offset;
            info->data_size    = chunk_size;

            if (info->byte_rate > 0)
                info->duration_sec = chunk_size / info->byte_rate;

            found_data = 1;
        }

        // ── Chunk desconocido — saltar ───────────────────
        else {
            skip_bytes(chunk_size);
            offset += chunk_size;
        }
    }

    storage_close_song();

    if (!found_fmt) {
        printf("[WAV] Error: chunk fmt no encontrado\n");
        return WAV_ERR_FORMAT;
    }
    if (!found_data) {
        printf("[WAV] Error: chunk data no encontrado\n");
        return WAV_ERR_FORMAT;
    }

    return WAV_OK;
}

// ── Imprimir info ────────────────────────────────────────

void wav_print_info(const WavInfo* info) {
    printf("[WAV] Sample rate:    %lu Hz\n",
           (unsigned long)info->sample_rate);
    printf("[WAV] Canales:        %d (%s)\n",
           info->num_channels,
           info->num_channels == 1 ? "mono" : "stereo");
    printf("[WAV] Bits/muestra:   %d\n",  info->bits_per_sample);
    printf("[WAV] Duracion:       %lu segundos\n",
           (unsigned long)info->duration_sec);
    printf("[WAV] Tamanio datos:  %lu bytes\n",
           (unsigned long)info->data_size);
    printf("[WAV] Offset datos:   %lu bytes\n",
           (unsigned long)info->data_offset);
    if (info->title[0])  printf("[WAV] Titulo:         %s\n", info->title);
    if (info->artist[0]) printf("[WAV] Artista:        %s\n", info->artist);
    if (info->album[0])  printf("[WAV] Album:          %s\n", info->album);
}