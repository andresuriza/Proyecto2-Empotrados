// vga_test.c — prueba del vga_text_controller (independiente del audio).
// Limpia la pantalla y escribe texto, para validar que la VGA renderiza.
//
// El controlador es un text buffer 80x30. Cada celda = 1 word de 32 bits:
//   bits [7:0]   = ASCII (0x20..0x7E)
//   bits [11:8]  = color de letra (fg), paleta 0..15
//   bits [15:12] = color de fondo  (bg), paleta 0..15
//   direccion de celda = fila*80 + col   (fila 0..29, col 0..79)
// Paleta: 0 negro, 2 verde, 14 amarillo, 15 blanco (ver vga_text_controller.sv).
//
// Compilar en la placa:  gcc -o vga_test vga_test.c
// Correr:                ./vga_test

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define LW_BRIDGE_BASE  0xFF200000UL
#define MAP_SIZE        0x80000
#define VGA_OFFSET      0x10000      // base del vga_text_controller en el LW bridge

#define COLS  80
#define ROWS  30

static volatile uint32_t *vga;

// arma una celda: ascii + colores
static inline uint32_t cell(char ch, uint8_t fg, uint8_t bg) {
    return ((uint32_t)(bg & 0xF) << 12) | ((uint32_t)(fg & 0xF) << 8) | (uint8_t)ch;
}

static void vga_clear(uint8_t fg, uint8_t bg) {
    uint32_t blank = cell(' ', fg, bg);
    for (int i = 0; i < COLS * ROWS; i++) vga[i] = blank;
}

static void vga_print(int row, int col, const char *s, uint8_t fg, uint8_t bg) {
    int idx = row * COLS + col;
    for (int i = 0; s[i] && (col + i) < COLS; i++)
        vga[idx + i] = cell(s[i], fg, bg);
}

int main(void) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open /dev/mem"); return 1; }

    volatile uint32_t *lw = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, LW_BRIDGE_BASE);
    if (lw == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    vga = (volatile uint32_t *)((char *)lw + VGA_OFFSET);

    // limpiar (letra blanca, fondo negro) y escribir texto de prueba
    vga_clear(15, 0);
    vga_print(2,  5, "VGA TEXT CONTROLLER OK", 14, 0);   // amarillo
    vga_print(5,  5, "Reproductor de Audio - CE1113", 15, 0);
    vga_print(7,  5, "Cancion: ---", 2, 0);              // verde
    vga_print(8,  5, "Artista: ---", 2, 0);
    vga_print(10, 5, "Si ves esto, la VGA funciona.", 15, 0);

    printf("Texto escrito al VGA (0x%lX). Revisa la pantalla.\n",
           LW_BRIDGE_BASE + VGA_OFFSET);

    munmap((void *)lw, MAP_SIZE);
    close(fd);
    return 0;
}
