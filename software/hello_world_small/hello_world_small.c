#include <stdio.h>
#include <stdint.h>

#include "system.h"
#include "io.h"
#include "sys/alt_irq.h"
#include "altera_avalon_pio_regs.h"
#include "altera_avalon_timer_regs.h"
#include "filters.h"

#define IDX_CMD         0
#define IDX_HEAD        1
#define IDX_TAIL        2
#define IDX_REQ         3
#define IDX_STATE       4

#define BUF_WORD_START  64
#define BUF_WORDS       16320

#define REQ_NONE        0
#define REQ_NEXT        1
#define REQ_PREV        2
#define REQ_STOP        3

#define ST_STOPPED      0
#define ST_PLAYING      1
#define ST_PAUSED       2

#define AUDIO_CTRL      0
#define AUDIO_FIFO      4
#define AUDIO_LEFT      8
#define AUDIO_RIGHT     12

static volatile uint32_t key_event   = 0;
static volatile uint32_t tick_ms     = 0;
static volatile uint32_t sw_event    = 0;
static uint32_t filter_mode          = FILTER_BYPASS;

// Tabla de conversión para displays de siete segmentos.
static const uint8_t seg7[16] = {
    0x40, 0x79, 0x24, 0x30, 0x19,
    0x12, 0x02, 0x78, 0x00, 0x10,
    0x08, 0x03, 0x46, 0x21, 0x06, 0x0E
};

// Rutina de servicio para interrupciones de botones.
static void key_isr(void *ctx, alt_u32 id)
{
    key_event = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PIO_KEY_BASE);
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PIO_KEY_BASE, 0xF);
}

// Rutina de servicio para interrupciones de switches.
static void sw_isr(void *ctx, alt_u32 id)
{
    sw_event = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PIO_SW_BASE);
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PIO_SW_BASE, 0x7);
}

// Rutina de servicio del temporizador del sistema.
static void timer_isr(void *ctx, alt_u32 id)
{
    IOWR_ALTERA_AVALON_TIMER_STATUS(SYS_TIMER_BASE, 0);
    tick_ms++;
}

// Muestra un tiempo en formato MM:SS sobre HEX3..HEX0.
static void hex_show_time(uint32_t total_sec)
{
    uint32_t mm = (total_sec / 60) % 100;
    uint32_t ss = total_sec % 60;

    uint32_t d3 = (mm / 10) % 10;
    uint32_t d2 = mm % 10;
    uint32_t d1 = ss / 10;
    uint32_t d0 = ss % 10;

    uint32_t lo =
        ((uint32_t)seg7[d3] << 21) |
        ((uint32_t)seg7[d2] << 14) |
        ((uint32_t)seg7[d1] << 7)  |
        ((uint32_t)seg7[d0]);

    IOWR_32DIRECT(PIO_HEX_LO_BASE, 0, lo);
}

// Actualiza el filtro de audio activo.
static void set_filter(void)
{
    filter_mode = IORD_ALTERA_AVALON_PIO_DATA(PIO_SW_BASE) & 0x3;

    IOWR_32DIRECT(
        PIO_HEX_HI_BASE,
        0,
        ((uint32_t)seg7[filter_mode] << 7) | 0x7F
    );
}

// Punto de entrada principal del sistema.
int main(void)
{
    // Configurar interrupciones de botones
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PIO_KEY_BASE, 0xF);
    IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PIO_KEY_BASE, 0xF);
    alt_irq_register(PIO_KEY_IRQ, NULL, key_isr);

    // Configurar interrupciones de switches
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PIO_SW_BASE, 0x7);
    IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PIO_SW_BASE, 0x7);
    alt_irq_register(4, NULL, sw_isr);

    // Configurar temporizador periódico del sistema
    IOWR_ALTERA_AVALON_TIMER_CONTROL(
        SYS_TIMER_BASE,
        ALTERA_AVALON_TIMER_CONTROL_ITO_MSK  |
        ALTERA_AVALON_TIMER_CONTROL_CONT_MSK |
        ALTERA_AVALON_TIMER_CONTROL_START_MSK
    );

    alt_irq_register(SYS_TIMER_IRQ, NULL, timer_isr);

    // Inicializar displays y mostrar el filtro activo
    hex_show_time(0);
    set_filter();

    // Esperar la inicialización automática del códec WM8731 antes de
    //  comenzar la reproducción de audio.
    for (volatile uint32_t d = 0; d < 2500000; d++) {}

    // Limpiar los FIFOs del periférico de audio
    IOWR_32DIRECT(AUDIO_0_BASE, AUDIO_CTRL, 0xC);
    IOWR_32DIRECT(AUDIO_0_BASE, AUDIO_CTRL, 0x0);

    // Obtener acceso a la región de memoria compartida
    volatile uint32_t *sh = (volatile uint32_t *)(SHARED_MEM_BASE);

    uint32_t tail = 0;

    // Variables de control de reproducción
    uint32_t playing       = 0;
    uint32_t start_ms      = 0;
    uint32_t last_disp     = 0xFFFFFFFF;
    uint32_t last_state    = 0xFFFFFFFF;
    uint32_t paused        = 0;
    uint32_t pause_started = 0;
    uint32_t stopped       = 0;

    while (1)
    {
        // Procesar eventos generados por los botones.
        if (key_event)
        {
            uint32_t k = key_event;
            key_event = 0;

            if (k & 0x1)
            {
                if (stopped)
                {
                    stopped   = 0;
                    paused    = 0;
                    start_ms  = tick_ms;
                    last_disp = 0xFFFFFFFF;
                }
                else
                {
                    paused = !paused;

                    if (paused)
                    {
                        pause_started = tick_ms;
                    }
                    else
                    {
                        
                        // Ajustar la referencia temporal para excluir el tiempo transcurrido durante la pausa.
                        start_ms += (tick_ms - pause_started);
                    }
                }
            }

            if (k & 0x2)
            {
                sh[IDX_REQ] = REQ_PREV;
                stopped = 0;
            }

            if (k & 0x4)
            {
                sh[IDX_REQ] = REQ_NEXT;
                stopped = 0;
            }

            if (k & 0x8)
            {
                stopped = 1;
                paused = 0;

                // Solicitar al ARM reiniciar la canción actual
                sh[IDX_REQ] = REQ_STOP;

                hex_show_time(0);
            }
        }

        // Actualizar el filtro si ocurrió un cambio en los switches
        if (sw_event)
        {
            sw_event = 0;
            set_filter();
        }

        uint32_t cmd = sh[IDX_CMD];

        if (cmd == 1)
        {
            // Detectar el inicio de una nueva reproducción y fijar la referencia temporal del cronómetro.
            if (!playing)
            {
                playing = 1;
                paused = 0;
                start_ms = tick_ms;
                last_disp = 0xFFFFFFFF;
            }

            // Actualizar la visualización MM:SS únicamente cuando cambia el segundo mostrado
            if (!paused && !stopped)
            {
                uint32_t elapsed = (tick_ms - start_ms) / 1000;

                if (elapsed != last_disp)
                {
                    last_disp = elapsed;
                    hex_show_time(elapsed);
                }
            }

            uint32_t head = sh[IDX_HEAD];

            // Transferir muestras desde el buffer compartido hacia el periférico de audio mientras 
            // exista espacio libre en ambos canales del FIFO.
            if (!paused && !stopped)
            {
                while (head != tail)
                {
                    uint32_t fifo = IORD_32DIRECT(
                        AUDIO_0_BASE,
                        AUDIO_FIFO
                    );

                    uint32_t left_space  = (fifo >> 16) & 0xFF;
                    uint32_t right_space = (fifo >> 24) & 0xFF;

                    uint32_t space =
                        (left_space < right_space)
                            ? left_space
                            : right_space;

                    if (space == 0)
                    {
                        break;
                    }

                    while (space-- > 0 && head != tail)
                    {
                        // Extraer muestras estéreo almacenadas como izquierdo y derecho
                        uint32_t packed =
                            sh[BUF_WORD_START + tail];

                        int32_t left =
                            (int32_t)(int16_t)(packed >> 16);

                        int32_t right =
                            (int32_t)(int16_t)(packed & 0xFFFF);

                        // Aplicar el filtro seleccionado
                        apply_filter(
                            &left,
                            &right,
                            filter_mode
                        );

                        // Enviar muestras al DAC
                        IOWR_32DIRECT(
                            AUDIO_0_BASE,
                            AUDIO_LEFT,
                            left
                        );

                        IOWR_32DIRECT(
                            AUDIO_0_BASE,
                            AUDIO_RIGHT,
                            right
                        );

                        // Avanzar el índice del buffer circular
                        if (++tail >= BUF_WORDS)
                        {
                            tail = 0;
                        }
                    }
                }

                // Publicar el nuevo valor de tail para que el ARM conozca cuánto audio fue consumido.
                sh[IDX_TAIL] = tail;
            }
        }
        else if (cmd == 2)
        {
            // Detener la reproducción y reinicializar el estado del periférico de audio.
            playing = 0;
            paused = 0;

            tail = 0;

            sh[IDX_TAIL] = 0;
            sh[IDX_CMD] = 0;

            IOWR_32DIRECT(AUDIO_0_BASE, AUDIO_CTRL, 0xC);
            IOWR_32DIRECT(AUDIO_0_BASE, AUDIO_CTRL, 0x0);
        }

        // Publicar el estado actual para que el ARM actualice la interfaz gráfica mostrada por VGA.
        uint32_t st =
            stopped
                ? ST_STOPPED
                : (paused
                    ? ST_PAUSED
                    : (playing
                        ? ST_PLAYING
                        : ST_STOPPED));

        if (st != last_state)
        {
            sh[IDX_STATE] = st;
            last_state = st;
        }
    }

    return 0;
}