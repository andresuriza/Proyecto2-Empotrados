#ifndef VGA_H
#define VGA_H

#include <stdint.h>
#include "wav_parser.h"

// ── Paleta de 16 colores del vga_text_controller.sv (índice 0..15) ──
enum {
    VGA_BLACK = 0, VGA_BLUE,  VGA_GREEN,  VGA_CYAN,
    VGA_RED,       VGA_MAGENTA, VGA_BROWN, VGA_LGRAY,
    VGA_DGRAY,     VGA_BBLUE, VGA_BGREEN, VGA_BCYAN,
    VGA_BRED,      VGA_BMAGENTA, VGA_YELLOW, VGA_WHITE
};

// Limpia toda la pantalla con el color de fondo dado.
void vga_clear(uint8_t bg);

// Escribe una cadena en (fila, col) con color de letra (fg) y fondo (bg).
void vga_print(int row, int col, const char* s, uint8_t fg, uint8_t bg);

// Pinta la pantalla de "ahora suena": título / artista / álbum / duración /
// número de canción. Si el WAV no trae un campo, usa el filename / "Desconocido".
void vga_show_track(const WavInfo* w, const char* filename, int idx, int total);

#endif
