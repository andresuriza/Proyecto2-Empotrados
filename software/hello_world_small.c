#include <stdio.h>
#include <stdint.h>
#include "system.h"
#include "io.h"
#include "sys/alt_irq.h"
#include "altera_avalon_pio_regs.h"
#include "altera_avalon_timer_regs.h"

static volatile uint32_t key_event = 0;
static volatile uint32_t tick_ms   = 0;

static void key_isr(void *ctx, alt_u32 id) {
    key_event = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PIO_KEY_BASE);
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PIO_KEY_BASE, 0xF);
}

static void timer_isr(void *ctx, alt_u32 id) {
    IOWR_ALTERA_AVALON_TIMER_STATUS(SYS_TIMER_BASE, 0);
    tick_ms++;
}

int main() {
    printf("NIOS II IRQ test\n");

    // KEY: interrupciones por flanco en los 4 botones
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PIO_KEY_BASE, 0xF);
    IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PIO_KEY_BASE, 0xF);
    alt_irq_register(PIO_KEY_IRQ, NULL, key_isr);

    // Timer 1ms: continuo con interrupcion por timeout
    IOWR_ALTERA_AVALON_TIMER_CONTROL(SYS_TIMER_BASE,
        ALTERA_AVALON_TIMER_CONTROL_ITO_MSK  |
        ALTERA_AVALON_TIMER_CONTROL_CONT_MSK |
        ALTERA_AVALON_TIMER_CONTROL_START_MSK);
    alt_irq_register(SYS_TIMER_IRQ, NULL, timer_isr);

    printf("IRQs listos. Presiona KEY0-KEY3...\n");

    uint32_t last_sec = 0;

    while (1) {
        if (key_event) {
            printf("[%u ms] KEY: 0x%x\n", (unsigned)tick_ms, (unsigned)key_event);
            key_event = 0;
        }

        uint32_t sec = tick_ms / 1000;
        if (sec != last_sec) {
            printf("[%u s] timer OK\n", (unsigned)sec);
            last_sec = sec;
        }
    }

    return 0;
}
