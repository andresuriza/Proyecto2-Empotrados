#include "../lib/fatfs/ff.h"
#include "../lib/fatfs/diskio.h"
#include <stdio.h>   // DEBUG

// ══════════════════════════════════════════════════════════
// HPS SDMMC Controller — Cyclone V
// Manual: Cyclone V HPS Technical Reference Manual
// Sección 20: SD/MMC Controller
// ══════════════════════════════════════════════════════════

// ── Dirección base — COMPLETAR con Platform Designer ──────
#define SDMMC_BASE        0xFF704000UL  // dirección típica Cyclone V HPS

// ── Registros del controlador ─────────────────────────────
#define SDMMC_CTRL        (*(volatile uint32_t*)(SDMMC_BASE + 0x000))
#define SDMMC_PWREN       (*(volatile uint32_t*)(SDMMC_BASE + 0x004))
#define SDMMC_CLKDIV      (*(volatile uint32_t*)(SDMMC_BASE + 0x008))
#define SDMMC_CLKSRC      (*(volatile uint32_t*)(SDMMC_BASE + 0x00C))
#define SDMMC_CLKENA      (*(volatile uint32_t*)(SDMMC_BASE + 0x010))
#define SDMMC_TMOUT       (*(volatile uint32_t*)(SDMMC_BASE + 0x014))
#define SDMMC_CTYPE       (*(volatile uint32_t*)(SDMMC_BASE + 0x018))
#define SDMMC_BLKSIZ      (*(volatile uint32_t*)(SDMMC_BASE + 0x01C))
#define SDMMC_BYTCNT      (*(volatile uint32_t*)(SDMMC_BASE + 0x020))
#define SDMMC_INTMASK     (*(volatile uint32_t*)(SDMMC_BASE + 0x024))
#define SDMMC_CMDARG      (*(volatile uint32_t*)(SDMMC_BASE + 0x028))
#define SDMMC_CMD         (*(volatile uint32_t*)(SDMMC_BASE + 0x02C))
#define SDMMC_RESP0       (*(volatile uint32_t*)(SDMMC_BASE + 0x030))
#define SDMMC_RESP1       (*(volatile uint32_t*)(SDMMC_BASE + 0x034))
#define SDMMC_RESP2       (*(volatile uint32_t*)(SDMMC_BASE + 0x038))
#define SDMMC_RESP3       (*(volatile uint32_t*)(SDMMC_BASE + 0x03C))
#define SDMMC_MINTSTS     (*(volatile uint32_t*)(SDMMC_BASE + 0x040))
#define SDMMC_RINTSTS     (*(volatile uint32_t*)(SDMMC_BASE + 0x044))
#define SDMMC_STATUS      (*(volatile uint32_t*)(SDMMC_BASE + 0x048))
#define SDMMC_FIFOTH      (*(volatile uint32_t*)(SDMMC_BASE + 0x04C))
#define SDMMC_DATA        (*(volatile uint32_t*)(SDMMC_BASE + 0x200))
// BMOD (Bus Mode): bit 7 = DE (IDMAC Enable). U-Boot leaves DE=1; must clear it
// so CMD17 data routes to FIFO (polling), not to stale DMA descriptors.
#define SDMMC_BMOD        (*(volatile uint32_t*)(SDMMC_BASE + 0x080))
#define BMOD_SWR          (1 << 0)   // software reset IDMAC
#define BMOD_DE           (1 << 7)   // IDMAC enable

// ── Bits de control ───────────────────────────────────────
#define CTRL_RESET        (1 << 0)
#define CTRL_FIFO_RESET   (1 << 1)
#define CTRL_DMA_RESET    (1 << 2)
#define CTRL_INT_EN       (1 << 4)

// ── Bits de CMD ───────────────────────────────────────────
#define CMD_START         (1 << 31)
#define CMD_USE_HOLD      (1 << 29)
#define CMD_UPDATE_CLK    (1 << 21)
#define CMD_SEND_INIT     (1 << 15)
#define CMD_STOP_ABORT    (1 << 14)
#define CMD_WAIT_PRVDATA  (1 << 13)
#define CMD_SEND_AUTOSTOP (1 << 12)
#define CMD_RW_WRITE      (1 << 10)  // 0=read, 1=write
#define CMD_STREAM_MODE   (1 << 11)  // 0=block transfer, 1=stream  (¡usar 0!)
#define CMD_DATA_EXP      (1 << 9)
#define CMD_RESP_LONG     (1 << 7)
#define CMD_RESP_EXP      (1 << 6)

// ── Bits de STATUS ────────────────────────────────────────
#define STATUS_DATA_BUSY  (1 << 9)
#define STATUS_FIFO_EMPTY (1 << 2)

// ── Bits de RINTSTS ───────────────────────────────────────
#define INT_DTO           (1 << 3)   // Data Transfer Over
#define INT_CMD_DONE      (1 << 2)
#define INT_RE            (1 << 1)   // Response Error
#define INT_CD            (1 << 0)   // Card Detect

#define SECTOR_SIZE       512
#define TIMEOUT_LIMIT     100000

// ── Estado interno ────────────────────────────────────────
static int card_initialized = 0;
static int card_sdhc        = 0;  // 1 si es SDHC/SDXC

// ── Helpers ───────────────────────────────────────────────

static void sdmmc_wait_cmd_done(void) {
    uint32_t timeout = TIMEOUT_LIMIT;
    while (!(SDMMC_RINTSTS & INT_CMD_DONE) && timeout--);
    SDMMC_RINTSTS = INT_CMD_DONE;
}

static void sdmmc_wait_data_done(void) {
    uint32_t timeout = TIMEOUT_LIMIT;
    while (!(SDMMC_RINTSTS & INT_DTO) && timeout--);
    SDMMC_RINTSTS = INT_DTO;
}

static uint32_t sdmmc_send_cmd(uint32_t cmd_idx, uint32_t arg,
                                uint32_t flags) {
    SDMMC_RINTSTS = 0xFFFFFFFF;  // limpiar interrupciones
    SDMMC_CMDARG  = arg;
    SDMMC_CMD     = CMD_START | CMD_USE_HOLD | flags | cmd_idx;
    sdmmc_wait_cmd_done();
    return SDMMC_RESP0;
}

static void sdmmc_update_clock(void) {
    SDMMC_CMD = CMD_START | CMD_UPDATE_CLK | CMD_WAIT_PRVDATA;
    uint32_t timeout = TIMEOUT_LIMIT;
    while ((SDMMC_CMD & CMD_START) && timeout--);
}

// ── Inicialización ────────────────────────────────────────

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;

    // Reset del controlador + deshabilitar IDMAC explícitamente.
    // U-Boot deja BMOD.DE=1 (DMA enable); sin esto CMD17 manda datos al DMA
    // y el FIFO queda siempre vacío → hang en el polling loop.
    SDMMC_CTRL = CTRL_RESET | CTRL_FIFO_RESET | CTRL_DMA_RESET;
    uint32_t timeout = TIMEOUT_LIMIT;
    while ((SDMMC_CTRL & (CTRL_RESET | CTRL_FIFO_RESET)) && timeout--);
    SDMMC_BMOD = BMOD_SWR;               // reset IDMAC state machine
    for (volatile int d = 0; d < 1000; d++) {}
    SDMMC_BMOD = 0;                      // IDMAC completamente deshabilitado

    // Configurar clock lento para inicialización (~400KHz)
    SDMMC_CLKENA  = 0;
    sdmmc_update_clock();
    SDMMC_CLKDIV  = 0xFF;  // dividir clock para init
    SDMMC_CLKSRC  = 0;
    SDMMC_CLKENA  = 1;
    sdmmc_update_clock();

    // Timeout y tamaño de bloque
    SDMMC_TMOUT  = 0xFFFFFFFF;
    SDMMC_BLKSIZ = SECTOR_SIZE;
    SDMMC_PWREN  = 1;

    // Secuencia de inicialización SD
    // CMD0 — GO_IDLE_STATE
    sdmmc_send_cmd(0, 0, CMD_SEND_INIT);

    // CMD8 — SEND_IF_COND (verifica voltaje, detecta SDHC)
    uint32_t resp = sdmmc_send_cmd(8, 0x1AA, CMD_RESP_EXP);
    card_sdhc = (resp == 0x1AA) ? 1 : 0;

    // ACMD41 — SD_SEND_OP_COND (esperar que la tarjeta esté lista)
    timeout = 1000;
    uint32_t ocr = 0;
    while (timeout--) {
        sdmmc_send_cmd(55, 0, CMD_RESP_EXP);  // CMD55 (APP_CMD)
        uint32_t acmd41_arg = card_sdhc ? 0x40FF8000 : 0x00FF8000;
        ocr = sdmmc_send_cmd(41, acmd41_arg, CMD_RESP_EXP);
        if (ocr & (1 << 31)) break;  // tarjeta lista
    }

    card_sdhc = (ocr & (1 << 30)) ? 1 : 0;  // bit CCS

    printf("[SD] ocr=0x%08lX card_sdhc=%d\r\n", ocr, card_sdhc);

    // CMD2 — ALL_SEND_CID
    sdmmc_send_cmd(2, 0, CMD_RESP_EXP | CMD_RESP_LONG);
    printf("[SD] CMD2 done\r\n");

    // CMD3 — SEND_RELATIVE_ADDR
    uint32_t rca = sdmmc_send_cmd(3, 0, CMD_RESP_EXP) & 0xFFFF0000;
    printf("[SD] RCA=0x%08lX\r\n", rca);

    // CMD7 — SELECT_CARD
    sdmmc_send_cmd(7, rca, CMD_RESP_EXP);
    printf("[SD] CMD7 done - card selected\r\n");

    // Subir clock a velocidad normal
    SDMMC_CLKENA = 0;
    sdmmc_update_clock();
    SDMMC_CLKDIV = 4;  // COMPLETAR según clock del sistema
    SDMMC_CLKENA = 1;
    sdmmc_update_clock();

    // Bus width: 1 bit. (4 bits requiere mandar ACMD6 a la tarjeta primero;
    // sin eso, el controlador en 4-bit no recibe datos -> FIFO vacio -> ceros.
    // 1 bit es mas lento pero no necesita ACMD6 -> robusto para bring-up.)
    SDMMC_CTYPE = 0;

    card_initialized = 1;

    printf("[SD] Init completo sdhc=%d\r\n", card_sdhc);
    return 0;
}

// ── Estado ────────────────────────────────────────────────

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0)        return STA_NOINIT;
    if (!card_initialized) return STA_NOINIT;
    return 0;
}

// ── Lectura de sectores ───────────────────────────────────

// Static aligned buffer: FatFS passes BYTE* buffers that may not be 4-byte
// aligned, but the SDMMC FIFO requires 32-bit reads. We read into this
// aligned buffer first, then memcpy to the caller's (possibly unaligned) dst.
static uint32_t s_aligned_buf[SECTOR_SIZE / 4];

static DRESULT read_one_sector(uint32_t lba, BYTE* dst) {
    uint32_t t;

    // 1. Esperar a que la tarjeta no este ocupada en la linea de datos
    t = TIMEOUT_LIMIT;
    while ((SDMMC_STATUS & STATUS_DATA_BUSY) && t--);

    // 2. Reset del FIFO y esperar el auto-clear
    SDMMC_CTRL |= CTRL_FIFO_RESET;
    t = TIMEOUT_LIMIT;
    while ((SDMMC_CTRL & CTRL_FIFO_RESET) && t--);

    // 3. CLAVE: el FIFO_RESET no vacia la ultima palabra de la transferencia
    //    anterior en este controlador. Drenar explicitamente hasta FIFO vacio,
    //    si no esa palabra stale (p.ej. la firma 55AA) contamina la lectura
    //    siguiente y desplaza los campos BPB -> bps=0 -> FatFS NO_FILESYSTEM.
    while (!(SDMMC_STATUS & STATUS_FIFO_EMPTY)) { (void)SDMMC_DATA; }

    uint32_t addr = card_sdhc ? lba : lba * SECTOR_SIZE;
    SDMMC_BLKSIZ = SECTOR_SIZE;
    SDMMC_BYTCNT = SECTOR_SIZE;

    // Bloque (no stream): bit11=0 transfer_mode=block, bit10=0 read.
    // Antes se ponia CMD_STREAM_MODE (bit11) por error -> el controlador hacia
    // un stream continuo: nunca terminaba por bloque, DTO no se disparaba, el
    // FSM de datos quedaba ocupado y la 2a lectura se desbordaba (HLE).
    sdmmc_send_cmd(17, addr, CMD_RESP_EXP | CMD_DATA_EXP | CMD_WAIT_PRVDATA);

    for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
        uint32_t timeout = TIMEOUT_LIMIT;
        while ((SDMMC_STATUS & STATUS_FIFO_EMPTY) && timeout--);
        if (timeout == 0) {
            printf("[SD] TIMEOUT s=%lu w=%lu RINTSTS=%08lX\r\n",
                   (unsigned long)lba, (unsigned long)i, (unsigned long)SDMMC_RINTSTS);
            return RES_ERROR;
        }
        s_aligned_buf[i] = SDMMC_DATA;
    }

    sdmmc_wait_data_done();

    // 4. Drenar cualquier palabra residual para dejar el FIFO limpio
    while (!(SDMMC_STATUS & STATUS_FIFO_EMPTY)) { (void)SDMMC_DATA; }

    // Safe copy to potentially-unaligned FatFS buffer
    for (uint32_t b = 0; b < SECTOR_SIZE; b++)
        dst[b] = ((uint8_t*)s_aligned_buf)[b];

    return RES_OK;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || !card_initialized) return RES_NOTRDY;

    for (UINT s = 0; s < count; s++) {
        DRESULT r = read_one_sector((uint32_t)(sector + s),
                                    buff + s * SECTOR_SIZE);
        if (r != RES_OK) return r;
    }
    return RES_OK;
}

// ── Escritura — no requerida (solo lectura) ───────────────

DRESULT disk_write(BYTE pdrv, const BYTE* buff,
                   LBA_t sector, UINT count) {
    return RES_WRPRT;
}

// ── Control ───────────────────────────────────────────────

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD*)buff = SECTOR_SIZE;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD*)buff = 1;
            return RES_OK;
        case GET_SECTOR_COUNT:
            // COMPLETAR si se necesita el tamaño total de la SD
            *(LBA_t*)buff = 0;
            return RES_OK;
    }
    return RES_PARERR;
}

// ── get_fattime requerido por FatFs ──────────────────────
#include "fatfs/ff.h"
DWORD get_fattime(void) {
    return ((DWORD)(2026 - 1980) << 25)
         | ((DWORD)6  << 21)
         | ((DWORD)1  << 16);
}