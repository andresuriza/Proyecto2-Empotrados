#ifndef WAV_PARSER_H
#define WAV_PARSER_H

#include <stdint.h>

#define WAV_OK           0
#define WAV_ERR_FORMAT  -1
#define WAV_ERR_READ    -2

typedef struct {
    // Formato de audio
    uint16_t num_channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t byte_rate;
    uint16_t block_align;

    // Datos
    uint32_t data_offset;    // byte donde empiezan las muestras
    uint32_t data_size;      // tamaño en bytes de las muestras
    uint32_t duration_sec;   // duración calculada

    // Metadatos opcionales
    char title[64];
    char artist[64];
    char album[64];
} WavInfo;

int wav_parse(const char* filename, WavInfo* info);
void wav_print_info(const WavInfo* info);

#endif