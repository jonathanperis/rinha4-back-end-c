#include "common/search.h"

#include "common/index_format.h"
#include "common/topk.h"

#include <stdlib.h>

#ifdef RINHA_SEARCH_STATS
#include <stdio.h>
#include <string.h>
#endif

#define RINHA_MAX_NPROBE 256U
/* One extra probe stores the next unscanned list lower bound. search_certified()
   uses that sentinel to prove no remaining list can enter the current top-5. */
#define RINHA_MAX_PROBES (RINHA_MAX_NPROBE + 1U)

typedef struct {
    uint64_t dist; /* Primary ordering key and certification lower bound. */
    uint64_t tie;  /* Secondary ordering key, usually centroid distance. */
    uint32_t list;
} probe_t;

static uint64_t early_distance_limit(void) {
    static int initialized = 0;
    static uint64_t limit = 0;
    if (!initialized) {
        initialized = 1;
        const char *value = getenv("INDEX_EARLY_DISTANCE_MILLI");
        if (value != NULL && value[0] != '\0') {
            char *end = NULL;
            unsigned long parsed = strtoul(value, &end, 10);
            if (end != value && parsed > 0UL && parsed <= 2000UL) {
                uint64_t scaled = (uint64_t)RINHA_SCALE * (uint64_t)parsed / 1000ULL;
                limit = scaled * scaled;
            }
        }
    }
    return limit;
}

static int early_done(const rinha_top5_t *top) {
    uint64_t limit = early_distance_limit();
    return limit != 0 && rinha_top5_worst_dist(top) <= limit;
}

#ifdef RINHA_SEARCH_STATS
typedef enum {
    RINHA_TRACE_FAST = 0,
    RINHA_TRACE_REPAIR = 1,
    RINHA_TRACE_EXACT = 2,
    RINHA_TRACE_PHASES = 3
} rinha_trace_phase_t;

typedef struct {
    uint64_t lists[RINHA_TRACE_PHASES];
    uint64_t blocks[RINHA_TRACE_PHASES];
    uint64_t vectors[RINHA_TRACE_PHASES];
} rinha_search_trace_t;

typedef struct {
    uint64_t requests;
    uint64_t flat_requests;
    uint64_t ivf_requests;
    uint64_t fast_certified;
    uint64_t repair_attempts;
    uint64_t repair_certified;
    uint64_t exact_fallbacks;
    uint64_t fast_fraud_bucket[6];
    uint64_t final_fraud_bucket[6];
    uint64_t repair_before_bucket[6];
    uint64_t repair_after_bucket[6];
    uint64_t lists[RINHA_TRACE_PHASES];
    uint64_t blocks[RINHA_TRACE_PHASES];
    uint64_t vectors[RINHA_TRACE_PHASES];
    uint64_t max_lists[RINHA_TRACE_PHASES];
    uint64_t max_blocks[RINHA_TRACE_PHASES];
    uint64_t max_vectors[RINHA_TRACE_PHASES];
    uint64_t fast_worst_sum;
    uint64_t fast_worst_max;
    uint64_t final_worst_sum;
    uint64_t final_worst_max;
    uint64_t total_vector_bucket[7];
    uint64_t total_list_bucket[7];
} rinha_search_stats_t;

static int g_search_stats_enabled;
static rinha_search_stats_t g_search_stats;

static int search_stats_env_enabled(void) {
    const char *value = getenv("RINHA_SEARCH_STATS");
    if (value == NULL || value[0] == '\0') return 0;
    return strcmp(value, "0") != 0 && strcmp(value, "false") != 0 && strcmp(value, "FALSE") != 0;
}

static void search_stats_init(void) __attribute__((constructor));
static void search_stats_init(void) {
    g_search_stats_enabled = search_stats_env_enabled();
}

static const char *trace_phase_name(uint32_t phase) {
    static const char *names[RINHA_TRACE_PHASES] = {"fast", "repair", "exact"};
    return phase < RINHA_TRACE_PHASES ? names[phase] : "unknown";
}

static uint32_t stats_bucket(uint64_t value) {
    if (value == 0) return 0;
    if (value <= 128U) return 1;
    if (value <= 512U) return 2;
    if (value <= 2048U) return 3;
    if (value <= 8192U) return 4;
    if (value <= 32768U) return 5;
    return 6;
}

static void search_stats_dump(void) __attribute__((destructor));
static void search_stats_dump(void) {
    if (!g_search_stats_enabled || g_search_stats.requests == 0) return;
    fprintf(stderr,
            "RINHA_SEARCH_STATS requests=%llu flat=%llu ivf=%llu fast_certified=%llu repair_attempts=%llu repair_certified=%llu exact_fallbacks=%llu\n",
            (unsigned long long)g_search_stats.requests,
            (unsigned long long)g_search_stats.flat_requests,
            (unsigned long long)g_search_stats.ivf_requests,
            (unsigned long long)g_search_stats.fast_certified,
            (unsigned long long)g_search_stats.repair_attempts,
            (unsigned long long)g_search_stats.repair_certified,
            (unsigned long long)g_search_stats.exact_fallbacks);
    fprintf(stderr,
            "RINHA_SEARCH_STATS fast_fraud=%llu,%llu,%llu,%llu,%llu,%llu final_fraud=%llu,%llu,%llu,%llu,%llu,%llu\n",
            (unsigned long long)g_search_stats.fast_fraud_bucket[0],
            (unsigned long long)g_search_stats.fast_fraud_bucket[1],
            (unsigned long long)g_search_stats.fast_fraud_bucket[2],
            (unsigned long long)g_search_stats.fast_fraud_bucket[3],
            (unsigned long long)g_search_stats.fast_fraud_bucket[4],
            (unsigned long long)g_search_stats.fast_fraud_bucket[5],
            (unsigned long long)g_search_stats.final_fraud_bucket[0],
            (unsigned long long)g_search_stats.final_fraud_bucket[1],
            (unsigned long long)g_search_stats.final_fraud_bucket[2],
            (unsigned long long)g_search_stats.final_fraud_bucket[3],
            (unsigned long long)g_search_stats.final_fraud_bucket[4],
            (unsigned long long)g_search_stats.final_fraud_bucket[5]);
    fprintf(stderr,
            "RINHA_SEARCH_STATS repair_before=%llu,%llu,%llu,%llu,%llu,%llu repair_after=%llu,%llu,%llu,%llu,%llu,%llu\n",
            (unsigned long long)g_search_stats.repair_before_bucket[0],
            (unsigned long long)g_search_stats.repair_before_bucket[1],
            (unsigned long long)g_search_stats.repair_before_bucket[2],
            (unsigned long long)g_search_stats.repair_before_bucket[3],
            (unsigned long long)g_search_stats.repair_before_bucket[4],
            (unsigned long long)g_search_stats.repair_before_bucket[5],
            (unsigned long long)g_search_stats.repair_after_bucket[0],
            (unsigned long long)g_search_stats.repair_after_bucket[1],
            (unsigned long long)g_search_stats.repair_after_bucket[2],
            (unsigned long long)g_search_stats.repair_after_bucket[3],
            (unsigned long long)g_search_stats.repair_after_bucket[4],
            (unsigned long long)g_search_stats.repair_after_bucket[5]);
    for (uint32_t phase = 0; phase < RINHA_TRACE_PHASES; ++phase) {
        fprintf(stderr,
                "RINHA_SEARCH_STATS phase=%s lists=%llu max_lists=%llu blocks=%llu max_blocks=%llu vectors=%llu max_vectors=%llu\n",
                trace_phase_name(phase),
                (unsigned long long)g_search_stats.lists[phase],
                (unsigned long long)g_search_stats.max_lists[phase],
                (unsigned long long)g_search_stats.blocks[phase],
                (unsigned long long)g_search_stats.max_blocks[phase],
                (unsigned long long)g_search_stats.vectors[phase],
                (unsigned long long)g_search_stats.max_vectors[phase]);
    }
    uint64_t ivf_requests = g_search_stats.ivf_requests == 0 ? 1U : g_search_stats.ivf_requests;
    fprintf(stderr,
            "RINHA_SEARCH_STATS fast_worst_avg=%llu fast_worst_max=%llu final_worst_avg=%llu final_worst_max=%llu\n",
            (unsigned long long)(g_search_stats.fast_worst_sum / ivf_requests),
            (unsigned long long)g_search_stats.fast_worst_max,
            (unsigned long long)(g_search_stats.final_worst_sum / ivf_requests),
            (unsigned long long)g_search_stats.final_worst_max);
    fprintf(stderr,
            "RINHA_SEARCH_STATS total_vectors_buckets=0:%llu,1-128:%llu,129-512:%llu,513-2048:%llu,2049-8192:%llu,8193-32768:%llu,32769+:%llu\n",
            (unsigned long long)g_search_stats.total_vector_bucket[0],
            (unsigned long long)g_search_stats.total_vector_bucket[1],
            (unsigned long long)g_search_stats.total_vector_bucket[2],
            (unsigned long long)g_search_stats.total_vector_bucket[3],
            (unsigned long long)g_search_stats.total_vector_bucket[4],
            (unsigned long long)g_search_stats.total_vector_bucket[5],
            (unsigned long long)g_search_stats.total_vector_bucket[6]);
    fprintf(stderr,
            "RINHA_SEARCH_STATS total_lists_buckets=0:%llu,1-128:%llu,129-512:%llu,513-2048:%llu,2049-8192:%llu,8193-32768:%llu,32769+:%llu\n",
            (unsigned long long)g_search_stats.total_list_bucket[0],
            (unsigned long long)g_search_stats.total_list_bucket[1],
            (unsigned long long)g_search_stats.total_list_bucket[2],
            (unsigned long long)g_search_stats.total_list_bucket[3],
            (unsigned long long)g_search_stats.total_list_bucket[4],
            (unsigned long long)g_search_stats.total_list_bucket[5],
            (unsigned long long)g_search_stats.total_list_bucket[6]);
}

static void trace_add(rinha_search_trace_t *trace, rinha_trace_phase_t phase, uint64_t lists, uint64_t blocks, uint64_t vectors) {
    if (trace == NULL) return;
    trace->lists[phase] += lists;
    trace->blocks[phase] += blocks;
    trace->vectors[phase] += vectors;
}

static void stats_add_trace(const rinha_search_trace_t *trace) {
    uint64_t total_lists = 0;
    uint64_t total_vectors = 0;
    for (uint32_t phase = 0; phase < RINHA_TRACE_PHASES; ++phase) {
        g_search_stats.lists[phase] += trace->lists[phase];
        g_search_stats.blocks[phase] += trace->blocks[phase];
        g_search_stats.vectors[phase] += trace->vectors[phase];
        total_lists += trace->lists[phase];
        total_vectors += trace->vectors[phase];
        if (trace->lists[phase] > g_search_stats.max_lists[phase]) g_search_stats.max_lists[phase] = trace->lists[phase];
        if (trace->blocks[phase] > g_search_stats.max_blocks[phase]) g_search_stats.max_blocks[phase] = trace->blocks[phase];
        if (trace->vectors[phase] > g_search_stats.max_vectors[phase]) g_search_stats.max_vectors[phase] = trace->vectors[phase];
    }
    ++g_search_stats.total_list_bucket[stats_bucket(total_lists)];
    ++g_search_stats.total_vector_bucket[stats_bucket(total_vectors)];
}

static void stats_record_ivf(uint8_t fast_fraud, uint8_t final_fraud, uint64_t fast_worst, uint64_t final_worst, int fast_certified, int repaired, int repair_certified, int exact_fallback, const rinha_search_trace_t *trace) {
    if (!g_search_stats_enabled) return;
    if (fast_fraud > 5U) fast_fraud = 5U;
    if (final_fraud > 5U) final_fraud = 5U;
    ++g_search_stats.requests;
    ++g_search_stats.ivf_requests;
    if (fast_certified) ++g_search_stats.fast_certified;
    if (repaired) {
        ++g_search_stats.repair_attempts;
        ++g_search_stats.repair_before_bucket[fast_fraud];
        ++g_search_stats.repair_after_bucket[final_fraud];
    }
    if (repair_certified) ++g_search_stats.repair_certified;
    if (exact_fallback) ++g_search_stats.exact_fallbacks;
    ++g_search_stats.fast_fraud_bucket[fast_fraud];
    ++g_search_stats.final_fraud_bucket[final_fraud];
    g_search_stats.fast_worst_sum += fast_worst;
    g_search_stats.final_worst_sum += final_worst;
    if (fast_worst > g_search_stats.fast_worst_max) g_search_stats.fast_worst_max = fast_worst;
    if (final_worst > g_search_stats.final_worst_max) g_search_stats.final_worst_max = final_worst;
    stats_add_trace(trace);
}

static void stats_record_flat(void) {
    if (!g_search_stats_enabled) return;
    ++g_search_stats.requests;
    ++g_search_stats.flat_requests;
}

#define TRACE_PARAMS , rinha_search_trace_t *trace, rinha_trace_phase_t phase
#define TRACE_ARGS(t, p) , (t), (p)
#else
#define TRACE_PARAMS
#define TRACE_ARGS(t, p)
#define trace_add(trace, phase, lists, blocks, vectors) ((void)0)
#define stats_record_ivf(fast_fraud, final_fraud, fast_worst, final_worst, fast_certified, repaired, repair_certified, exact_fallback, trace) ((void)0)
#define stats_record_flat() ((void)0)
#endif

static int probe_before(uint64_t dist, uint64_t tie, const probe_t *probe) {
    return dist < probe->dist || (dist == probe->dist && tie < probe->tie);
}

static void probe_add(probe_t probes[RINHA_MAX_PROBES], uint32_t nprobe, uint64_t dist, uint64_t tie, uint32_t list) {
    if (!probe_before(dist, tie, &probes[nprobe - 1U])) return;
    uint32_t pos = nprobe - 1U;
    while (pos > 0 && probe_before(dist, tie, &probes[pos - 1U])) {
        probes[pos] = probes[pos - 1U];
        --pos;
    }
    probes[pos].dist = dist;
    probes[pos].tie = tie;
    probes[pos].list = list;
}

/* Keep this bucketizer and query_profile_key() in lock-step with
   src/preprocess/build_index.c. The builder writes one bit-packed table and
   the runtime probes it directly; changing either side alone silently disables
   the profile fastpath or returns wrong one-sided buckets. */
static uint32_t positive_bucket(int16_t value, uint32_t buckets) {
    if (value <= 0) return 0;
    uint32_t bucket = (uint32_t)(((int64_t)value * (int64_t)buckets) / (RINHA_SCALE + 1));
    return bucket >= buckets ? buckets - 1U : bucket;
}

static uint32_t query_profile_key(const int16_t *v) {
    uint32_t key = 0;
    key |= positive_bucket(v[2], 16U);
    key |= positive_bucket(v[7], 8U) << 4;
    key |= positive_bucket(v[8], 4U) << 7;
    key |= positive_bucket(v[12], 4U) << 9;
    key |= positive_bucket(v[0], 4U) << 11;
    key |= (uint32_t)(v[5] < 0) << 13;
    key |= (uint32_t)(v[9] > 0) << 14;
    key |= (uint32_t)(v[10] > 0) << 15;
    key |= (uint32_t)(v[11] > 0) << 16;
    key |= positive_bucket(v[6], 4U) << 17;
    key |= (uint32_t)(v[1] > (RINHA_SCALE / 10)) << 19;
    key |= positive_bucket(v[13], 4U) << 20;
    return key;
}

static int try_profile_fastpath(const rinha_index_t *index, const int16_t query[RINHA_DIMS], uint8_t *fraud) {
    /* Some vector buckets are pure legit/fraud in the training set. The index
       stores those masks so obvious requests skip nearest-neighbor search. */
    if (!index->profile_fastpath || index->profile_counts == NULL || index->profile_masks == NULL) return 0;
    uint32_t key = query_profile_key(query);
    uint8_t mask = index->profile_masks[key];
    uint16_t count = index->profile_counts[key];
    if (mask == RINHA_FASTPATH_LEGIT_MASK && count >= index->profile_legit_min_count) {
        *fraud = 0;
        return 1;
    }
    if (mask == RINHA_FASTPATH_FRAUD_MASK && count >= index->profile_fraud_min_count) {
        *fraud = 5;
        return 1;
    }
    return 0;
}


/* Reference fastpath keys also mirror the preprocess specs exactly: same
   feature order, bin count, and shift order. They are written once into the
   mmaped index and then read here without schema metadata. */
static uint32_t reference_fastpath_bin(int16_t value, const int16_t *edges, uint32_t bins) {
    for (uint32_t bin = 0; bin + 1U < bins; ++bin) {
        if (value < edges[bin]) return bin;
    }
    return bins - 1U;
}

static uint32_t reference_fastpath1_key(const rinha_index_t *index, const int16_t *v) {
    const int16_t *edges = index->reference_fastpath1_edges;
    uint32_t key = 0;
    uint32_t shift = 0;
#define ADD_REF_BIN(dim, bins) do { key |= reference_fastpath_bin(v[(dim)], edges, (bins)) << shift; shift += __builtin_ctz((bins)); edges += (bins); } while (0)
    ADD_REF_BIN(0, 16U);
    ADD_REF_BIN(7, 8U);
    ADD_REF_BIN(10, 64U);
    ADD_REF_BIN(1, 2U);
    ADD_REF_BIN(9, 8U);
    ADD_REF_BIN(11, 16U);
    ADD_REF_BIN(12, 2U);
    ADD_REF_BIN(3, 4U);
#undef ADD_REF_BIN
    return key;
}

static uint32_t reference_fastpath2_key(const rinha_index_t *index, const int16_t *v) {
    const int16_t *edges = index->reference_fastpath2_edges;
    uint32_t key = 0;
    uint32_t shift = 0;
#define ADD_REF_BIN(dim, bins) do { key |= reference_fastpath_bin(v[(dim)], edges, (bins)) << shift; shift += __builtin_ctz((bins)); edges += (bins); } while (0)
    ADD_REF_BIN(5, 16U);
    ADD_REF_BIN(13, 16U);
    ADD_REF_BIN(6, 16U);
    ADD_REF_BIN(1, 16U);
    ADD_REF_BIN(12, 16U);
#undef ADD_REF_BIN
    return key;
}

static int try_reference_fastpath(const rinha_index_t *index, const int16_t query[RINHA_DIMS], uint8_t *fraud) {
    /* Reference fastpaths are coarser lookup tables generated at preprocess
       time. They trade a tiny mmap lookup for avoiding the expensive scan when
       the bucket was proven one-sided enough for the selected lane. */
    if (!index->reference_fastpath || index->reference_fastpath1 == NULL || index->reference_fastpath1_edges == NULL) return 0;
    uint8_t result = index->reference_fastpath1[reference_fastpath1_key(index, query)];
    if (result == RINHA_FASTPATH_LEGIT_MASK && index->reference_fastpath_legit) {
        *fraud = 0;
        return 1;
    }
    if (result == RINHA_FASTPATH_FRAUD_MASK && index->reference_fastpath_fraud) {
        *fraud = 5;
        return 1;
    }
    if (index->reference_fastpath2 == NULL || index->reference_fastpath2_edges == NULL) return 0;
    result = index->reference_fastpath2[reference_fastpath2_key(index, query)];
    if (result == RINHA_FASTPATH_LEGIT_MASK && index->reference_fastpath2_legit) {
        *fraud = 0;
        return 1;
    }
    if (result == RINHA_FASTPATH_FRAUD_MASK && index->reference_fastpath2_fraud) {
        *fraud = 5;
        return 1;
    }
    return 0;
}

static uint64_t list_lower_bound(const rinha_index_t *index, const int16_t query[RINHA_DIMS], uint32_t list) {
    const int16_t *mins = index->bounds_min + (size_t)list * RINHA_DIMS;
    const int16_t *maxs = index->bounds_max + (size_t)list * RINHA_DIMS;
    uint64_t sum = 0;
#ifdef __AVX2__
    __m128i q16 = _mm_loadu_si128((const __m128i *)query);
    __m128i min16 = _mm_loadu_si128((const __m128i *)mins);
    __m128i max16 = _mm_loadu_si128((const __m128i *)maxs);
    __m128i clamped16 = _mm_min_epi16(_mm_max_epi16(q16, min16), max16);
    __m128i diff16 = _mm_sub_epi16(q16, clamped16);
    __m256i diff32 = _mm256_cvtepi16_epi32(diff16);
    __m256i sq32 = _mm256_mullo_epi32(diff32, diff32);
    __m128i lo = _mm256_castsi256_si128(sq32);
    __m128i hi = _mm256_extracti128_si256(sq32, 1);
    __m128i pair = _mm_add_epi32(lo, hi);
    pair = _mm_add_epi32(pair, _mm_srli_si128(pair, 8));
    pair = _mm_add_epi32(pair, _mm_srli_si128(pair, 4));
    sum = (uint64_t)(uint32_t)_mm_cvtsi128_si32(pair);
    for (int d = 8; d < RINHA_DIMS; ++d) {
#else
    for (int d = 0; d < RINHA_DIMS; ++d) {
#endif
        int16_t q = query[d];
        if (q < mins[d]) {
            sum += rinha_sqdiff_i16(q, mins[d]);
        } else if (q > maxs[d]) {
            sum += rinha_sqdiff_i16(q, maxs[d]);
        }
    }
    return sum;
}

static uint64_t centroid_distance(const rinha_index_t *index, const int16_t query[RINHA_DIMS], uint32_t list) {
    if (index->layout != RINHA_INDEX_LAYOUT_IVF_KMEANS_BLOCK16) {
        return rinha_dist_i16(query, index->centroids + (size_t)list * RINHA_DIMS);
    }
    uint64_t sum = 0;
    for (int d = 0; d < RINHA_DIMS; ++d) {
        sum += rinha_sqdiff_i16(query[d], index->centroids[(size_t)d * index->list_count + list]);
    }
    return sum;
}

#ifdef __AVX2__
static void select_v2_probes_avx2(const rinha_index_t *index, const int16_t query[RINHA_DIMS], probe_t probes[RINHA_MAX_PROBES], uint32_t max_nprobe) {
    uint32_t list = 0;
    for (; list + 8U <= index->list_count; list += 8U) {
        __m256i acc_lo = _mm256_setzero_si256();
        __m256i acc_hi = _mm256_setzero_si256();
        for (int d = 0; d < RINHA_DIMS; ++d) {
            const int16_t *centroids = index->centroids + (size_t)d * index->list_count + list;
            __m128i c16 = _mm_loadu_si128((const __m128i *)centroids);
            __m256i c32 = _mm256_cvtepi16_epi32(c16);
            __m256i q32 = _mm256_set1_epi32(query[d]);
            __m256i diff = _mm256_sub_epi32(c32, q32);
            __m256i sq = _mm256_mullo_epi32(diff, diff);
            acc_lo = _mm256_add_epi64(acc_lo, _mm256_cvtepu32_epi64(_mm256_castsi256_si128(sq)));
            acc_hi = _mm256_add_epi64(acc_hi, _mm256_cvtepu32_epi64(_mm256_extracti128_si256(sq, 1)));
        }
        uint64_t dist[8];
        _mm256_storeu_si256((__m256i *)(dist + 0), acc_lo);
        _mm256_storeu_si256((__m256i *)(dist + 4), acc_hi);
        for (uint32_t lane = 0; lane < 8U; ++lane) probe_add(probes, max_nprobe, dist[lane], dist[lane], list + lane);
    }
    for (; list < index->list_count; ++list) {
        uint64_t dist = centroid_distance(index, query, list);
        probe_add(probes, max_nprobe, dist, dist, list);
    }
}
#endif

#ifndef __AVX2__
static uint64_t block8_lane_dist_bounded(const int16_t query[RINHA_DIMS], const int16_t *block, uint32_t lane, uint64_t limit) {
    uint64_t sum = 0;
    for (int d = 0; d < RINHA_DIMS; ++d) {
        sum += rinha_sqdiff_i16(query[d], block[(size_t)d * 8U + lane]);
        if (sum >= limit) return sum;
    }
    return sum;
}
#endif

#ifdef __AVX2__
static void block8_accum_pair_avx2(const int16_t query[RINHA_DIMS], const int16_t *block, int d, __m256i *acc_lo, __m256i *acc_hi) {
    __m128i vd = _mm_loadu_si128((const __m128i *)(block + (size_t)d * 8U));
    __m128i ve = _mm_loadu_si128((const __m128i *)(block + (size_t)(d + 1) * 8U));
    __m128i qd = _mm_set1_epi16(query[d]);
    __m128i qe = _mm_set1_epi16(query[d + 1]);
    __m128i dd = _mm_sub_epi16(vd, qd);
    __m128i de = _mm_sub_epi16(ve, qe);
    __m128i packed_lo = _mm_unpacklo_epi16(dd, de);
    __m128i packed_hi = _mm_unpackhi_epi16(dd, de);
    __m256i packed = _mm256_set_m128i(packed_hi, packed_lo);
    __m256i pair_sum = _mm256_madd_epi16(packed, packed);
    __m128i pair_lo = _mm256_castsi256_si128(pair_sum);
    __m128i pair_hi = _mm256_extracti128_si256(pair_sum, 1);
    *acc_lo = _mm256_add_epi64(*acc_lo, _mm256_cvtepu32_epi64(pair_lo));
    *acc_hi = _mm256_add_epi64(*acc_hi, _mm256_cvtepu32_epi64(pair_hi));
}

/* Block indexes use a dimension-major SoA layout: block[d * lanes + lane].
   That lets AVX2 compare 8 or 16 candidates per dimension while keeping the
   unsorted top-5 cutoff hot in registers. The first eight dimensions are a
   cheap reject pass; only plausible lanes pay for all 14 dimensions. */
static int block8_dist_avx2(const int16_t query[RINHA_DIMS], const int16_t *block, uint64_t limit, uint64_t dist[8]) {
    __m256i acc_lo = _mm256_setzero_si256();
    __m256i acc_hi = _mm256_setzero_si256();
    for (int d = 0; d < 8; d += 2) block8_accum_pair_avx2(query, block, d, &acc_lo, &acc_hi);
    _mm256_storeu_si256((__m256i *)dist, acc_lo);
    _mm256_storeu_si256((__m256i *)(dist + 4), acc_hi);
    int any_candidate = 0;
    for (uint32_t lane = 0; lane < 8U; ++lane) any_candidate |= dist[lane] < limit;
    if (!any_candidate) return 0;
    for (int d = 8; d < RINHA_DIMS; d += 2) block8_accum_pair_avx2(query, block, d, &acc_lo, &acc_hi);
    _mm256_storeu_si256((__m256i *)dist, acc_lo);
    _mm256_storeu_si256((__m256i *)(dist + 4), acc_hi);
    return 1;
}
#endif

static void scan_list_block8(const rinha_index_t *index, const int16_t query[RINHA_DIMS], uint32_t list, rinha_top5_t *top TRACE_PARAMS) {
    uint32_t remaining = index->offsets[list + 1U] - index->offsets[list];
    uint32_t slot = index->slot_offsets[list];
#ifdef RINHA_SEARCH_STATS
    uint64_t blocks = (remaining + 7U) >> 3;
    trace_add(trace, phase, 1U, blocks, remaining);
#endif
    while (remaining > 0) {
        uint32_t valid = remaining < 8U ? remaining : 8U;
        const int16_t *block = index->vectors + (size_t)slot * RINHA_DIMS;
#ifdef __AVX2__
        uint64_t worst = rinha_top5_worst_dist(top);
        uint64_t dist[8];
        if (!block8_dist_avx2(query, block, worst, dist)) {
            slot += 8U;
            remaining -= valid;
            continue;
        }
        for (uint32_t lane = 0; lane < valid; ++lane) {
            if (dist[lane] < worst) {
                rinha_top5_add(top, dist[lane], index->labels[slot + lane]);
                worst = rinha_top5_worst_dist(top);
            }
        }
#else
        for (uint32_t lane = 0; lane < valid; ++lane) {
            uint64_t dist = block8_lane_dist_bounded(query, block, lane, rinha_top5_worst_dist(top));
            rinha_top5_add(top, dist, index->labels[slot + lane]);
        }
#endif
        slot += 8U;
        remaining -= valid;
    }
}

#ifndef __AVX2__
static uint64_t block16_lane_dist_bounded(const int16_t query[RINHA_DIMS], const int16_t *block, uint32_t lane, uint64_t limit) {
    uint64_t sum = 0;
    for (int d = 0; d < RINHA_DIMS; ++d) {
        sum += rinha_sqdiff_i16(query[d], block[(size_t)d * 16U + lane]);
        if (sum >= limit) return sum;
    }
    return sum;
}
#endif

#ifdef __AVX2__
static void block16_accum_pair_avx2(const int16_t query[RINHA_DIMS], const int16_t *block, int d, __m256i acc[4]) {
    __m256i vd = _mm256_loadu_si256((const __m256i *)(block + (size_t)d * 16U));
    __m256i ve = _mm256_loadu_si256((const __m256i *)(block + (size_t)(d + 1) * 16U));
    __m256i qd = _mm256_set1_epi16(query[d]);
    __m256i qe = _mm256_set1_epi16(query[d + 1]);
    __m256i dd = _mm256_sub_epi16(vd, qd);
    __m256i de = _mm256_sub_epi16(ve, qe);
    __m256i packed_lo = _mm256_unpacklo_epi16(dd, de);
    __m256i packed_hi = _mm256_unpackhi_epi16(dd, de);
    __m256i pair_lo = _mm256_madd_epi16(packed_lo, packed_lo);
    __m256i pair_hi = _mm256_madd_epi16(packed_hi, packed_hi);
    acc[0] = _mm256_add_epi64(acc[0], _mm256_cvtepu32_epi64(_mm256_castsi256_si128(pair_lo)));
    acc[1] = _mm256_add_epi64(acc[1], _mm256_cvtepu32_epi64(_mm256_extracti128_si256(pair_lo, 1)));
    acc[2] = _mm256_add_epi64(acc[2], _mm256_cvtepu32_epi64(_mm256_castsi256_si128(pair_hi)));
    acc[3] = _mm256_add_epi64(acc[3], _mm256_cvtepu32_epi64(_mm256_extracti128_si256(pair_hi, 1)));
}

/* Same SoA block contract as block8, widened to 16 lanes for the k-means
   layout. The unusual store order preserves lane numbers after unpack/madd. */
static int block16_dist_avx2(const int16_t query[RINHA_DIMS], const int16_t *block, uint64_t limit, uint64_t dist[16]) {
    __m256i acc[4] = {_mm256_setzero_si256(), _mm256_setzero_si256(), _mm256_setzero_si256(), _mm256_setzero_si256()};
    for (int d = 0; d < 8; d += 2) block16_accum_pair_avx2(query, block, d, acc);
    _mm256_storeu_si256((__m256i *)(dist + 0), acc[0]);
    _mm256_storeu_si256((__m256i *)(dist + 8), acc[1]);
    _mm256_storeu_si256((__m256i *)(dist + 4), acc[2]);
    _mm256_storeu_si256((__m256i *)(dist + 12), acc[3]);
    int any_candidate = 0;
    for (uint32_t lane = 0; lane < 16U; ++lane) any_candidate |= dist[lane] < limit;
    if (!any_candidate) return 0;
    for (int d = 8; d < RINHA_DIMS; d += 2) block16_accum_pair_avx2(query, block, d, acc);
    _mm256_storeu_si256((__m256i *)(dist + 0), acc[0]);
    _mm256_storeu_si256((__m256i *)(dist + 8), acc[1]);
    _mm256_storeu_si256((__m256i *)(dist + 4), acc[2]);
    _mm256_storeu_si256((__m256i *)(dist + 12), acc[3]);
    return 1;
}
#endif

static void scan_list_block16(const rinha_index_t *index, const int16_t query[RINHA_DIMS], uint32_t list, rinha_top5_t *top TRACE_PARAMS) {
    uint32_t start_block = index->offsets[list];
    uint32_t end_block = index->offsets[list + 1U];
    trace_add(trace, phase, 1U, end_block - start_block, (uint64_t)(end_block - start_block) * 16U);
    for (uint32_t block_index = start_block; block_index < end_block; ++block_index) {
        const int16_t *block = index->vectors + (size_t)block_index * RINHA_DIMS * 16U;
        const uint8_t *labels = index->labels + (size_t)block_index * 16U;
#ifdef __AVX2__
        uint64_t worst = rinha_top5_worst_dist(top);
        uint64_t dist[16];
        if (!block16_dist_avx2(query, block, worst, dist)) continue;
        for (uint32_t lane = 0; lane < 16U; ++lane) {
            if (dist[lane] < worst) {
                rinha_top5_add(top, dist[lane], labels[lane]);
                worst = rinha_top5_worst_dist(top);
            }
        }
#else
        for (uint32_t lane = 0; lane < 16U; ++lane) {
            uint64_t dist = block16_lane_dist_bounded(query, block, lane, rinha_top5_worst_dist(top));
            rinha_top5_add(top, dist, labels[lane]);
        }
#endif
    }
}

static void scan_list(const rinha_index_t *index, const int16_t query[RINHA_DIMS], uint32_t list, rinha_top5_t *top TRACE_PARAMS) {
    if (index->layout == RINHA_INDEX_LAYOUT_IVF_KMEANS_BLOCK16) {
        scan_list_block16(index, query, list, top TRACE_ARGS(trace, phase));
        return;
    }
    if (index->layout == RINHA_INDEX_LAYOUT_IVF_BLOCK8 && index->slot_offsets != 0) {
        scan_list_block8(index, query, list, top TRACE_ARGS(trace, phase));
        return;
    }
    uint32_t start = index->offsets[list];
    uint32_t end = index->offsets[list + 1U];
    trace_add(trace, phase, 1U, 0U, end - start);
    for (uint32_t i = start; i < end; ++i) {
        const int16_t *vec = index->vectors + (size_t)i * RINHA_DIMS;
        uint64_t dist = rinha_dist_i16_bounded(query, vec, rinha_top5_worst_dist(top));
        rinha_top5_add(top, dist, index->labels[i]);
    }
}

static uint32_t kd_semantic_partition_key(const int16_t *v) {
    uint32_t key = 0;
    key |= (uint32_t)(v[5] < 0) << 7;
    key |= (uint32_t)(v[9] > 0) << 6;
    key |= (uint32_t)(v[10] > 0) << 5;
    key |= (uint32_t)(v[11] > 0) << 4;
    key |= (positive_bucket(v[2], 4U) & 3U) << 2;
    key |= (positive_bucket(v[7], 4U) & 3U);
    return key;
}

static uint64_t kd_node_lower_bound(const rinha_kd_node_disk_t *node, const int16_t query[RINHA_DIMS]) {
    uint64_t sum = 0;
    for (int d = 0; d < RINHA_DIMS; ++d) {
        int16_t q = query[d];
        if (q < node->min[d]) sum += rinha_sqdiff_i16(q, node->min[d]);
        else if (q > node->max[d]) sum += rinha_sqdiff_i16(q, node->max[d]);
    }
    return sum;
}

static int kd_early_done(const rinha_index_t *index, const rinha_top5_t *top) {
    return index->kd_early_distance_limit != 0U && rinha_top5_worst_dist(top) <= index->kd_early_distance_limit;
}

static int scan_kd_leaf_block8(const rinha_index_t *index, const int16_t query[RINHA_DIMS], const rinha_kd_node_disk_t *node, rinha_top5_t *top TRACE_PARAMS) {
    uint32_t remaining = node->count;
    uint32_t slot = node->start;
    trace_add(trace, phase, 0U, (remaining + 7U) >> 3, remaining);
    while (remaining > 0) {
        uint32_t valid = remaining < 8U ? remaining : 8U;
        const int16_t *block = index->vectors + (size_t)slot * RINHA_DIMS;
#ifdef __AVX2__
        uint64_t worst = rinha_top5_worst_dist(top);
        uint64_t dist[8];
        if (!block8_dist_avx2(query, block, worst, dist)) { slot += 8U; remaining -= valid; continue; }
        for (uint32_t lane = 0; lane < valid; ++lane) {
            if (dist[lane] < worst) { rinha_top5_add(top, dist[lane], index->labels[slot + lane]); worst = rinha_top5_worst_dist(top); }
        }
        if (kd_early_done(index, top)) return 1;
#else
        for (uint32_t lane = 0; lane < valid; ++lane) {
            uint64_t dist = block8_lane_dist_bounded(query, block, lane, rinha_top5_worst_dist(top));
            rinha_top5_add(top, dist, index->labels[slot + lane]);
        }
        if (kd_early_done(index, top)) return 1;
#endif
        slot += 8U;
        remaining -= valid;
    }
    return 0;
}

static int search_kd_node(const rinha_index_t *index, const int16_t query[RINHA_DIMS], uint32_t node_id, rinha_top5_t *top TRACE_PARAMS) {
    if (node_id == UINT32_MAX || node_id >= index->kd_node_count) return 0;
    const rinha_kd_node_disk_t *nodes = (const rinha_kd_node_disk_t *)index->kd_nodes;
    const rinha_kd_node_disk_t *node = nodes + node_id;
    if (kd_node_lower_bound(node, query) >= rinha_top5_worst_dist(top)) return 0;
    trace_add(trace, phase, 1U, 0U, 0U);
    if (node->leaf) return scan_kd_leaf_block8(index, query, node, top TRACE_ARGS(trace, phase));
    const rinha_kd_node_disk_t *left = node->left != UINT32_MAX ? nodes + node->left : NULL;
    const rinha_kd_node_disk_t *right = node->right != UINT32_MAX ? nodes + node->right : NULL;
    uint64_t dl = left ? kd_node_lower_bound(left, query) : UINT64_MAX;
    uint64_t dr = right ? kd_node_lower_bound(right, query) : UINT64_MAX;
    if (dl <= dr) {
        if (dl < rinha_top5_worst_dist(top) && search_kd_node(index, query, node->left, top TRACE_ARGS(trace, phase))) return 1;
        if (dr < rinha_top5_worst_dist(top) && search_kd_node(index, query, node->right, top TRACE_ARGS(trace, phase))) return 1;
    } else {
        if (dr < rinha_top5_worst_dist(top) && search_kd_node(index, query, node->right, top TRACE_ARGS(trace, phase))) return 1;
        if (dl < rinha_top5_worst_dist(top) && search_kd_node(index, query, node->left, top TRACE_ARGS(trace, phase))) return 1;
    }
    return 0;
}

static uint8_t search_kd_tree(const rinha_index_t *index, const int16_t query[RINHA_DIMS]) {
#ifdef RINHA_SEARCH_STATS
    rinha_search_trace_t trace;
    memset(&trace, 0, sizeof(trace));
#endif
    uint8_t profile_fraud = 0;
    if (try_profile_fastpath(index, query, &profile_fraud)) {
        stats_record_ivf(profile_fraud, profile_fraud, 0U, 0U, 1, 0, 0, 0, &trace);
        return profile_fraud;
    }
    uint8_t reference_fraud = 0;
    if (try_reference_fastpath(index, query, &reference_fraud)) {
        stats_record_ivf(reference_fraud, reference_fraud, 0U, 0U, 1, 0, 0, 0, &trace);
        return reference_fraud;
    }
    rinha_top5_t top;
    rinha_top5_init(&top);
    probe_t ordered_partitions[256];
    const rinha_kd_node_disk_t *nodes = (const rinha_kd_node_disk_t *)index->kd_nodes;
    uint32_t exact = kd_semantic_partition_key(query);
    uint32_t part_count = 0;
    for (uint32_t p = 0; p < 256U; ++p) {
        uint32_t root = index->offsets[p];
        if (root == UINT32_MAX || root >= index->kd_node_count) continue;
        uint64_t lb = kd_node_lower_bound(nodes + root, query);
        ordered_partitions[part_count].dist = (p == exact) ? 0U : lb;
        ordered_partitions[part_count].tie = (p == exact) ? 0U : lb;
        ordered_partitions[part_count].list = p;
        ++part_count;
    }
    for (uint32_t i = 1; i < part_count; ++i) {
        probe_t v = ordered_partitions[i];
        uint32_t j = i;
        while (j > 0 && (v.dist < ordered_partitions[j - 1U].dist || (v.dist == ordered_partitions[j - 1U].dist && v.tie < ordered_partitions[j - 1U].tie))) { ordered_partitions[j] = ordered_partitions[j - 1U]; --j; }
        ordered_partitions[j] = v;
    }
    for (uint32_t i = 0; i < part_count; ++i) {
        uint32_t root = index->offsets[ordered_partitions[i].list];
        if (ordered_partitions[i].tie >= rinha_top5_worst_dist(&top)) break;
        if (search_kd_node(index, query, root, &top TRACE_ARGS(&trace, RINHA_TRACE_EXACT))) break;
    }
    uint8_t fraud = rinha_top5_fraud_count(&top);
#ifdef RINHA_SEARCH_STATS
    uint64_t worst = rinha_top5_worst_dist(&top);
    stats_record_ivf(fraud, fraud, worst, worst, 1, 0, 0, 0, &trace);
#endif
    return fraud;
}

static int list_was_scanned(const probe_t probes[RINHA_MAX_PROBES], uint32_t scanned_count, uint32_t list) {
    for (uint32_t i = 0; i < scanned_count; ++i) {
        if (probes[i].list == list) return 1;
    }
    return 0;
}

static void scan_unscanned_lists(const rinha_index_t *index, const int16_t query[RINHA_DIMS], const probe_t probes[RINHA_MAX_PROBES], uint32_t scanned_count, rinha_top5_t *top TRACE_PARAMS) {
    for (uint32_t list = 0; list < index->list_count; ++list) {
        if (list_was_scanned(probes, scanned_count, list)) continue;
        if (index->bounds_min != 0 && index->bounds_max != 0 && list_lower_bound(index, query, list) >= rinha_top5_worst_dist(top)) continue;
        scan_list(index, query, list, top TRACE_ARGS(trace, phase));
    }
}

/* Certification means the next unscanned list is already farther away than
   the current worst top-5 neighbor by its bounding box lower bound. At that
   point no later list can change the majority fraud count. */
static int search_certified(const rinha_index_t *index, const probe_t probes[RINHA_MAX_PROBES], uint32_t scanned_count, const rinha_top5_t *top) {
    if (scanned_count >= index->list_count) return 1;
    if (index->bounds_min == 0 || index->bounds_max == 0) return 0;
    return scanned_count < RINHA_MAX_PROBES && rinha_top5_worst_dist(top) < probes[scanned_count].dist;
}

static uint8_t search_flat(const rinha_index_t *index, const int16_t query[RINHA_DIMS]) {
    rinha_top5_t top;
    rinha_top5_init(&top);
    for (uint32_t i = 0; i < index->count; ++i) {
        const int16_t *vec = index->vectors + (size_t)i * RINHA_DIMS;
        uint64_t dist = rinha_dist_i16_bounded(query, vec, rinha_top5_worst_dist(&top));
        rinha_top5_add(&top, dist, index->labels[i]);
    }
    stats_record_flat();
    return rinha_top5_fraud_count(&top);
}

static uint8_t search_ivf(const rinha_index_t *index, const int16_t query[RINHA_DIMS]) {
#ifdef RINHA_SEARCH_STATS
    rinha_search_trace_t trace;
    memset(&trace, 0, sizeof(trace));
    uint8_t fast_fraud = 0;
    uint64_t fast_worst = 0;
    uint64_t final_worst = 0;
    int fast_certified = 0;
    int repaired = 0;
    int repair_certified = 0;
    int exact_fallback = 0;
#endif
    uint8_t profile_fraud = 0;
    if (try_profile_fastpath(index, query, &profile_fraud)) {
        stats_record_ivf(profile_fraud, profile_fraud, 0U, 0U, 1, 0, 0, 0, &trace);
        return profile_fraud;
    }
    uint8_t reference_fraud = 0;
    if (try_reference_fastpath(index, query, &reference_fraud)) {
        stats_record_ivf(reference_fraud, reference_fraud, 0U, 0U, 1, 0, 0, 0, &trace);
        return reference_fraud;
    }
    uint32_t nprobe = index->nprobe;
    if (nprobe == 0) nprobe = RINHA_INDEX_DEFAULT_NPROBE;
    if (nprobe > index->list_count) nprobe = index->list_count;
    if (nprobe > RINHA_MAX_NPROBE) nprobe = RINHA_MAX_NPROBE;
    uint32_t repair_nprobe = index->repair_nprobe;
    if (repair_nprobe > index->list_count) repair_nprobe = index->list_count;
    if (repair_nprobe > RINHA_MAX_NPROBE) repair_nprobe = RINHA_MAX_NPROBE;
    if (repair_nprobe < nprobe) repair_nprobe = nprobe;
    uint32_t max_nprobe = repair_nprobe;
    if (max_nprobe < index->list_count && max_nprobe < RINHA_MAX_PROBES) ++max_nprobe;

    probe_t probes[RINHA_MAX_PROBES];
    for (uint32_t i = 0; i < max_nprobe; ++i) {
        probes[i].dist = UINT64_MAX;
        probes[i].tie = UINT64_MAX;
        probes[i].list = 0;
    }
    if (index->layout == RINHA_INDEX_LAYOUT_IVF_KMEANS_BLOCK16 && index->bounds_min == 0 && index->bounds_max == 0) {
#ifdef __AVX2__
        select_v2_probes_avx2(index, query, probes, max_nprobe);
#else
        for (uint32_t list = 0; list < index->list_count; ++list) {
            uint64_t dist = centroid_distance(index, query, list);
            probe_add(probes, max_nprobe, dist, dist, list);
        }
#endif
    } else {
        for (uint32_t list = 0; list < index->list_count; ++list) {
            uint64_t lower;
            uint64_t centroid_dist_value;
            if (index->bounds_min != 0 && index->bounds_max != 0) {
                lower = list_lower_bound(index, query, list);
                if (lower > probes[max_nprobe - 1U].dist) continue;
                centroid_dist_value = centroid_distance(index, query, list);
            } else {
                centroid_dist_value = centroid_distance(index, query, list);
                lower = centroid_dist_value;
            }
            probe_add(probes, max_nprobe, lower, centroid_dist_value, list);
        }
    }

    rinha_top5_t top;
    rinha_top5_init(&top);
    for (uint32_t p = 0; p < nprobe; ++p) {
        scan_list(index, query, probes[p].list, &top TRACE_ARGS(&trace, RINHA_TRACE_FAST));
    }
    uint8_t fraud = rinha_top5_fraud_count(&top);
    uint32_t scanned_count = nprobe;
#ifdef RINHA_SEARCH_STATS
    fast_fraud = fraud;
    fast_worst = rinha_top5_worst_dist(&top);
#endif
    if (search_certified(index, probes, scanned_count, &top)) {
#ifdef RINHA_SEARCH_STATS
        fast_certified = 1;
        final_worst = fast_worst;
        stats_record_ivf(fast_fraud, fraud, fast_worst, final_worst, fast_certified, repaired, repair_certified, exact_fallback, &trace);
#endif
        return fraud;
    }
    if (early_done(&top)) {
#ifdef RINHA_SEARCH_STATS
        final_worst = rinha_top5_worst_dist(&top);
        stats_record_ivf(fast_fraud, fraud, fast_worst, final_worst, fast_certified, repaired, repair_certified, exact_fallback, &trace);
#endif
        return fraud;
    }
    uint64_t worst = rinha_top5_worst_dist(&top);
    /* Only pay for repair/exact scans when the first probe set is near a
       decision boundary: either the top-5 fraud count is in the tuned repair
       band, or its worst neighbor is too far for that bucket. */
    int near_decision_boundary = (fraud >= index->repair_min_fraud && fraud <= index->repair_max_fraud) ||
        (fraud <= 5U && index->repair_worst_threshold[fraud] != 0U && worst >= index->repair_worst_threshold[fraud]);
    if (repair_nprobe > nprobe && near_decision_boundary) {
#ifdef RINHA_SEARCH_STATS
        repaired = 1;
#endif
        for (uint32_t p = nprobe; p < repair_nprobe; ++p) {
            scan_list(index, query, probes[p].list, &top TRACE_ARGS(&trace, RINHA_TRACE_REPAIR));
        }
        scanned_count = repair_nprobe;
        fraud = rinha_top5_fraud_count(&top);
        if (search_certified(index, probes, scanned_count, &top)) {
#ifdef RINHA_SEARCH_STATS
            repair_certified = 1;
            final_worst = rinha_top5_worst_dist(&top);
            stats_record_ivf(fast_fraud, fraud, fast_worst, final_worst, fast_certified, repaired, repair_certified, exact_fallback, &trace);
#endif
            return fraud;
        }
        if (early_done(&top)) {
#ifdef RINHA_SEARCH_STATS
            final_worst = rinha_top5_worst_dist(&top);
            stats_record_ivf(fast_fraud, fraud, fast_worst, final_worst, fast_certified, repaired, repair_certified, exact_fallback, &trace);
#endif
            return fraud;
        }
        worst = rinha_top5_worst_dist(&top);
        near_decision_boundary = (fraud >= index->repair_min_fraud && fraud <= index->repair_max_fraud) ||
            (fraud <= 5U && index->repair_worst_threshold[fraud] != 0U && worst >= index->repair_worst_threshold[fraud]);
    }
    if (near_decision_boundary && index->exact_fallback != 0 && index->bounds_min != 0 && index->bounds_max != 0) {
#ifdef RINHA_SEARCH_STATS
        exact_fallback = 1;
#endif
        scan_unscanned_lists(index, query, probes, scanned_count, &top TRACE_ARGS(&trace, RINHA_TRACE_EXACT));
        fraud = rinha_top5_fraud_count(&top);
    }
#ifdef RINHA_SEARCH_STATS
    final_worst = rinha_top5_worst_dist(&top);
    stats_record_ivf(fast_fraud, fraud, fast_worst, final_worst, fast_certified, repaired, repair_certified, exact_fallback, &trace);
#endif
    return fraud;
}

uint8_t rinha_search_fraud_count(const rinha_index_t *index, const int16_t query[RINHA_DIMS]) {
    if (index == 0 || index->count == 0 || index->vectors == 0 || index->labels == 0) return 0;
    if (index->layout == RINHA_INDEX_LAYOUT_KD_TREE_BLOCK8 && index->offsets != 0 && index->kd_nodes != 0 && index->kd_node_count > 0) return search_kd_tree(index, query);
    if ((index->layout == RINHA_INDEX_LAYOUT_IVF || index->layout == RINHA_INDEX_LAYOUT_IVF_BLOCK8 || index->layout == RINHA_INDEX_LAYOUT_IVF_KMEANS_BLOCK16) && index->offsets != 0 && index->centroids != 0 && index->list_count > 0) return search_ivf(index, query);
    return search_flat(index, query);
}
