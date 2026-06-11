#include <stdio.h>
#include <stdint.h>
#include "system.h"
#include "io.h"
#include "sys/alt_irq.h"
#include "altera_avalon_pio_regs.h"
#include "altera_avalon_timer_regs.h"
#include "filters.h"            // filtros de audio (seleccionados por SW[1:0])

// --- Mailbox (debe coincidir con hw_test.c) ---
#define IDX_CMD         0
#define IDX_HEAD        1
#define IDX_TAIL        2
#define IDX_REQ         3   // NIOS -> ARM: peticion de control (0=nada,1=next,2=prev,3=stop)
#define BUF_WORD_START  64
#define BUF_WORDS       16320

// Peticiones NIOS -> ARM (las ejecuta el ARM sobre su playlist)
#define REQ_NONE        0
#define REQ_NEXT        1
#define REQ_PREV        2
#define REQ_STOP        3   // KEY3: rebobinar la misma cancion y quedar detenido

// --- Audio IP offsets (desde AUDIO_0_BASE) ---
#define AUDIO_CTRL      0   // byte offset 0
#define AUDIO_FIFO      4   // byte offset 4: [23:16]=left space [31:24]=right space
#define AUDIO_LEFT      8   // byte offset 8: DAC left
#define AUDIO_RIGHT     12  // byte offset 12: DAC right

// --- Estado compartido ---
static volatile uint32_t key_event   = 0;
static volatile uint32_t tick_ms     = 0;
static volatile uint32_t sw_event    = 0;
static          uint32_t filter_mode = FILTER_BYPASS;   // 0=bypass 1=low 2=high 3=band (SW[1:0])

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

// --- Mostrar MM:SS en HEX3..HEX0 (escritura directa, NO bloquea como printf) ---
// Layout pio_hex_lo: [27:21]=HEX3 [20:14]=HEX2 [13:7]=HEX1 [6:0]=HEX0
static void hex_show_time(uint32_t total_sec) {
    uint32_t mm = (total_sec / 60) % 100;   // tope 99 min
    uint32_t ss = total_sec % 60;
    uint32_t d3 = (mm / 10) % 10;           // decenas de minuto
    uint32_t d2 =  mm % 10;                 // unidades de minuto
    uint32_t d1 =  ss / 10;                 // decenas de segundo
    uint32_t d0 =  ss % 10;                 // unidades de segundo
    uint32_t lo = ((uint32_t)seg7[d3] << 21) |
                  ((uint32_t)seg7[d2] << 14) |
                  ((uint32_t)seg7[d1] <<  7) |
                   (uint32_t)seg7[d0];
    IOWR_32DIRECT(PIO_HEX_LO_BASE, 0, lo);
}

// --- Leer SW[1:0] -> filter_mode y mostrar el filtro activo en HEX5 ---
// HEX5: 0=bypass 1=lowpass 2=highpass 3=bandpass (HEX4 apagado). El diseño NO tiene
// LEDs (sin PIO ni cableado en el top), por eso el indicador va en HEX5.
static void set_filter(void) {
    filter_mode = IORD_ALTERA_AVALON_PIO_DATA(PIO_SW_BASE) & 0x3;
    IOWR_32DIRECT(PIO_HEX_HI_BASE, 0, ((uint32_t)seg7[filter_mode] << 7) | 0x7F);
}

int main(void) {
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

    // --- HEX en 00:00 al inicio + filtro inicial en HEX5 ---
    hex_show_time(0);
    set_filter();   // lee SW[1:0] y muestra el filtro activo en HEX5

    // --- Audio IP: esperar que el codec WM8731 termine de auto-inicializar (~500ms) ---
    for (volatile uint32_t d = 0; d < 2500000; d++) {}

    IOWR_32DIRECT(AUDIO_0_BASE, AUDIO_CTRL, 0xC); // limpiar FIFOs
    IOWR_32DIRECT(AUDIO_0_BASE, AUDIO_CTRL, 0x0);

    // --- Shared memory ---
    volatile uint32_t *sh = (volatile uint32_t *)(SHARED_MEM_BASE);
    uint32_t tail = 0;

    // --- Estado de reproduccion (para el reloj MM:SS) ---
    uint32_t playing       = 0;
    uint32_t start_ms      = 0;
    uint32_t last_disp     = 0xFFFFFFFF;
    uint32_t paused        = 0;
    uint32_t pause_started = 0;
    uint32_t stopped       = 0;   // KEY3: detenido en el inicio de la cancion (espera play)

    while (1) {

        // --- KEY: control de reproduccion (sin printf para no bloquear el audio) ---
        if (key_event) {
            uint32_t k = key_event;
            key_event = 0;
            if (k & 0x1) {                       // KEY0 = play/pausa
                if (stopped) {                   // estaba detenido -> play desde el inicio
                    stopped   = 0;
                    paused    = 0;
                    start_ms  = tick_ms;         // MM:SS arranca de 00:00
                    last_disp = 0xFFFFFFFF;
                } else {
                    paused = !paused;
                    if (paused) pause_started = tick_ms;
                    else        start_ms += (tick_ms - pause_started); // no contar la pausa en MM:SS
                }
            }
            if (k & 0x2) { sh[IDX_REQ] = REQ_PREV; stopped = 0; } // KEY1 = anterior  (lo ejecuta el ARM)
            if (k & 0x4) { sh[IDX_REQ] = REQ_NEXT; stopped = 0; } // KEY2 = siguiente (lo ejecuta el ARM)
            if (k & 0x8) {                       // KEY3 = STOP: rebobina la misma cancion, detenido
                stopped = 1;
                paused  = 0;
                sh[IDX_REQ] = REQ_STOP;          // el ARM rebobina la cancion al inicio
                hex_show_time(0);                // MM:SS -> 00:00
            }
        }
        // --- SW: cambio de filtro (actualiza filter_mode + indicador en HEX5) ---
        if (sw_event) { sw_event = 0; set_filter(); }

        // --- Audio polling ---
        uint32_t cmd = sh[IDX_CMD];

        if (cmd == 1) {
            // arranque de cancion: fijar referencia de tiempo
            if (!playing) {
                playing   = 1;
                paused    = 0;
                start_ms  = tick_ms;
                last_disp = 0xFFFFFFFF;
            }

            // actualizar HEX MM:SS una vez por segundo (congelado en pausa; 00:00 en stop)
            if (!paused && !stopped) {
                uint32_t el = (tick_ms - start_ms) / 1000;
                if (el != last_disp) {
                    last_disp = el;
                    hex_show_time(el);
                }
            }

            uint32_t head = sh[IDX_HEAD];
            if (!paused && !stopped) {   // pausa/stop: no drena -> buffer se llena -> ARM se frena
                // Drenar TODAS las muestras disponibles de un saque mientras haya espacio en el
                // FIFO. Antes se escribia 1 muestra por vuelta del while(1), con /1000 y %BUF_WORDS
                // por muestra -> el loop no sostenia 48kHz -> el codec (master) se quedaba sin datos
                // y sonaba a media velocidad (x0.5). En lote y sin divisiones por muestra, si alcanza.
                // Optimizacion: leer el ESPACIO del FIFO UNA vez y escribir esas N muestras sin
                // re-chequear -> menos lecturas Avalon por muestra -> sobra tiempo para los filtros.
                // (El bandpass hace 2 promedios y antes tipaba el loop por debajo de 48kHz -> x0.5.)
                while (head != tail) {
                    uint32_t fifo  = IORD_32DIRECT(AUDIO_0_BASE, AUDIO_FIFO);
                    uint32_t sl    = (fifo >> 16) & 0xFF;   // espacio canal izq
                    uint32_t sr    = (fifo >> 24) & 0xFF;   // espacio canal der
                    uint32_t space = (sl < sr) ? sl : sr;
                    if (space == 0) break;                  // FIFO lleno

                    while (space-- > 0 && head != tail) {
                        // desempacar muestra: bits[31:16]=L bits[15:0]=R
                        uint32_t packed = sh[BUF_WORD_START + tail];
                        int32_t left  = (int32_t)(int16_t)(packed >> 16);
                        int32_t right = (int32_t)(int16_t)(packed & 0xFFFF);

                        apply_filter(&left, &right, filter_mode);   // filtro segun SW[1:0]

                        IOWR_32DIRECT(AUDIO_0_BASE, AUDIO_LEFT,  left);
                        IOWR_32DIRECT(AUDIO_0_BASE, AUDIO_RIGHT, right);

                        if (++tail >= BUF_WORDS) tail = 0;   // sin modulo
                    }
                }
                sh[IDX_TAIL] = tail;   // publicar el tail una vez por lote
            }

        } else if (cmd == 2) {
            // STOP
            playing = 0;
            paused  = 0;
            tail = 0;
            sh[IDX_TAIL] = 0;
            sh[IDX_CMD]  = 0;   // ACK: volver a idle
            IOWR_32DIRECT(AUDIO_0_BASE, AUDIO_CTRL, 0xC); // clear FIFOs
            IOWR_32DIRECT(AUDIO_0_BASE, AUDIO_CTRL, 0x0);
            // el HEX queda mostrando el tiempo final de la cancion
        }
    }

    return 0;
}