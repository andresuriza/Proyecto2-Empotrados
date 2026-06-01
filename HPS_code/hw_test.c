#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#define HEX_HI_OFFSET  0x31040
#define HEX_LO_OFFSET  0x31050

#define LW_BRIDGE_BASE 0xFF200000
#define MAILBOX_BASE 0xF000
#define MAP_SIZE       0x40000

#define SHARED_OFFSET 0x00000

int main()
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);

    volatile uint32_t *lw = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, LW_BRIDGE_BASE);
    volatile uint32_t *mailbox = (volatile uint32_t*) ((char*) lw + MAILBOX_BASE);

    mailbox[0] = 1;   // PLAY
    mailbox[1] = 7;   // Track 7

    return 0;
}