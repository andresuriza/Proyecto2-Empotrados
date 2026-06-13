#include "filters.h"

#define LP_N   12   
#define LP_SH   4
#define HP_N    8    
#define HP_SH   3
#define BPH_N   4  
#define BPH_SH  2
#define BPL_N  64 
#define BPL_SH  6

#define DUB_LP_N   4    
#define DUB_LP_SH  2   
#define ECHO_D  2048 

// Actualiza un filtro de promedio móvil utilizando suma acumulada.
static inline int32_t ma(int32_t *buf, int32_t *sum, int *idx, int n, int sh, int32_t x) {
    *sum     += x - buf[*idx];    
    buf[*idx] = x;
    if (++(*idx) >= n) *idx = 0;    
    return *sum >> sh;
}

static int32_t lp_bl[LP_N], lp_br[LP_N];
static int32_t lp_sl, lp_sr;  static int lp_il, lp_ir;

// Aplica un filtro FIR pasa-bajos a los canales estéreo.
static void filter_lowpass(int32_t *left, int32_t *right) {
    *left  = ma(lp_bl, &lp_sl, &lp_il, LP_N, LP_SH, *left);
    *right = ma(lp_br, &lp_sr, &lp_ir, LP_N, LP_SH, *right);
}

static int32_t hp_bl[HP_N], hp_br[HP_N];
static int32_t hp_sl, hp_sr;  static int hp_il, hp_ir;

// Aplica un filtro FIR pasa-altos a los canales estéreo.
static void filter_highpass(int32_t *left, int32_t *right) {
    int32_t xl = *left;
    int32_t xr = *right;

    *left  = xl - ma(hp_bl, &hp_sl, &hp_il, HP_N, HP_SH, xl);
    *right = xr - ma(hp_br, &hp_sr, &hp_ir, HP_N, HP_SH, xr);
}

static int32_t bph_b[BPH_N];  static int32_t bph_s;  static int bph_i;
static int32_t bpl_b[BPL_N];  static int32_t bpl_s;  static int bpl_i;

// Aplica un filtro FIR pasa-banda en modo mono.
static void filter_bandpass(int32_t *left, int32_t *right) {
    int32_t m  = (*left + *right) >> 1;  
    int32_t hi = ma(bph_b, &bph_s, &bph_i, BPH_N, BPH_SH, m);
    int32_t lo = ma(bpl_b, &bpl_s, &bpl_i, BPL_N, BPL_SH, m);
    int32_t bp = hi - lo;
    bp -= bp >> 2;    
    *left  = bp;
    *right = bp;
}

static int32_t dub_b[DUB_LP_N]; static int32_t dub_s; static int dub_i;
static int32_t echo_buf[ECHO_D]; static int echo_i;
static void filter_eq(int32_t *left, int32_t *right) {
    int32_t m  = (*left + *right) >> 1;          
    int32_t lp = ma(dub_b, &dub_s, &dub_i, DUB_LP_N, DUB_LP_SH, m); 
    int32_t d  = echo_buf[echo_i];                
    int32_t out = (lp >> 1) + (d >> 1);  
    echo_buf[echo_i] = out;               
    if (++echo_i >= ECHO_D) echo_i = 0;
    *left  = out;
    *right = out;
}

// Aplica el filtro seleccionado a una muestra de audio estéreo.
void apply_filter(int32_t *left, int32_t *right, uint32_t mode) {
    switch (mode) {
        case FILTER_LOWPASS:  filter_highpass(left, right); break; 
        case FILTER_HIGHPASS: filter_bandpass(left, right); break; 
        case FILTER_BANDPASS: filter_eq(left, right);       break; 
        default: break;  
    }

    if      (*left  >  32767) *left  =  32767;
    else if (*left  < -32768) *left  = -32768;
    if      (*right >  32767) *right =  32767;
    else if (*right < -32768) *right = -32768;
}