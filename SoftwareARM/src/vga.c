#include "vga.h"
#include "platform.h"
#include <stdio.h>

// Base del controlador de texto VGA por el LW bridge (0xFF210000).
// Word-addressed: cada celda es un word en el índice (fila*80 + col).
// Celda = [15:12]=bg  [11:8]=fg  [7:0]=ASCII.
static volatile uint32_t* const VGA = (volatile uint32_t*)VGA_BASE;

static inline void put_cell(int row, int col, char c, uint8_t fg, uint8_t bg) {
    if (row < 0 || row >= VGA_ROWS || col < 0 || col >= VGA_COLS) return;
    VGA[row * VGA_COLS + col] =
        ((uint32_t)(bg & 0xF) << 12) |
        ((uint32_t)(fg & 0xF) << 8)  |
        (uint32_t)(uint8_t)c;
}

void vga_clear(uint8_t bg) {
    for (int r = 0; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            put_cell(r, c, ' ', VGA_WHITE, bg);
}

void vga_print(int row, int col, const char* s, uint8_t fg, uint8_t bg) {
    for (int i = 0; s[i] && (col + i) < VGA_COLS; i++)
        put_cell(row, col + i, s[i], fg, bg);
}

void vga_show_track(const WavInfo* w, const char* filename, int idx, int total) {
    char line[VGA_COLS + 1];

    vga_clear(VGA_BLUE);
    vga_print(1, 2, "=== Reproductor de Audio SoC ===", VGA_YELLOW, VGA_BLUE);

    snprintf(line, sizeof line, "Titulo : %s",
             w->title[0]  ? w->title  : filename);
    vga_print(4, 2, line, VGA_WHITE, VGA_BLUE);

    snprintf(line, sizeof line, "Artista: %s",
             w->artist[0] ? w->artist : "Desconocido");
    vga_print(5, 2, line, VGA_WHITE, VGA_BLUE);

    snprintf(line, sizeof line, "Album  : %s",
             w->album[0]  ? w->album  : "Desconocido");
    vga_print(6, 2, line, VGA_WHITE, VGA_BLUE);

    snprintf(line, sizeof line, "Duracion: %02lu:%02lu",
             (unsigned long)(w->duration_sec / 60),
             (unsigned long)(w->duration_sec % 60));
    vga_print(8, 2, line, VGA_BCYAN, VGA_BLUE);

    snprintf(line, sizeof line, "Cancion %d / %d", idx, total);
    vga_print(9, 2, line, VGA_BGREEN, VGA_BLUE);
}
