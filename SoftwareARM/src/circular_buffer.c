#include "circular_buffer.h"
#include <string.h>
#include <stdio.h>

void circ_buf_init(CircularBuffer* buf) {
    buf->write_idx = 0;
    buf->read_idx  = 0;

    for (int i = 0; i < NUM_BLOCKS; i++)
        buf->block_state[i] = BLOCK_FREE;

    memset(buf->data, 0, AUDIO_BUF_SIZE);
    printf("[CIRC] Buffer inicializado. Bloques: %d x %d bytes\n",
           NUM_BLOCKS, BLOCK_SIZE);
}

// Cuántos bloques libres hay
int circ_buf_free_blocks(CircularBuffer* buf) {
    int free = 0;
    for (int i = 0; i < NUM_BLOCKS; i++)
        if (buf->block_state[i] == BLOCK_FREE) free++;
    return free;
}

int circ_buf_is_full(CircularBuffer* buf) {
    return circ_buf_free_blocks(buf) == 0;
}

int circ_buf_is_empty(CircularBuffer* buf) {
    for (int i = 0; i < NUM_BLOCKS; i++)
        if (buf->block_state[i] == BLOCK_READY) return 0;
    return 1;
}

// ARM escribe un bloque de audio
int circ_buf_write_block(CircularBuffer* buf, uint8_t* data, uint32_t size) {
    if (size > BLOCK_SIZE) size = BLOCK_SIZE;

    // Buscar el próximo bloque libre
    uint32_t idx = buf->write_idx;
    if (buf->block_state[idx] != BLOCK_FREE) {
        printf("[CIRC] Buffer lleno, esperando...\n");
        return -1;
    }

    // Copiar datos al bloque
    uint32_t offset = idx * BLOCK_SIZE;
    memcpy(&buf->data[offset], data, size);

    // Marcar como listo para NIOS
    buf->block_state[idx] = BLOCK_READY;

    // Avanzar índice de escritura
    buf->write_idx = (idx + 1) % NUM_BLOCKS;

    return (int)size;
}

// NIOS lee un bloque de audio
int circ_buf_read_block(CircularBuffer* buf, uint8_t* out, uint32_t size) {
    if (size > BLOCK_SIZE) size = BLOCK_SIZE;

    uint32_t idx = buf->read_idx;
    if (buf->block_state[idx] != BLOCK_READY) {
        return -1;  // no hay datos
    }

    // Marcar como leyendo
    buf->block_state[idx] = BLOCK_READING;

    // Copiar datos al output
    uint32_t offset = idx * BLOCK_SIZE;
    memcpy(out, &buf->data[offset], size);

    // Liberar bloque
    buf->block_state[idx] = BLOCK_FREE;

    // Avanzar índice de lectura
    buf->read_idx = (idx + 1) % NUM_BLOCKS;

    return (int)size;
}