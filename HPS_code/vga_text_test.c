// ============================================================================
//  vga_text_test.c  —  ARM (HPS) test for the VGA text/metadata controller
// ----------------------------------------------------------------------------
//  Writes sample song metadata into the VGA text buffer to prove the 8x16 text
//  renderer works end-to-end. Reuses the proven mmap/addressing from vga_test.c.
//
//  ── ADDRESS / CELL MAP (see vga_text_controller.sv) ─────────────────────────
//   Bridge          : HPS-to-FPGA LIGHTWEIGHT  (base 0xFF200000)
//   Component offset : 0x00010000   ->  VGA base = 0xFF210000
//   addressUnits    : WORDS  =>  cell N is at byte offset N*4
//   Text region     : COLS x ROWS = 40 x 8  (320 cells)
//                       cell_index = row*40 + col   (col 0..39, row 0..7)
//   Cell (16 bits)  : [7:0] ASCII | [11:8] fg colour | [15:12] bg colour
//   Palette         : 0 black 1 blue 2 green 3 cyan 4 red 5 magenta 6 brown
//                     7 lt-gray 8 dk-gray 9 br-blue 10 br-green 11 br-cyan
//                     12 br-red 13 br-magenta 14 yellow 15 white
//
//  Compile (cross, DE1-SoC HPS Linux / Linaro toolchain):
//    arm-linux-gnueabihf-gcc -O2 -o vga_text_test HPS_code/vga_text_test.c
//  or natively on the board:
//    gcc -O2 -o vga_text_test HPS_code/vga_text_test.c
//
//  Run on the board as root (needs /dev/mem):
//    ./vga_text_test
// ============================================================================

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

#define LW_BRIDGE_BASE   0xFF200000UL   // HPS-to-FPGA Lightweight bridge base
#define VGA_OFFSET       0x00010000UL   // vga_text_controller base within bridge
#define MAP_SPAN         0x00020000UL   // 128 KB: covers offset 0x10000 + buffer

#define COLS             40
#define ROWS             8
#define CELLS            (COLS * ROWS)  // 320

// Palette indices
#define C_BLACK   0
#define C_GREEN   2
#define C_RED     4
#define C_CYAN    3
#define C_YELLOW  14
#define C_WHITE   15

static volatile uint32_t *vga;          // -> cell 0 of the text buffer

// Build a 16-bit cell: {bg[15:12], fg[11:8], ascii[7:0]}
static inline uint32_t cell(char ch, uint8_t fg, uint8_t bg) {
    return ((uint32_t)(bg & 0xF) << 12) | ((uint32_t)(fg & 0xF) << 8)
         | (uint32_t)(uint8_t)ch;
}

// Fill the whole text region with spaces of the given colours.
static void vga_clear(uint8_t fg, uint8_t bg) {
    uint32_t blank = cell(' ', fg, bg);
    for (int i = 0; i < CELLS; i++)
        vga[i] = blank;
    __sync_synchronize();
}

// Write a string at (row, col); clipped to the region width.
static void vga_print(int row, int col, const char *s, uint8_t fg, uint8_t bg) {
    if (row < 0 || row >= ROWS) return;
    int base = row * COLS;
    for (int i = 0; s[i] != '\0' && (col + i) < COLS; i++)
        vga[base + col + i] = cell(s[i], fg, bg);
    __sync_synchronize();
    printf("  row %d col %d: \"%s\" (fg=%u bg=%u)\n", row, col, s, fg, bg);
}

int main(void) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem (run as root?)");
        return 1;
    }

    // Page-align the mmap offset (bridge base is already page-aligned; fold any
    // in-page remainder back into the pointer).
    long pagesize = sysconf(_SC_PAGE_SIZE);
    unsigned long aligned_base = LW_BRIDGE_BASE & ~((unsigned long)pagesize - 1UL);
    unsigned long in_page      = LW_BRIDGE_BASE - aligned_base;   // 0
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

    // Clear screen: white on black.
    vga_clear(C_WHITE, C_BLACK);

    // Sample metadata.
    printf("Writing metadata:\n");
    vga_print(0, 0, "TITULO: TEST SONG",     C_WHITE,  C_BLACK);
    vga_print(1, 0, "ARTISTA: TEST ARTIST",  C_CYAN,   C_BLACK);
    vga_print(2, 0, "ALBUM: TEST ALBUM",     C_GREEN,  C_BLACK);
    vga_print(4, 0, "CANCION 3 / 10",        C_WHITE,  C_BLACK);
    // Coloured line proves the attribute byte (yellow text on red background).
    vga_print(6, 0, "FILTER: LOWPASS",       C_YELLOW, C_RED);

    if (munmap(map, span) != 0)
        perror("munmap");
    close(fd);

    printf("Done. Check the top-left of the VGA screen.\n");
    return 0;
}
