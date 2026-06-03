#include <stdio.h>
#include <stdint.h>
#include "system.h"
#include "io.h"
#include "sys/alt_irq.h"
#include "altera_avalon_pio_regs.h"
#include "altera_avalon_timer_regs.h"

static volatile uint32_t key_event = 0;
static volatile uint32_t tick_ms   = 0;

static volatile uint32_t sw_event = 0;

static void sw_isr(void *ctx, alt_u32 id) {
    sw_event = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PIO_SW_BASE);
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PIO_SW_BASE, 0x7);
}

static const uint8_t seg7[16] = {
    0x40, 0x79, 0x24, 0x30, 0x19,
    0x12, 0x02, 0x78, 0x00, 0x10,
    0x08, 0x03, 0x46, 0x21, 0x06, 0x0E
};

void show_hex(uint32_t val) {
    uint32_t lo = ((uint32_t)seg7[(val >> 12) & 0xF] << 21) |
                  ((uint32_t)seg7[(val >>  8) & 0xF] << 14) |
                  ((uint32_t)seg7[(val >>  4) & 0xF] <<  7) |
                   (uint32_t)seg7[(val >>  0) & 0xF];

    uint32_t hi = ((uint32_t)seg7[(val >> 20) & 0xF] <<  7) |
                   (uint32_t)seg7[(val >> 16) & 0xF];

    IOWR_32DIRECT(PIO_HEX_LO_BASE, 0, lo);
    IOWR_32DIRECT(PIO_HEX_HI_BASE, 0, hi);
}

static void key_isr(void *ctx, alt_u32 id) {
    key_event = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PIO_KEY_BASE);
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PIO_KEY_BASE, 0xF);
}

static void timer_isr(void *ctx, alt_u32 id) {
    IOWR_ALTERA_AVALON_TIMER_STATUS(SYS_TIMER_BASE, 0);
    tick_ms++;
}

int main() {
    printf("NIOS II button/switch test\n");

    // KEY interrupts
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PIO_KEY_BASE, 0xF);
    IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PIO_KEY_BASE, 0xF);
    // Switches: interrupt on any edge
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PIO_SW_BASE, 0x7);
    IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PIO_SW_BASE, 0x7);
    alt_irq_register(4, NULL, sw_isr);
    alt_irq_register(PIO_KEY_IRQ, NULL, key_isr);

    // Timer 1ms
    IOWR_ALTERA_AVALON_TIMER_CONTROL(SYS_TIMER_BASE,
        ALTERA_AVALON_TIMER_CONTROL_ITO_MSK  |
        ALTERA_AVALON_TIMER_CONTROL_CONT_MSK |
        ALTERA_AVALON_TIMER_CONTROL_START_MSK);
    alt_irq_register(SYS_TIMER_IRQ, NULL, timer_isr);

    // Mailbox
    volatile uint32_t *mailbox = (volatile uint32_t *)(SHARED_MEM_BASE);

    printf("Press KEY0-KEY3 or toggle switches...\n");

    uint32_t last_sw = 0xFF; // force first display update

    while (1) {
        // KEY: show which button was pressed on HEX0
        if (key_event) {
            uint32_t k = key_event;
            key_event = 0;
            printf("[%u ms] KEY: 0x%x\n", (unsigned)tick_ms, (unsigned)k);
            // find lowest pressed key (bit 0 = KEY0, etc.)
            uint32_t key_num = 0;
            if      (k & 0x1) key_num = 0;
            else if (k & 0x2) key_num = 1;
            else if (k & 0x4) key_num = 2;
            else if (k & 0x8) key_num = 3;
            // show "K0" / "K1" / "K2" / "K3" on HEX1-HEX0
            // 'K' = 0x0E (same as F inverted... close enough), use A-F range
            uint32_t lo = ((uint32_t)0x41 << 7) |   // 'k' approximation on HEX1
                           (uint32_t)seg7[key_num];  // digit on HEX0
            IOWR_32DIRECT(PIO_HEX_LO_BASE, 0, lo);
        }

        if (sw_event) {
            uint32_t sw = IORD_ALTERA_AVALON_PIO_DATA(PIO_SW_BASE) & 0x7;
            sw_event = 0;
            printf("[%u ms] SW: 0x%x\n", (unsigned)tick_ms, (unsigned)sw);
            uint32_t lo = IORD_32DIRECT(PIO_HEX_LO_BASE, 0);
            lo &= 0x3FFF;
            lo |= ((uint32_t)seg7[sw & 0xF] << 14);
            IOWR_32DIRECT(PIO_HEX_LO_BASE, 0, lo);
        }

        // Mailbox from HPS still works
        uint32_t cmd = mailbox[0];
        if (cmd != 0) {
            show_hex(cmd);
            mailbox[1] = 0xACE;
            mailbox[0] = 0;
        }
    }

    return 0;
}
