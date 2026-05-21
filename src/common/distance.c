#include "common/distance.h"

#include <limits.h>

static int16_t clamp_i16(long value) {
    if (value < SHRT_MIN) return SHRT_MIN;
    if (value > SHRT_MAX) return SHRT_MAX;
    return (int16_t)value;
}

int16_t rinha_qround(double value) {
    if (value < -1.0) value = -1.0;
    if (value > 1.0) value = 1.0;
    double scaled = value * (double)RINHA_SCALE;
    long rounded = (long)(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
    return clamp_i16(rounded);
}

int16_t rinha_qclamp01(double value) {
    if (value < 0.0) value = 0.0;
    if (value > 1.0) value = 1.0;
    double scaled = value * (double)RINHA_SCALE;
    long rounded = (long)(scaled + 0.5);
    return clamp_i16(rounded);
}

uint64_t rinha_dist_i16(const int16_t a[RINHA_DIMS], const int16_t b[RINHA_DIMS]) {
#ifdef __AVX2__
    uint64_t sum = rinha_dist_i16_first8_avx2(a, b);
    for (int i = 8; i < RINHA_DIMS; ++i) {
        sum += rinha_sqdiff_i16(a[i], b[i]);
    }
    return sum;
#else
    uint64_t sum = 0;
    for (int i = 0; i < RINHA_DIMS; ++i) {
        sum += rinha_sqdiff_i16(a[i], b[i]);
    }
    return sum;
#endif
}
