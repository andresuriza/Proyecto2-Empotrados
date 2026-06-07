// ============================================================================
//  vga_test.c  —  ARM (HPS) end-to-end test for the simple VGA controller
// ----------------------------------------------------------------------------
//  Runs on the DE1-SoC ARM HPS under Linux. Drives the memory-free VGA test
//  controller (component "vga_text_controller") to prove the VGA chain works:
//  it cycles through solid colours, the 8-bar test pattern and a checkerboard,
//  printing each step so the screen can be correlated with the console.
//
//  Address map (see vga_text_controller.sv):
//    component sits behind the HPS-to-FPGA Lightweight bridge (0xFF200000)
//    at offset 0x10000  ->  absolute 0xFF210000.
//    Avalon slave is WORD-addressed: word 0 = byte 0x0, word 1 = byte 0x4.
//      word 0  BG_COLOR : [23:0] = {R[7:0], G[7:0], B[7:0]}
//      word 1  MODE     : 0 = solid, 1 = colour bars, 2 = checkerboard
//
//  Compile (cross, DE1-SoC HPS Linux / Linaro toolchain):
//    arm-linux-gnueabihf-gcc -O2 -o vga_test HPS_code/vga_test.c
//  or natively on the board:
//    gcc -O2 -o vga_test HPS_code/vga_test.c
//
//  Run on the board as root (needs /dev/mem):
//    ./vga_test
// ============================================================================

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

#define LW_BRIDGE_BASE  0xFF200000UL    // HPS-to-FPGA Lightweight bridge
#define MAP_SIZE        0x80000         // 512 KB (covers VGA at 0x10000)
#define VGA_OFFSET      0x10000         // vga_text_controller base in the bridge

// Avalon WORD indices -> uint32_t array indices (word 0 = byte 0x0, word 1 = 0x4)
#define REG_BG_COLOR    0
#define REG_MODE        1

// MODE values
#define MODE_SOLID      0
#define MODE_BARS       1
#define MODE_CHECKER    2

// BG_COLOR helper: pack R,G,B into {R[23:16], G[15:8], B[7:0]}
#define RGB(r, g, b)    (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

int main(void) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem (run as root?)");
        return 1;
    }

    void *map = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                     fd, LW_BRIDGE_BASE);
    if (map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    volatile uint32_t *vga = (volatile uint32_t *)((char *)map + VGA_OFFSET);

    printf("VGA test @ 0x%08lX (LW bridge 0x%08lX + 0x%05X)\n",
           LW_BRIDGE_BASE + VGA_OFFSET, LW_BRIDGE_BASE, VGA_OFFSET);

    // Step 1: solid RED
    printf("[1/6] MODE=solid, BG=RED   -> full red screen\n");
    vga[REG_MODE]     = MODE_SOLID;
    vga[REG_BG_COLOR] = RGB(0xFF, 0x00, 0x00);
    sleep(2);

    // Step 2: solid GREEN
    printf("[2/6] MODE=solid, BG=GREEN -> full green screen\n");
    vga[REG_BG_COLOR] = RGB(0x00, 0xFF, 0x00);
    sleep(2);

    // Step 3: solid BLUE
    printf("[3/6] MODE=solid, BG=BLUE  -> full blue screen\n");
    vga[REG_BG_COLOR] = RGB(0x00, 0x00, 0xFF);
    sleep(2);

    // Step 4: 8 vertical colour bars
    printf("[4/6] MODE=bars            -> 8 vertical colour bars\n");
    vga[REG_MODE] = MODE_BARS;
    sleep(2);

    // Step 5: checkerboard
    printf("[5/6] MODE=checkerboard    -> checkerboard pattern\n");
    vga[REG_MODE] = MODE_CHECKER;
    sleep(2);

    // Step 6: back to a solid colour and finish
    printf("[6/6] MODE=solid, BG=BLACK -> done\n");
    vga[REG_MODE]     = MODE_SOLID;
    vga[REG_BG_COLOR] = RGB(0x00, 0x00, 0x00);

    if (munmap(map, MAP_SIZE) != 0)
        perror("munmap");
    close(fd);

    printf("VGA test complete.\n");
    return 0;
}
