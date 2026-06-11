#include "filters.h"

// ══════════════════════════════════════════════════════════
// 3 filtros FIR (promedio móvil) con SUMA CORRIENTE (running sum):
//   sum += x_nuevo - x_viejo;  out = sum >> SH   (/ 2^SH = shift, barato, SIN division).
// El indice envuelve con N (cualquier valor, no hace falta potencia de 2).
//   - Si 2^SH == N  -> promedio exacto.
//   - Si 2^SH >  N  -> promedio atenuado (ganancia N/2^SH < 1): util para que el lowpass
//                      no sature el parlante.
// FIR puro = estable, no se desborda. Cambiar los taps NO cambia el costo (O(1)) -> NO trae
// de vuelta el x0.5 (ese era el cómputo; el bandpass se resolvió pasandolo a MONO).
// Corte -3dB de un MA de N ~ 0.44*fs/N (fs=48kHz):  N=8->~2.6kHz  N=12->~1.8kHz  N=64->~330Hz
// ══════════════════════════════════════════════════════════

#define LP_N   12        // lowpass ~1.8kHz, atenuado x0.75 (12/16) -> mas apagado y NO petea
#define LP_SH   4
#define HP_N    8        // highpass: x - MA8 (~2.6kHz) -> fino/agudo
#define HP_SH   3
#define BPH_N   4        // bandpass corte alto ~5kHz (deja pasar mas voz/agudos -> menos "pared")
#define BPH_SH  2
#define BPL_N  64        // bandpass corte bajo ~330Hz
#define BPL_SH  6

// Avanza un promedio móvil de N muestras (cualquier N) y devuelve sum >> SH.
static inline int32_t ma(int32_t *buf, int32_t *sum, int *idx, int n, int sh, int32_t x) {
    *sum     += x - buf[*idx];      // suma corriente: + nuevo - el mas viejo
    buf[*idx] = x;
    if (++(*idx) >= n) *idx = 0;    // envuelve con cualquier N
    return *sum >> sh;              // / 2^SH (shift)
}

// ── FILTRO 1: Pasa-bajos FIR ── apagado/grave. SW[1:0]=01
static int32_t lp_bl[LP_N], lp_br[LP_N];
static int32_t lp_sl, lp_sr;  static int lp_il, lp_ir;
static void filter_lowpass(int32_t *left, int32_t *right) {
    *left  = ma(lp_bl, &lp_sl, &lp_il, LP_N, LP_SH, *left);
    *right = ma(lp_br, &lp_sr, &lp_ir, LP_N, LP_SH, *right);
}

// ── FILTRO 2: Pasa-altos FIR (x - MA8) ── brillante/fino. SW[1:0]=10
static int32_t hp_bl[HP_N], hp_br[HP_N];
static int32_t hp_sl, hp_sr;  static int hp_il, hp_ir;
static void filter_highpass(int32_t *left, int32_t *right) {
    int32_t xl = *left, xr = *right;
    *left  = xl - ma(hp_bl, &hp_sl, &hp_il, HP_N, HP_SH, xl);
    *right = xr - ma(hp_br, &hp_sr, &hp_ir, HP_N, HP_SH, xr);
}

// ── FILTRO 3: Pasa-banda FIR (MA4 - MA64) EN MONO ── voz/teléfono. SW[1:0]=11
// Mono: mezcla L+R y filtra una vez (2 promedios en vez de 4) -> no tipa el NIOS (x0.5).
static int32_t bph_b[BPH_N];  static int32_t bph_s;  static int bph_i;
static int32_t bpl_b[BPL_N];  static int32_t bpl_s;  static int bpl_i;
static void filter_bandpass(int32_t *left, int32_t *right) {
    int32_t m  = (*left + *right) >> 1;                 // mezcla a mono
    int32_t hi = ma(bph_b, &bph_s, &bph_i, BPH_N, BPH_SH, m);
    int32_t lo = ma(bpl_b, &bpl_s, &bpl_i, BPL_N, BPL_SH, m);
    int32_t bp = hi - lo;
    bp -= bp >> 2;          // x0.75: baja los picos para que no pete (conserva el brillo)
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