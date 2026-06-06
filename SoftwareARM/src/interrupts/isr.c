#include "interrupts/gic.h"
#include "platform.h"
//#include "player.h"          // para llamar play/pause/next/prev

// Declaraciones de handlers específicos
static void handle_button_irq(void);

// Dispatcher — determina qué interrupción ocurrió
void irq_dispatch(void) {
    // Leer qué interrupción llegó
    uint32_t irq_id = GICC_IAR & 0x3FF;  // bits 9:0

    // Llamar al handler correspondiente
    switch (irq_id) {
        case BTN_IRQ_ID:
            handle_button_irq();
            break;
        default:
            // Interrupción no manejada — ignorar
            break;
    }

    // Señalar fin de interrupción al GIC (siempre obligatorio)
    GICC_EOIR = irq_id;
}

// Handler de botones
static void handle_button_irq(void) {
    // Leer qué botón fue presionado
    volatile uint32_t* btn_reg       = (volatile uint32_t*) BTN_BASE;
    volatile uint32_t* btn_edge_reg  = (volatile uint32_t*)(BTN_BASE + 0x0C);

    uint32_t pressed = *btn_edge_reg;  // registro de edge capture

    if (pressed & 0x1) player_play_pause();   // KEY0
    if (pressed & 0x2) player_next_track();   // KEY1
    if (pressed & 0x4) player_prev_track();   // KEY2
    if (pressed & 0x8) player_stop();         // KEY3

    // Limpiar el edge capture register
    *btn_edge_reg = pressed;
}