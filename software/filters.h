#ifndef FILTERS_H
#define FILTERS_H

#include <stdint.h>

// Modos de filtro — seleccionados por SW[1:0]
#define FILTER_BYPASS   0
#define FILTER_LOWPASS  1
#define FILTER_HIGHPASS 2
#define FILTER_BANDPASS 3

void apply_filter(int32_t *left, int32_t *right, uint32_t mode);

#endif