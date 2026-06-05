#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include <stdio.h>

#define SECTOR_SIZE 512
static FILE* disk_img = NULL;

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    disk_img = fopen("sd_card.img", "rb");
    if (!disk_img) {
        printf("[DISKIO] Error: no se encontro sd_card.img\n");
        return STA_NOINIT;
    }
    printf("[DISKIO] sd_card.img abierta correctamente\n");
    return 0;
}

DSTATUS disk_status(BYTE pdrv) {
    return (disk_img) ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (!disk_img) return RES_NOTRDY;
    fseek(disk_img, (long)(sector * SECTOR_SIZE), SEEK_SET);
    if (fread(buff, SECTOR_SIZE, count, disk_img) != count)
        return RES_ERROR;
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    return RES_WRPRT;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD*)buff = SECTOR_SIZE;
            return RES_OK;
        case GET_SECTOR_COUNT:
            fseek(disk_img, 0, SEEK_END);
            *(LBA_t*)buff = ftell(disk_img) / SECTOR_SIZE;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD*)buff = 1;
            return RES_OK;
    }
    return RES_PARERR;
}

// Timestamp requerido por FatFs (año 2026, mes 1, dia 1)
DWORD get_fattime(void) {
    return ((DWORD)(2026 - 1980) << 25)
         | ((DWORD)1  << 21)
         | ((DWORD)1  << 16)
         | ((DWORD)0  << 11)
         | ((DWORD)0  << 5)
         | ((DWORD)0  >> 1);
}