#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>

#define LW_BRIDGE_BASE 0xFF200000
#define MAP_SIZE       0x40000

#define HEX_HI_OFFSET  0x31040
#define HEX_LO_OFFSET  0x31050

#define SHARED_OFFSET 0x00000

int main()
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);

    volatile uint32_t *lw =
        mmap(NULL, MAP_SIZE,
             PROT_READ | PROT_WRITE,
             MAP_SHARED,
             fd,
             LW_BRIDGE_BASE);

    volatile uint32_t *shared =
    (volatile uint32_t *)((char*)lw);

    shared[0x1000/4] = 0x12345678;

    return 0;
}