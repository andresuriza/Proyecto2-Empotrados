#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <stdint.h>

// Tamaño del buffer en bytes (~63KB de la memoria compartida)
#define AUDIO_BUF_SIZE  (63 * 1024)

// Número de bloques y tamaño de cada bloque
#define BLOCK_SIZE      4096    // 4KB por bloque
#define NUM_BLOCKS      (AUDIO_BUF_SIZE / BLOCK_SIZE)

// Estados de cada bloque
#define BLOCK_FREE      0   // ARM puede escribir
#define BLOCK_READY     1   // NIOS puede leer
#define BLOCK_READING   2   // NIOS está leyendo

typedef struct {
    // Control del buffer
    volatile uint32_t write_idx;        // índice donde ARM escribe
    volatile uint32_t read_idx;         // índice donde NIOS lee
    volatile uint32_t block_state[NUM_BLOCKS];  // estado de cada bloque

    // Info de audio actual
    uint32_t sample_rate;
    uint16_t num_channels;
    uint16_t bits_per_sample;
    uint32_t total_samples;

    // Datos de audio
    uint8_t  data[AUDIO_BUF_SIZE];
} CircularBuffer;

// Funciones
void circ_buf_init(CircularBuffer* buf);
int  circ_buf_write_block(CircularBuffer* buf, uint8_t* data, uint32_t size);
int  circ_buf_read_block(CircularBuffer* buf, uint8_t* out, uint32_t size);
int  circ_buf_is_full(CircularBuffer* buf);
int  circ_buf_is_empty(CircularBuffer* buf);
int  circ_buf_free_blocks(CircularBuffer* buf);

#endif