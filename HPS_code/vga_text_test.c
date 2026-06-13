#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

#define LW_BRIDGE_BASE   0xFF200000UL
#define VGA_OFFSET       0x00010000UL
#define MAP_SPAN         0x00020000UL

#define COLS             40
#define ROWS             8
#define CELLS            (COLS * ROWS)

#define C_BLACK   0
#define C_GREEN   2
#define C_RED     4
#define C_CYAN    3
#define C_YELLOW  14
#define C_WHITE   15

static volatile uint32_t *vga;

// Construye una celda de 16 bits para el controlador VGA
static inline uint32_t cell(char ch, uint8_t fg, uint8_t bg) {
    return ((uint32_t)(bg & 0xF) << 12) | ((uint32_t)(fg & 0xF) << 8)
         | (uint32_t)(uint8_t)ch;
}

// Limpia la pantalla VGA completa con un color de fondo específico
static void vga_clear(uint8_t fg, uint8_t bg) {
    uint32_t blank = cell(' ', fg, bg);
    for (int i = 0; i < CELLS; i++)
        vga[i] = blank;
    __sync_synchronize();
}

// Escribe una cadena de texto en una posición específica de la VGA
static void vga_print(int row, int col, const char *s, uint8_t fg, uint8_t bg) {
    if (row < 0 || row >= ROWS) return;
    int base = row * COLS;
    for (int i = 0; s[i] != '\0' && (col + i) < COLS; i++)
        vga[base + col + i] = cell(s[i], fg, bg);
    __sync_synchronize();
    printf("  row %d col %d: \"%s\" (fg=%u bg=%u)\n", row, col, s, fg, bg);
}

// Inicializa el controlador VGA y muestra metadatos de ejemplo
int main(void) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem (run as root?)");
        return 1;
    }

    long pagesize = sysconf(_SC_PAGE_SIZE);
    unsigned long aligned_base = LW_BRIDGE_BASE & ~((unsigned long)pagesize - 1UL);
    unsigned long in_page      = LW_BRIDGE_BASE - aligned_base;
    size_t span                = in_page + MAP_SPAN;

    void *map = mmap(NULL, span, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                     (off_t)aligned_base);
    if (map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    vga = (volatile uint32_t *)((char *)map + in_page + VGA_OFFSET);

    printf("VGA text buffer @ 0x%08lX (40x8 cells, word-addressed)\n",
           LW_BRIDGE_BASE + VGA_OFFSET);

    vga_clear(C_WHITE, C_BLACK);

    printf("Writing metadata:\n");
    vga_print(0, 0, "TITULO: TEST SONG",     C_WHITE,  C_BLACK);
    vga_print(1, 0, "ARTISTA: TEST ARTIST",  C_CYAN,   C_BLACK);
    vga_print(2, 0, "ALBUM: TEST ALBUM",     C_GREEN,  C_BLACK);
    vga_print(4, 0, "CANCION 3 / 10",        C_WHITE,  C_BLACK);
    vga_print(6, 0, "FILTER: LOWPASS",       C_YELLOW, C_RED);

    if (munmap(map, span) != 0)
        perror("munmap");
    close(fd);

    printf("Done. Check the top-left of the VGA screen.\n");
    return 0;
}