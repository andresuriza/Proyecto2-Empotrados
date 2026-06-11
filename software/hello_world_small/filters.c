#include "filters.h"

// ══════════════════════════════════════════════════════════
// 3 filtros FIR (promedio móvil) con SUMA CORRIENTE (running sum):
//   sum += x_nuevo - x_viejo;  ma = sum >> log2(N).   -> O(1) por muestra
// aunque N sea grande -> podemos usar muchos taps (corte FUERTE) sin encarecer.
// FIR puro = estable, sin realimentación -> NO se desborda/acumula como los IIR.
// Promedio móvil de N muestras a 48kHz: corte -3dB ~ fs/(2N).
//   N=8 -> ~3kHz   N=16 -> ~1.5kHz   N=32 -> ~750Hz   N=64 -> ~375Hz
//
// Si querés MÁS o MENOS efecto, cambiá solo los #define (N debe ser potencia de 2,
// y SH = log2(N)). Lowpass: N grande = más apagado. Highpass: N chico = más fino.
// ══════════════════════════════════════════════════════════

#define LP_N    8        // lowpass ~3kHz  (suave: deja medios/agudos -> menos "peta" el parlante)
#define LP_SH   3
#define HP_N    8        // highpass: quita por debajo de ~3kHz (mas fino/agudo)
#define HP_SH   3
#define BPH_N   4        // bandpass etapa corta: lowpass ~6kHz (mas brillo -> mas claro)
#define BPH_SH  2
#define BPL_N  64        // bandpass etapa larga:  lowpass ~375Hz
#define BPL_SH  6

// Avanza un promedio móvil de N muestras (N potencia de 2) y devuelve su valor.
static inline int32_t ma(int32_t *buf, int32_t *sum, int *idx, int n, int sh, int32_t x) {
    *sum      += x - buf[*idx];     // suma corriente: + nuevo - el más viejo
    buf[*idx]  = x;
    *idx       = (*idx + 1) & (n - 1);
    return *sum >> sh;              // / N
}

// ── FILTRO 1: Pasa-bajos FIR (MA 32 taps) ── apagado/grave. SW[1:0]=01
static int32_t lp_bl[LP_N], lp_br[LP_N];
static int32_t lp_sl, lp_sr;
static int     lp_il, lp_ir;
static void filter_lowpass(int32_t *left, int32_t *right) {
    *left  = ma(lp_bl, &lp_sl, &lp_il, LP_N, LP_SH, *left);
    *right = ma(lp_br, &lp_sr, &lp_ir, LP_N, LP_SH, *right);
}

// ── FILTRO 2: Pasa-altos FIR (x - MA16) ── brillante/fino. SW[1:0]=10
static int32_t hp_bl[HP_N], hp_br[HP_N];
static int32_t hp_sl, hp_sr;
static int     hp_il, hp_ir;
static void filter_highpass(int32_t *left, int32_t *right) {
    int32_t xl = *left, xr = *right;
    *left  = xl - ma(hp_bl, &hp_sl, &hp_il, HP_N, HP_SH, xl);
    *right = xr - ma(hp_br, &hp_sr, &hp_ir, HP_N, HP_SH, xr);
}

// ── FILTRO 3: Pasa-banda FIR (MA corto - MA largo) ── voz/teléfono. SW[1:0]=11
// EN MONO: se mezcla L+R y se filtra UNA sola vez (2 promedios en vez de 4) -> mitad de
// cuentas -> no tipa el NIOS por debajo de 48kHz (antes daba x0.5). Telefono = mono, igual.
static int32_t bph_b[BPH_N];  static int32_t bph_s;  static int bph_i;
static int32_t bpl_b[BPL_N];  static int32_t bpl_s;  static int bpl_i;
static void filter_bandpass(int32_t *left, int32_t *right) {
    int32_t m  = (*left + *right) >> 1;                 // mezcla a mono
    int32_t hi = ma(bph_b, &bph_s, &bph_i, BPH_N, BPH_SH, m);
    int32_t lo = ma(bpl_b, &bpl_s, &bpl_i, BPL_N, BPL_SH, m);
    int32_t bp = hi - lo;
    *left  = bp;
    *right = bp;
}

// ══════════════════════════════════════════════════════════
// FUNCIÓN PÚBLICA
// ══════════════════════════════════════════════════════════
void apply_filter(int32_t *left, int32_t *right, uint32_t mode) {
    switch (mode) {
        case FILTER_LOWPASS:  filter_lowpass(left, right);  break;
        case FILTER_HIGHPASS: filter_highpass(left, right); break;
        case FILTER_BANDPASS: filter_bandpass(left, right); break;
        default: break;  // BYPASS
    }
    // Saturacion de seguridad (highpass/bandpass = restas, pueden pasarse un poco).
    if      (*left  >  32767) *left  =  32767;
    else if (*left  < -32768) *left  = -32768;
    if      (*right >  32767) *right =  32767;
    else if (*right < -32768) *right = -32768;
}