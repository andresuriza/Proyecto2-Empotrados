// ============================================================================
//  vga_test.c  —  ARM (HPS) end-to-end test for the simple VGA controller
// ----------------------------------------------------------------------------
//  Runs on the DE1-SoC ARM HPS under Linux. Drives the memory-free VGA test
//  controller (component "vga_text_controller") and verifies each Avalon write
//  by reading the register back, so a black screen can be isolated to either
//  the HW chain or the ARM access path.
//
//  ── ADDRESS AUDIT (verified against hps.qsys + vga_text_controller_hw.tcl) ──
//   Bridge          : HPS-to-FPGA LIGHTWEIGHT bridge   (NOT the 0xC0000000 one)
//                       hps_0.h2f_lw_axi_master  ->  vga_text_controller_0.avl
//   Bridge base     : 0xFF200000
//   Component offset : 0x00010000   (baseAddress assigned by Qsys)
//   addressUnits    : WORDS, readLatency 1  =>  word N is at byte offset N*4
//
//   Resulting absolute byte addresses (compare by hand vs Qsys "Address Map"):
//       VGA component base        = 0xFF200000 + 0x00010000 = 0xFF210000
//       word 0  BG_COLOR @ byte 0x0 -> 0xFF210000
//       word 1  MODE     @ byte 0x4 -> 0xFF210004
//
//   Register contents:
//       BG_COLOR [23:0] = {R[7:0], G[7:0], B[7:0]}   (reads back masked to 24b)
//       MODE     [1:0]  = 0 solid / 1 colour bars / 2 checkerboard (reads back 2b)
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

#define LW_BRIDGE_BASE   0xFF200000UL   // HPS-to-FPGA Lightweight bridge base
#define VGA_OFFSET       0x00010000UL   // vga_text_controller base within bridge
#define MAP_SPAN         0x00020000UL   // 128 KB: covers offset 0x10000 + regs

// Avalon WORD indices -> uint32_t array indices (word 0 = byte 0x0, word 1 = 0x4)
#define REG_BG_COLOR     0              // byte offset 0x0  -> 0xFF210000
#define REG_MODE         1              // byte offset 0x4  -> 0xFF210004

// MODE values
#define MODE_SOLID       0
#define MODE_BARS        1
#define MODE_CHECKER     2

// BG_COLOR helper: pack R,G,B into {R[23:16], G[15:8], B[7:0]}
#define RGB(r, g, b)     (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

// Write a register, flush the store with a full memory barrier, then read it
// back through the same mapping and report. The read-back is the definitive
// check that the Avalon write reached the FPGA slave. 'mask' is the number of
// implemented bits the slave reflects (24 for BG_COLOR, 2 for MODE).
static void reg_write_verify(volatile uint32_t *reg, uint32_t val, uint32_t mask,
                             const char *name) {
    *reg = val;
    __sync_synchronize();           // DMB/DSB: ensure the store completes/orders
    uint32_t rb = *reg;             // read-back (device memory, volatile, ordered)
    const char *flag = ((rb & mask) == (val & mask)) ? "OK" : "MISMATCH (write not reaching FPGA?)";
    printf("    %-8s <= 0x%06X   read-back 0x%08X  [%s]\n", name, val, rb, flag);
}

int main(void) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem (run as root?)");
        return 1;
    }

    // Page-align the mmap offset explicitly. mmap()'s file offset MUST be a
    // multiple of the page size; we align the bridge base down to a page
    // boundary and add the in-page remainder back when computing the pointer.
    long pagesize = sysconf(_SC_PAGE_SIZE);
    unsigned long aligned_base = LW_BRIDGE_BASE & ~((unsigned long)pagesize - 1UL);
    unsigned long in_page      = LW_BRIDGE_BASE - aligned_base;   // 0 (base is aligned)
    size_t span                = in_page + MAP_SPAN;

    void *map = mmap(NULL, span, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                     (off_t)aligned_base);
    if (map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    // Register pointer = mapped_base + (component offset, page remainder folded in)
    volatile uint32_t *vga =
        (volatile uint32_t *)((char *)map + in_page + VGA_OFFSET);

    printf("Bridge base   = 0x%08lX (page-aligned 0x%08lX, span 0x%zX)\n",
           LW_BRIDGE_BASE, aligned_base, span);
    printf("VGA base      = 0x%08lX  (bridge + 0x%05lX)\n",
           LW_BRIDGE_BASE + VGA_OFFSET, VGA_OFFSET);
    printf("BG_COLOR addr = 0x%08lX  (word 0, byte 0x0)\n", LW_BRIDGE_BASE + VGA_OFFSET + 0x0);
    printf("MODE addr     = 0x%08lX  (word 1, byte 0x4)\n", LW_BRIDGE_BASE + VGA_OFFSET + 0x4);

    // Step 1: solid RED
    printf("[1/6] MODE=solid, BG=RED   -> full red screen\n");
    reg_write_verify(&vga[REG_MODE],     MODE_SOLID,            0x3,      "MODE");
    reg_write_verify(&vga[REG_BG_COLOR], RGB(0xFF, 0x00, 0x00), 0xFFFFFF, "BG_COLOR");
    sleep(2);

    // Step 2: solid GREEN
    printf("[2/6] MODE=solid, BG=GREEN -> full green screen\n");
    reg_write_verify(&vga[REG_BG_COLOR], RGB(0x00, 0xFF, 0x00), 0xFFFFFF, "BG_COLOR");
    sleep(2);

    // Step 3: solid BLUE
    printf("[3/6] MODE=solid, BG=BLUE  -> full blue screen\n");
    reg_write_verify(&vga[REG_BG_COLOR], RGB(0x00, 0x00, 0xFF), 0xFFFFFF, "BG_COLOR");
    sleep(2);

    // Step 4: 8 vertical colour bars
    printf("[4/6] MODE=bars            -> 8 vertical colour bars\n");
    reg_write_verify(&vga[REG_MODE], MODE_BARS, 0x3, "MODE");
    sleep(2);

    // Step 5: checkerboard
    printf("[5/6] MODE=checkerboard    -> checkerboard pattern\n");
    reg_write_verify(&vga[REG_MODE], MODE_CHECKER, 0x3, "MODE");
    sleep(2);

    // Step 6: back to clean state and finish
    printf("[6/6] MODE=solid, BG=BLACK -> done\n");
    reg_write_verify(&vga[REG_MODE],     MODE_SOLID, 0x3,      "MODE");
    reg_write_verify(&vga[REG_BG_COLOR], 0x000000,   0xFFFFFF, "BG_COLOR");

    if (munmap(map, span) != 0)
        perror("munmap");
    close(fd);

    printf("VGA test complete.\n");
    return 0;
}
