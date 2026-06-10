#include "filters.h"

// ══════════════════════════════════════════════════════════
// Filtros IIR de UN POLO. Ventajas vs la version anterior:
//  - ACOTADOS: la salida nunca se aleja mucho del rango de entrada -> no overflow/ruido.
//  - BARATOS: 1 resta + 1 shift + 1 suma por muestra -> sin riesgo de underrun.
// Un polo pasa-bajos:  y += (x - y) >> k.   Corte fc ~= fs / (2*pi*2^k).  fs = 48kHz:
//   k=2 -> ~1.9kHz    k=3 -> ~950Hz    k=4 -> ~480Hz    k=5 -> ~240Hz
// (L y R con estado separado para estereo)
// ══════════════════════════════════════════════════════════

// ── FILTRO 1: Pasa-bajos (un polo, ~950Hz) ──
// Quita agudos -> sonido grave/cálido. SW[1:0]=01
static int32_t lp_l = 0, lp_r = 0;

static void filter_lowpass(int32_t *left, int32_t *right) {
    lp_l += (*left  - lp_l) >> 3;
    lp_r += (*right - lp_r) >> 3;
    *left  = lp_l;
    *right = lp_r;
}

// ── FILTRO 2: Pasa-altos (un polo, ~950Hz) ──
// highpass = x - lowpass(x) (lo que el pasa-bajos deja afuera = los agudos).
// Sonido brillante/fino. SW[1:0]=10
static int32_t hp_lp_l = 0, hp_lp_r = 0;

static void filter_highpass(int32_t *left, int32_t *right) {
    hp_lp_l += (*left  - hp_lp_l) >> 3;
    hp_lp_r += (*right - hp_lp_r) >> 3;
    *left  = *left  - hp_lp_l;
    *right = *right - hp_lp_r;
}

// ── FILTRO 3: Pasa-banda (~240Hz - 1.9kHz) ──
// lowpass rapido (corte alto ~1.9kHz) menos su lowpass lento (corte bajo ~240Hz)
// = queda la banda media. Efecto voz/telefono. SW[1:0]=11
static int32_t bp1_l = 0, bp1_r = 0;   // lowpass corte alto
static int32_t bp2_l = 0, bp2_r = 0;   // lowpass corte bajo

static void filter_bandpass(int32_t *left, int32_t *right) {
    bp1_l += (*left  - bp1_l) >> 2;     // ~1.9kHz
    bp1_r += (*right - bp1_r) >> 2;
    bp2_l += (bp1_l  - bp2_l) >> 5;     // ~240Hz
    bp2_r += (bp1_r  - bp2_r) >> 5;
    *left  = bp1_l - bp2_l;
    *right = bp1_r - bp2_r;
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
    // Saturacion de seguridad (highpass/bandpass pueden pasarse un poco en transientes).
    if      (*left  >  32767) *left  =  32767;
    else if (*left  < -32768) *left  = -32768;
    if      (*right >  32767) *right =  32767;
    else if (*right < -32768) *right = -32768;
}