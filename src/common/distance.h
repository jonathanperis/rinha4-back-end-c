#ifndef RINHA_DISTANCE_H
#define RINHA_DISTANCE_H

#include <stdint.h>

#ifdef __AVX2__
#include <immintrin.h>
#endif

#define RINHA_DIMS 14
#define RINHA_SCALE 10000

int16_t rinha_qround(double value);
int16_t rinha_qclamp01(double value);
uint64_t rinha_dist_i16(const int16_t a[RINHA_DIMS], const int16_t b[RINHA_DIMS]);

static inline uint64_t rinha_sqdiff_i16(int16_t a, int16_t b) {
    int64_t diff = (int64_t)a - (int64_t)b;
    return (uint64_t)(diff * diff);
}

#ifdef __AVX2__
static inline uint64_t rinha_dist_i16_first8_avx2(const int16_t *a, const int16_t *b) {
    __m128i va16 = _mm_loadu_si128((const __m128i *)a);
    __m128i vb16 = _mm_loadu_si128((const __m128i *)b);
    __m256i va = _mm256_cvtepi16_epi32(va16);
    __m256i vb = _mm256_cvtepi16_epi32(vb16);
    __m256i diff = _mm256_sub_epi32(va, vb);
    __m256i sq = _mm256_mullo_epi32(diff, diff);
    __m128i lo = _mm256_castsi256_si128(sq);
    __m128i hi = _mm256_extracti128_si256(sq, 1);
    __m128i sum = _mm_add_epi32(lo, hi);
    sum = _mm_add_epi32(sum, _mm_srli_si128(sum, 8));
    sum = _mm_add_epi32(sum, _mm_srli_si128(sum, 4));
    return (uint64_t)(uint32_t)_mm_cvtsi128_si32(sum);
}
#endif

/* Squared distance with early abort once the current top-5 cutoff is reached.
   The dimension order is deliberate: high-signal binary/category dimensions are
   checked before smoother numeric features, so bad candidates often stop before
   all 14 dimensions are touched. */
static inline uint64_t rinha_dist_i16_bounded(const int16_t a[RINHA_DIMS], const int16_t b[RINHA_DIMS], uint64_t limit) {
    uint64_t sum = 0;

    sum += rinha_sqdiff_i16(a[9], b[9]);
    if (sum >= limit) return sum;
    sum += rinha_sqdiff_i16(a[10], b[10]);
    if (sum >= limit) return sum;
    sum += rinha_sqdiff_i16(a[11], b[11]);
    if (sum >= limit) return sum;
#ifdef __AVX2__
    sum += rinha_dist_i16_first8_avx2(a, b);
    if (sum >= limit) return sum;
    sum += rinha_sqdiff_i16(a[8], b[8]);
    if (sum >= limit) return sum;
    sum += rinha_sqdiff_i16(a[12], b[12]);
    if (sum >= limit) return sum;
    sum += rinha_sqdiff_i16(a[13], b[13]);
    return sum;
#else
    sum += rinha_sqdiff_i16(a[5], b[5]);
    if (sum >= limit) return sum;
    sum += rinha_sqdiff_i16(a[6], b[6]);
    if (sum >= limit) return sum;
    sum += rinha_sqdiff_i16(a[0], b[0]);
    if (sum >= limit) return sum;
    sum += rinha_sqdiff_i16(a[1], b[1]);
    if (sum >= limit) return sum;
    sum += rinha_sqdiff_i16(a[2], b[2]);
    if (sum >= limit) return sum;
    sum += rinha_sqdiff_i16(a[7], b[7]);
    if (sum >= limit) return sum;
    sum += rinha_sqdiff_i16(a[8], b[8]);
    if (sum >= limit) return sum;
    sum += rinha_sqdiff_i16(a[12], b[12]);
    if (sum >= limit) return sum;
    sum += rinha_sqdiff_i16(a[13], b[13]);
    if (sum >= limit) return sum;
    sum += rinha_sqdiff_i16(a[3], b[3]);
    if (sum >= limit) return sum;
    sum += rinha_sqdiff_i16(a[4], b[4]);
    return sum;
#endif
}

#endif
