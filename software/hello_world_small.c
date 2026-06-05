#include <stdio.h>
#include <stdint.h>
#include "system.h"
#include "io.h"
#include "sys/alt_irq.h"
#include "altera_avalon_pio_regs.h"
#include "altera_avalon_timer_regs.h"

// --- Mailbox (debe coincidir con hw_test.c) ---
#define IDX_CMD         0
#define IDX_HEAD        1
#define IDX_TAIL        2
#define BUF_WORD_START  64
#define BUF_WORDS       16320

// --- Audio IP offsets (desde AUDIO_0_BASE) ---
#define AUDIO_CTRL      0   // byte offset 0
#define AUDIO_FIFO      4   // byte offset 4: [23:16]=left space [31:24]=right space
#define AUDIO_LEFT      8   // byte offset 8: DAC left
#define AUDIO_RIGHT     12  // byte offset 12: DAC right

// --- Estado compartido ---
static volatile uint32_t key_event = 0;
static volatile uint32_t tick_ms   = 0;
static volatile uint32_t sw_event  = 0;

static const uint8_t seg7[16] = {
    0x40, 0x79, 0x24, 0x30, 0x19,
    0x12, 0x02, 0x78, 0x00, 0x10,
    0x08, 0x03, 0x46, 0x21, 0x06, 0x0E
};

// --- ISRs ---
static void key_isr(void *ctx, alt_u32 id) {
    key_event = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PIO_KEY_BASE);
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PIO_KEY_BASE, 0xF);
}

static void sw_isr(void *ctx, alt_u32 id) {
    sw_event = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PIO_SW_BASE);
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PIO_SW_BASE, 0x7);
}

static void timer_isr(void *ctx, alt_u32 id) {
    IOWR_ALTERA_AVALON_TIMER_STATUS(SYS_TIMER_BASE, 0);
    tick_ms++;
}

// --- Helpers HEX ---
static void hex_write_lo(uint32_t val) {
    uint32_t lo = ((uint32_t)seg7[(val >> 12) & 0xF] << 21) |
                  ((uint32_t)seg7[(val >>  8) & 0xF] << 14) |
                  ((uint32_t)seg7[(val >>  4) & 0xF] <<  7) |
                   (uint32_t)seg7[(val >>  0) & 0xF];
    IOWR_32DIRECT(PIO_HEX_LO_BASE, 0, lo);
}

int main(void) {
    printf("NIOS II init\n");

    // --- Registrar IRQs ---
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PIO_KEY_BASE, 0xF);
    IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PIO_KEY_BASE, 0xF);
    alt_irq_register(PIO_KEY_IRQ, NULL, key_isr);

    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PIO_SW_BASE, 0x7);
    IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PIO_SW_BASE, 0x7);
    alt_irq_register(4, NULL, sw_isr);

    IOWR_ALTERA_AVALON_TIMER_CONTROL(SYS_TIMER_BASE,
        ALTERA_AVALON_TIMER_CONTROL_ITO_MSK  |
        ALTERA_AVALON_TIMER_CONTROL_CONT_MSK |
        ALTERA_AVALON_TIMER_CONTROL_START_MSK);
    alt_irq_register(SYS_TIMER_IRQ, NULL, timer_isr);

    // --- Apagar todos los HEX al inicio (activo bajo: 0x7F = segmento off) ---
    IOWR_32DIRECT(PIO_HEX_LO_BASE, 0, 0x0FFFFFFF);
    IOWR_32DIRECT(PIO_HEX_HI_BASE, 0, 0x00003FFF);

    // --- Audio IP: esperar que el codec WM8731 termine de inicializar (~500ms) ---
    // audio_and_video_config_0 envia I2C al arrancar; hay que darle tiempo
    for (volatile uint32_t d = 0; d < 2500000; d++) {}

    IOWR_32DIRECT(AUDIO_0_BASE, AUDIO_CTRL, 0xC); // limpiar FIFOs
    IOWR_32DIRECT(AUDIO_0_BASE, AUDIO_CTRL, 0x0);

    // --- Shared memory ---
    volatile uint32_t *sh = (volatile uint32_t *)(SHARED_MEM_BASE);
    uint32_t tail = 0;

    printf("Listo. Esperando ARM...\n");

    uint32_t last_sec = 0;

    while (1) {

        // --- KEY ---
        if (key_event) {
            uint32_t k = key_event;
            key_event = 0;
            uint32_t n = (k & 1) ? 0 : (k & 2) ? 1 : (k & 4) ? 2 : 3;
            printf("[%u ms] KEY%u\n", (unsigned)tick_ms, (unsigned)n);
            uint32_t lo = ((uint32_t)0x41 << 7) | seg7[n];
            IOWR_32DIRECT(PIO_HEX_LO_BASE, 0, lo);
        }

        // --- SW ---
        if (sw_event) {
            uint32_t sw = IORD_ALTERA_AVALON_PIO_DATA(PIO_SW_BASE) & 0x7;
            sw_event = 0;
            printf("[%u ms] SW: 0x%x\n", (unsigned)tick_ms, (unsigned)sw);
        }

        // --- Timer tick ---
        uint32_t sec = tick_ms / 1000;
        if (sec != last_sec) {
            last_sec = sec;
            printf("[%u s]\n", (unsigned)sec);
        }

        // --- Audio polling ---
        uint32_t cmd = sh[IDX_CMD];

        if (cmd == 1) {
            uint32_t head = sh[IDX_HEAD];

            if (head != tail) {
                // esperar espacio en FIFO del DAC
                uint32_t fifo;
                uint32_t fifo_timeout = 0;
                do {
                    fifo = IORD_32DIRECT(AUDIO_0_BASE, AUDIO_FIFO);
                    if (++fifo_timeout >= 5000000) {
                        printf("FIFO stuck: 0x%08X\n", (unsigned)fifo);
                        fifo_timeout = 0;
                    }
                } while (((fifo >> 16) & 0xFF) == 0);

                // desempacar muestra: bits[31:16]=L bits[15:0]=R
                uint32_t packed = sh[BUF_WORD_START + tail];
                int32_t left  = (int32_t)(int16_t)(packed >> 16);
                int32_t right = (int32_t)(int16_t)(packed & 0xFFFF);

                IOWR_32DIRECT(AUDIO_0_BASE, AUDIO_LEFT,  left);
                IOWR_32DIRECT(AUDIO_0_BASE, AUDIO_RIGHT, right);

                tail = (tail + 1) % BUF_WORDS;
                sh[IDX_TAIL] = tail;
            }

        } else if (cmd == 2) {
            // STOP
            tail = 0;
            sh[IDX_TAIL] = 0;
            IOWR_32DIRECT(AUDIO_0_BASE, AUDIO_CTRL, 0xC); // clear FIFOs
            IOWR_32DIRECT(AUDIO_0_BASE, AUDIO_CTRL, 0x0);
            printf("STOP\n");
            while (sh[IDX_CMD] == 2) {} // esperar nuevo comando
        }
    }

    return 0;
}
