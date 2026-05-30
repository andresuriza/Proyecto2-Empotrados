#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define LW_BRIDGE_BASE 0xFF200000u
#define LW_BRIDGE_SPAN 0x00200000u

#define SHM_WORD0_NIOS_MAGIC 0x00
#define SHM_WORD1_NIOS_COUNT 0x04
#define SHM_WORD2_HPS_MAGIC 0x08
#define SHM_WORD3_HPS_COUNT 0x0C
#define SHM_WORD4_KEYSW 0x10
#define SHM_WORD5_COMMAND 0x14
#define SHM_WORD6_STATUS 0x18

#define NIOS_MAGIC 0x4E494F53u
#define HPS_MAGIC 0x48505321u

static volatile uint32_t *map_lw_bridge(void)
{
    const off_t base = LW_BRIDGE_BASE;
    const size_t span = LW_BRIDGE_SPAN;

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0)
    {
        perror("open /dev/mem");
        return NULL;
    }

    void *addr = mmap(NULL, span, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base);
    close(fd);

    if (addr == MAP_FAILED)
    {
        perror("mmap");
        return NULL;
    }

    return (volatile uint32_t *)addr;
}

static void unmap_lw_bridge(volatile uint32_t *base)
{
    if (base != NULL)
    {
        munmap((void *)base, LW_BRIDGE_SPAN);
    }
}

static inline volatile uint32_t *mailbox(volatile uint32_t *lw_base)
{
    return (volatile uint32_t *)((volatile uint8_t *)lw_base + 0x0);
}

int main(void)
{
    volatile uint32_t *lw_base = map_lw_bridge();
    if (lw_base == NULL)
    {
        return EXIT_FAILURE;
    }

    volatile uint32_t *mb = mailbox(lw_base);
    uint32_t heartbeat = 0;

    printf("Proyecto2 HPS mailbox test\n");
    printf("- mapped LW bridge at 0x%08X\n", LW_BRIDGE_BASE);

    mb[SHM_WORD2_HPS_MAGIC / 4] = HPS_MAGIC;
    mb[SHM_WORD3_HPS_COUNT / 4] = heartbeat;
    mb[SHM_WORD5_COMMAND / 4] = 0xA5000000u;
    mb[SHM_WORD6_STATUS / 4] = 0x48505321u;

    for (;;)
    {
        uint32_t nios_magic = mb[SHM_WORD0_NIOS_MAGIC / 4];
        uint32_t nios_count = mb[SHM_WORD1_NIOS_COUNT / 4];
        uint32_t keysw = mb[SHM_WORD4_KEYSW / 4];

        mb[SHM_WORD2_HPS_MAGIC / 4] = HPS_MAGIC;
        mb[SHM_WORD3_HPS_COUNT / 4] = heartbeat;
        mb[SHM_WORD5_COMMAND / 4] = 0xA5000000u | (heartbeat & 0xFFFFu);

        printf("heartbeat=%u NIOS_MAGIC=0x%08X NIOS_COUNT=%u KEYSW=0x%08X\n",
               heartbeat, nios_magic, nios_count, keysw);

        if (nios_magic != NIOS_MAGIC)
        {
            printf("warning: Nios signature not present yet\n");
        }

        heartbeat++;
        sleep(1);
    }

    unmap_lw_bridge(lw_base);
    return EXIT_SUCCESS;
}