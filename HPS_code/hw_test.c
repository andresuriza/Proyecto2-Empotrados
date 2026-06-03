#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define LW_BRIDGE_BASE 0xFF200000
#define MAP_SIZE       0x40000

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: ./hw_test <hex_value>\n");
        printf("Example: ./hw_test 123456\n");
        return 1;
    }

    uint32_t val = (uint32_t)strtoul(argv[1], NULL, 16);

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    volatile uint32_t *lw = mmap(NULL, MAP_SIZE,
        PROT_READ | PROT_WRITE, MAP_SHARED, fd, LW_BRIDGE_BASE);

    volatile uint32_t *mailbox = lw;  // offset 0x0000

    mailbox[0] = val;

    // Wait for NIOS ACK
    int timeout = 1000;
    while (mailbox[1] != 0xACE && timeout--) usleep(1000);

    if (timeout > 0)
        printf("NIOS ACK! Displaying: 0x%06X\n", val);
    else
        printf("Timeout — NIOS didn't respond.\n");

    munmap((void*)lw, MAP_SIZE);
    close(fd);
    return 0;
}