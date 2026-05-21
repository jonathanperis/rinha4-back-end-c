#include "common/distance.h"
#include "common/index_format.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct {
    uint32_t key;
    uint32_t index;
} sort_item_t;

static int env_enabled(const char *name) {
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') return 0;
    return strcmp(value, "0") != 0 && strcmp(value, "false") != 0 && strcmp(value, "FALSE") != 0;
}

static uint32_t env_u32(const char *name, uint32_t fallback, uint32_t min, uint32_t max) {
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') return fallback;
    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || parsed < min || parsed > max) return fallback;
    return (uint32_t)parsed;
}

static char *read_gzip(const char *path, size_t *out_len) {
    gzFile file = gzopen(path, "rb");
    if (file == NULL) return NULL;
    size_t cap = 32U * 1024U * 1024U;
    size_t len = 0;
    char *data = (char *)malloc(cap + 1);
    if (data == NULL) {
        gzclose(file);
        return NULL;
    }
    for (;;) {
        if (len + 1048576U > cap) {
            cap *= 2U;
            char *next = (char *)realloc(data, cap + 1);
            if (next == NULL) {
                free(data);
                gzclose(file);
                return NULL;
            }
            data = next;
        }
        int n = gzread(file, data + len, 1048576U);
        if (n < 0) {
            free(data);
            gzclose(file);
            return NULL;
        }
        if (n == 0) break;
        len += (size_t)n;
    }
    gzclose(file);
    data[len] = '\0';
    *out_len = len;
    return data;
}

static char *find_token(char *p, const char *token) {
    return strstr(p, token);
}

static int cmp_sort_item(const void *a, const void *b) {
    const sort_item_t *ia = (const sort_item_t *)a;
    const sort_item_t *ib = (const sort_item_t *)b;
    if (ia->key != ib->key) return (ia->key > ib->key) - (ia->key < ib->key);
    return (ia->index > ib->index) - (ia->index < ib->index);
}

static uint32_t quant_bits(int16_t value, unsigned bits) {
    int32_t shifted = (int32_t)value + RINHA_SCALE;
    if (shifted < 0) shifted = 0;
    if (shifted > RINHA_SCALE * 2) shifted = RINHA_SCALE * 2;
    uint32_t max = (1U << bits) - 1U;
    return (uint32_t)(((uint64_t)(uint32_t)shifted * max) / (uint32_t)(RINHA_SCALE * 2));
}

static uint32_t projection_key(const int16_t *v) {
    uint32_t prefix = 0;
    /* MXLange-inspired grouping: first split by last-tx, online/card,
       unknown merchant, MCC risk, then amount/count profile signals. */
    prefix |= (uint32_t)(v[5] < 0) << 31;
    prefix |= (uint32_t)(v[9] > 0) << 30;
    prefix |= (uint32_t)(v[10] > 0) << 29;
    prefix |= (uint32_t)(v[11] > 0) << 28;
    prefix |= (uint32_t)(v[12] > (RINHA_SCALE / 2)) << 27;

    const int dims[7] = {2, 8, 12, 5, 7, 0, 13};
    uint32_t qs[7];
    for (int i = 0; i < 7; ++i) qs[i] = quant_bits(v[dims[i]], 4);
    uint32_t key = 0;
    for (int bit = 3; bit >= 0; --bit) {
        for (int i = 0; i < 7; ++i) {
            key = (key << 1) | ((qs[i] >> bit) & 1U);
        }
    }
    return prefix | (key >> 1);
}

/* Runtime search.c has the same profile bucket/key logic. Keep both copies
   identical because the binary index stores only the resulting table. */
static uint32_t positive_bucket(int16_t value, uint32_t buckets) {
    if (value <= 0) return 0;
    uint32_t bucket = (uint32_t)(((int64_t)value * (int64_t)buckets) / (RINHA_SCALE + 1));
    return bucket >= buckets ? buckets - 1U : bucket;
}

static uint32_t profile_key(const int16_t *v) {
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

static int write_profile_fastpath(FILE *out, const int16_t *vectors, const uint8_t *labels, uint32_t row) {
    uint16_t *counts = (uint16_t *)calloc(RINHA_PROFILE_FASTPATH_COUNT, sizeof(*counts));
    uint8_t *masks = (uint8_t *)calloc(RINHA_PROFILE_FASTPATH_COUNT, sizeof(*masks));
    if (counts == NULL || masks == NULL) {
        free(masks);
        free(counts);
        return 0;
    }
    for (uint32_t i = 0; i < row; ++i) {
        uint32_t key = profile_key(vectors + (size_t)i * RINHA_DIMS);
        if (counts[key] != UINT16_MAX) ++counts[key];
        masks[key] |= labels[i] != 0 ? RINHA_FASTPATH_FRAUD_MASK : RINHA_FASTPATH_LEGIT_MASK;
    }
    int ok = fwrite(counts, sizeof(*counts), RINHA_PROFILE_FASTPATH_COUNT, out) == RINHA_PROFILE_FASTPATH_COUNT &&
             fwrite(masks, sizeof(*masks), RINHA_PROFILE_FASTPATH_COUNT, out) == RINHA_PROFILE_FASTPATH_COUNT;
    free(masks);
    free(counts);
    return ok;
}


/* These specs are mirrored by reference_fastpath{1,2}_key() in search.c. */
static const uint8_t k_reference_fastpath1_features[] = {0, 7, 10, 1, 9, 11, 12, 3};
static const uint8_t k_reference_fastpath1_bits[] = {4, 3, 6, 1, 3, 4, 1, 2};
static const uint8_t k_reference_fastpath2_features[] = {5, 13, 6, 1, 12};
static const uint8_t k_reference_fastpath2_bits[] = {4, 4, 4, 4, 4};

typedef struct {
    const uint8_t *features;
    const uint8_t *bits;
    uint32_t len;
    uint32_t slots;
    uint32_t edge_count;
    uint32_t legit_min_count;
    uint32_t fraud_min_count;
} reference_fastpath_spec_t;

static int cmp_i16(const void *a, const void *b) {
    int16_t av = *(const int16_t *)a;
    int16_t bv = *(const int16_t *)b;
    return (av > bv) - (av < bv);
}

static uint32_t reference_fastpath_bin(int16_t value, const int16_t *edges, uint32_t bins) {
    for (uint32_t bin = 0; bin + 1U < bins; ++bin) {
        if (value < edges[bin]) return bin;
    }
    return bins - 1U;
}

static uint32_t reference_fastpath_key(const int16_t *v, const reference_fastpath_spec_t *spec, const int16_t *edges) {
    uint32_t key = 0;
    uint32_t shift = 0;
    uint32_t edge_offset = 0;
    for (uint32_t i = 0; i < spec->len; ++i) {
        uint32_t bins = 1U << spec->bits[i];
        uint32_t bin = reference_fastpath_bin(v[spec->features[i]], edges + edge_offset, bins);
        key |= bin << shift;
        shift += spec->bits[i];
        edge_offset += bins;
    }
    return key;
}

static int fill_reference_fastpath_edges(const int16_t *vectors, uint32_t row, const reference_fastpath_spec_t *spec, int16_t *edges) {
    int16_t *column = (int16_t *)malloc((size_t)row * sizeof(*column));
    if (column == NULL) return 0;
    uint32_t edge_offset = 0;
    for (uint32_t fi = 0; fi < spec->len; ++fi) {
        uint8_t feature = spec->features[fi];
        for (uint32_t r = 0; r < row; ++r) column[r] = vectors[(size_t)r * RINHA_DIMS + feature];
        qsort(column, row, sizeof(*column), cmp_i16);
        uint32_t bins = 1U << spec->bits[fi];
        for (uint32_t bin = 0; bin + 1U < bins; ++bin) {
            uint32_t quantile = (uint32_t)(((uint64_t)(bin + 1U) * row) / bins);
            if (quantile >= row) quantile = row - 1U;
            edges[edge_offset + bin] = column[quantile];
        }
        edges[edge_offset + bins - 1U] = INT16_MAX;
        edge_offset += bins;
    }
    free(column);
    return 1;
}

static int write_one_reference_fastpath(FILE *out, const int16_t *vectors, const uint8_t *labels, uint32_t row, const reference_fastpath_spec_t *spec) {
    int16_t *edges = (int16_t *)malloc((size_t)spec->edge_count * sizeof(*edges));
    uint16_t *counts = (uint16_t *)calloc((size_t)spec->slots, sizeof(*counts));
    uint8_t *masks = (uint8_t *)calloc((size_t)spec->slots, sizeof(*masks));
    uint8_t *table = (uint8_t *)calloc((size_t)spec->slots, sizeof(*table));
    if (edges == NULL || counts == NULL || masks == NULL || table == NULL || !fill_reference_fastpath_edges(vectors, row, spec, edges)) {
        free(table); free(masks); free(counts); free(edges);
        return 0;
    }
    for (uint32_t i = 0; i < row; ++i) {
        uint32_t key = reference_fastpath_key(vectors + (size_t)i * RINHA_DIMS, spec, edges);
        if (counts[key] != UINT16_MAX) ++counts[key];
        masks[key] |= labels[i] != 0 ? RINHA_FASTPATH_FRAUD_MASK : RINHA_FASTPATH_LEGIT_MASK;
    }
    for (uint32_t key = 0; key < spec->slots; ++key) {
        if (masks[key] == RINHA_FASTPATH_LEGIT_MASK && counts[key] >= spec->legit_min_count) table[key] = RINHA_FASTPATH_LEGIT_MASK;
        else if (masks[key] == RINHA_FASTPATH_FRAUD_MASK && counts[key] >= spec->fraud_min_count) table[key] = RINHA_FASTPATH_FRAUD_MASK;
    }
    int ok = fwrite(edges, sizeof(*edges), (size_t)spec->edge_count, out) == (size_t)spec->edge_count &&
             fwrite(table, sizeof(*table), (size_t)spec->slots, out) == (size_t)spec->slots;
    free(table); free(masks); free(counts); free(edges);
    return ok;
}

static int write_reference_fastpaths(FILE *out, const int16_t *vectors, const uint8_t *labels, uint32_t row) {
    reference_fastpath_spec_t spec1 = {k_reference_fastpath1_features, k_reference_fastpath1_bits, 8U, RINHA_REFERENCE_FASTPATH1_SLOTS, RINHA_REFERENCE_FASTPATH1_EDGES, env_u32("BUCKET_REFERENCE_FASTPATH1_LEGIT_MIN_COUNT", 100U, 1U, UINT16_MAX), env_u32("BUCKET_REFERENCE_FASTPATH1_FRAUD_MIN_COUNT", 6000U, 1U, UINT16_MAX)};
    reference_fastpath_spec_t spec2 = {k_reference_fastpath2_features, k_reference_fastpath2_bits, 5U, RINHA_REFERENCE_FASTPATH2_SLOTS, RINHA_REFERENCE_FASTPATH2_EDGES, env_u32("BUCKET_REFERENCE_FASTPATH2_LEGIT_MIN_COUNT", 150U, 1U, UINT16_MAX), env_u32("BUCKET_REFERENCE_FASTPATH2_FRAUD_MIN_COUNT", 6000U, 1U, UINT16_MAX)};
    return write_one_reference_fastpath(out, vectors, labels, row, &spec1) && write_one_reference_fastpath(out, vectors, labels, row, &spec2);
}

static int16_t mean_i16(int64_t sum, uint32_t len) {
    if (len == 0) return 0;
    int64_t half = (int64_t)len / 2;
    return (int16_t)(sum >= 0 ? (sum + half) / (int64_t)len : (sum - half) / (int64_t)len);
}

static int write_flat(FILE *out, const int16_t *vectors, const uint8_t *labels, uint32_t row) {
    rinha_index_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = RINHA_INDEX_MAGIC;
    header.version = RINHA_INDEX_VERSION;
    header.count = row;
    header.dims = RINHA_DIMS;
    header.scale = RINHA_SCALE;
    header.layout = RINHA_INDEX_LAYOUT_FLAT;
    return fwrite(&header, sizeof(header), 1, out) == 1 &&
           fwrite(vectors, sizeof(int16_t), (size_t)row * RINHA_DIMS, out) == (size_t)row * RINHA_DIMS &&
           fwrite(labels, sizeof(uint8_t), (size_t)row, out) == (size_t)row;
}

static int write_ivf_block8(FILE *out, const int16_t *vectors, const uint8_t *labels, uint32_t row, uint32_t list_count) {
    if (list_count == 0 || list_count > row) list_count = row;
    sort_item_t *items = (sort_item_t *)malloc((size_t)row * sizeof(*items));
    uint32_t *offsets = (uint32_t *)malloc(((size_t)list_count + 1U) * sizeof(*offsets));
    uint32_t *slot_offsets = (uint32_t *)malloc(((size_t)list_count + 1U) * sizeof(*slot_offsets));
    int16_t *centroids = (int16_t *)calloc((size_t)list_count * RINHA_DIMS, sizeof(*centroids));
    int16_t *bounds_min = (int16_t *)malloc((size_t)list_count * RINHA_DIMS * sizeof(*bounds_min));
    int16_t *bounds_max = (int16_t *)malloc((size_t)list_count * RINHA_DIMS * sizeof(*bounds_max));
    int64_t *sums = (int64_t *)calloc((size_t)list_count * RINHA_DIMS, sizeof(*sums));
    if (items == NULL || offsets == NULL || slot_offsets == NULL || centroids == NULL || bounds_min == NULL || bounds_max == NULL || sums == NULL) {
        free(sums);
        free(bounds_max);
        free(bounds_min);
        free(centroids);
        free(slot_offsets);
        free(offsets);
        free(items);
        return 0;
    }
    for (uint32_t i = 0; i < row; ++i) {
        items[i].key = projection_key(vectors + (size_t)i * RINHA_DIMS);
        items[i].index = i;
    }
    qsort(items, row, sizeof(*items), cmp_sort_item);
    for (uint32_t list = 0; list <= list_count; ++list) {
        offsets[list] = (uint32_t)(((uint64_t)list * row) / list_count);
    }
    slot_offsets[0] = 0;
    for (uint32_t list = 0; list < list_count; ++list) {
        uint32_t len = offsets[list + 1U] - offsets[list];
        uint32_t padded = (len + 7U) & ~7U;
        slot_offsets[list + 1U] = slot_offsets[list] + padded;
    }
    uint32_t physical_count = slot_offsets[list_count];
    int16_t *block_vectors = (int16_t *)calloc((size_t)physical_count * RINHA_DIMS, sizeof(*block_vectors));
    uint8_t *block_labels = (uint8_t *)calloc((size_t)physical_count, sizeof(*block_labels));
    if (block_vectors == NULL || block_labels == NULL) {
        free(block_labels);
        free(block_vectors);
        free(sums);
        free(bounds_max);
        free(bounds_min);
        free(centroids);
        free(slot_offsets);
        free(offsets);
        free(items);
        return 0;
    }
    for (uint32_t list = 0; list < list_count; ++list) {
        for (int d = 0; d < RINHA_DIMS; ++d) {
            bounds_min[(size_t)list * RINHA_DIMS + (size_t)d] = INT16_MAX;
            bounds_max[(size_t)list * RINHA_DIMS + (size_t)d] = INT16_MIN;
        }
        for (uint32_t pos = offsets[list]; pos < offsets[list + 1U]; ++pos) {
            uint32_t rel = pos - offsets[list];
            uint32_t slot = slot_offsets[list] + (rel & ~7U);
            uint32_t lane = rel & 7U;
            const int16_t *v = vectors + (size_t)items[pos].index * RINHA_DIMS;
            for (int d = 0; d < RINHA_DIMS; ++d) {
                size_t idx = (size_t)list * RINHA_DIMS + (size_t)d;
                sums[idx] += v[d];
                if (v[d] < bounds_min[idx]) bounds_min[idx] = v[d];
                if (v[d] > bounds_max[idx]) bounds_max[idx] = v[d];
                block_vectors[(size_t)slot * RINHA_DIMS + (size_t)d * 8U + lane] = v[d];
            }
            block_labels[slot + lane] = labels[items[pos].index];
        }
        uint32_t len = offsets[list + 1U] - offsets[list];
        for (int d = 0; d < RINHA_DIMS; ++d) centroids[(size_t)list * RINHA_DIMS + (size_t)d] = mean_i16(sums[(size_t)list * RINHA_DIMS + (size_t)d], len);
    }

    rinha_index_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = RINHA_INDEX_MAGIC;
    header.version = RINHA_INDEX_VERSION;
    header.count = row;
    header.dims = RINHA_DIMS;
    header.scale = RINHA_SCALE;
    header.layout = RINHA_INDEX_LAYOUT_IVF_BLOCK8;
    header.list_count = list_count;
    header.default_nprobe = RINHA_INDEX_DEFAULT_NPROBE;
    header.reserved[0] = RINHA_INDEX_FLAG_BOUNDS | RINHA_INDEX_FLAG_PROFILE_FASTPATH | RINHA_INDEX_FLAG_REFERENCE_FASTPATH;
    header.reserved[1] = physical_count;

    int ok = fwrite(&header, sizeof(header), 1, out) == 1 &&
              fwrite(offsets, sizeof(uint32_t), (size_t)list_count + 1U, out) == (size_t)list_count + 1U &&
              fwrite(slot_offsets, sizeof(uint32_t), (size_t)list_count + 1U, out) == (size_t)list_count + 1U &&
              fwrite(centroids, sizeof(int16_t), (size_t)list_count * RINHA_DIMS, out) == (size_t)list_count * RINHA_DIMS &&
              fwrite(bounds_min, sizeof(int16_t), (size_t)list_count * RINHA_DIMS, out) == (size_t)list_count * RINHA_DIMS &&
              fwrite(bounds_max, sizeof(int16_t), (size_t)list_count * RINHA_DIMS, out) == (size_t)list_count * RINHA_DIMS &&
              fwrite(block_vectors, sizeof(int16_t), (size_t)physical_count * RINHA_DIMS, out) == (size_t)physical_count * RINHA_DIMS &&
              fwrite(block_labels, sizeof(uint8_t), (size_t)physical_count, out) == (size_t)physical_count &&
              write_profile_fastpath(out, vectors, labels, row) &&
              write_reference_fastpaths(out, vectors, labels, row);
    free(block_labels);
    free(block_vectors);
    free(sums);
    free(bounds_max);
    free(bounds_min);
    free(centroids);
    free(slot_offsets);
    free(offsets);
    free(items);
    return ok;
}

static uint64_t dist_to_centroid_bounded(const int16_t *v, const int16_t *centroids, uint32_t list, uint64_t limit) {
    const int16_t *c = centroids + (size_t)list * RINHA_DIMS;
    uint64_t sum = 0;
    for (int d = 0; d < RINHA_DIMS; ++d) {
        sum += rinha_sqdiff_i16(v[d], c[d]);
        if (sum >= limit) return sum;
    }
    return sum;
}

static uint32_t nearest_centroid_window(const int16_t *v, const int16_t *centroids, uint32_t list_count, uint32_t center, uint32_t window) {
    uint32_t start = center > window ? center - window : 0U;
    uint32_t end = center + window + 1U;
    if (end > list_count) end = list_count;
    uint32_t best = start;
    uint64_t best_dist = UINT64_MAX;
    for (uint32_t list = start; list < end; ++list) {
        uint64_t dist = dist_to_centroid_bounded(v, centroids, list, best_dist);
        if (dist < best_dist) {
            best_dist = dist;
            best = list;
        }
    }
    return best;
}

static int write_ivf_kmeans_block16(FILE *out, const int16_t *vectors, const uint8_t *labels, uint32_t row, uint32_t list_count) {
    if (list_count == 0 || list_count > row) list_count = row;
    uint32_t train_max = env_u32("RINHA_KMEANS_TRAIN", 131072U, 1U, row);
    uint32_t iters = env_u32("RINHA_KMEANS_ITERS", 3U, 0U, 32U);
    uint32_t window = env_u32("RINHA_KMEANS_WINDOW", 64U, 0U, list_count);

    sort_item_t *items = (sort_item_t *)malloc((size_t)row * sizeof(*items));
    uint32_t *rank_bucket = (uint32_t *)malloc((size_t)row * sizeof(*rank_bucket));
    uint32_t *assignment = (uint32_t *)malloc((size_t)row * sizeof(*assignment));
    uint32_t *counts = (uint32_t *)calloc((size_t)list_count, sizeof(*counts));
    uint32_t *offsets = (uint32_t *)malloc(((size_t)list_count + 1U) * sizeof(*offsets));
    uint32_t *cursors = (uint32_t *)malloc((size_t)list_count * sizeof(*cursors));
    int16_t *centroids = (int16_t *)calloc((size_t)list_count * RINHA_DIMS, sizeof(*centroids));
    int16_t *centroids_t = (int16_t *)malloc((size_t)RINHA_DIMS * list_count * sizeof(*centroids_t));
    int64_t *sums = (int64_t *)calloc((size_t)list_count * RINHA_DIMS, sizeof(*sums));
    uint32_t *sum_counts = (uint32_t *)calloc((size_t)list_count, sizeof(*sum_counts));
    if (items == NULL || rank_bucket == NULL || assignment == NULL || counts == NULL || offsets == NULL || cursors == NULL || centroids == NULL || centroids_t == NULL || sums == NULL || sum_counts == NULL) goto fail_early;

    for (uint32_t i = 0; i < row; ++i) {
        items[i].key = projection_key(vectors + (size_t)i * RINHA_DIMS);
        items[i].index = i;
    }
    qsort(items, row, sizeof(*items), cmp_sort_item);

    for (uint32_t list = 0; list < list_count; ++list) {
        uint32_t start = (uint32_t)(((uint64_t)list * row) / list_count);
        uint32_t end = (uint32_t)(((uint64_t)(list + 1U) * row) / list_count);
        if (end <= start) end = start + 1U;
        for (uint32_t pos = start; pos < end && pos < row; ++pos) {
            const int16_t *v = vectors + (size_t)items[pos].index * RINHA_DIMS;
            for (int d = 0; d < RINHA_DIMS; ++d) sums[(size_t)list * RINHA_DIMS + (size_t)d] += v[d];
            ++sum_counts[list];
            rank_bucket[items[pos].index] = list;
        }
    }
    for (uint32_t list = 0; list < list_count; ++list) {
        uint32_t sample_pos = (uint32_t)(((uint64_t)list * row + row / 2U) / list_count);
        if (sample_pos >= row) sample_pos = row - 1U;
        const int16_t *fallback = vectors + (size_t)items[sample_pos].index * RINHA_DIMS;
        for (int d = 0; d < RINHA_DIMS; ++d) centroids[(size_t)list * RINHA_DIMS + (size_t)d] = sum_counts[list] != 0 ? mean_i16(sums[(size_t)list * RINHA_DIMS + (size_t)d], sum_counts[list]) : fallback[d];
    }

    uint32_t train_count = train_max < row ? train_max : row;
    uint32_t stride = row / train_count;
    if (stride == 0) stride = 1U;
    for (uint32_t iter = 0; iter < iters; ++iter) {
        memset(sums, 0, (size_t)list_count * RINHA_DIMS * sizeof(*sums));
        memset(sum_counts, 0, (size_t)list_count * sizeof(*sum_counts));
        for (uint32_t t = 0; t < train_count; ++t) {
            uint32_t pos = (uint32_t)((uint64_t)t * stride + stride / 2U);
            if (pos >= row) pos = row - 1U;
            uint32_t idx = items[pos].index;
            const int16_t *v = vectors + (size_t)idx * RINHA_DIMS;
            uint32_t best = nearest_centroid_window(v, centroids, list_count, rank_bucket[idx], window);
            for (int d = 0; d < RINHA_DIMS; ++d) sums[(size_t)best * RINHA_DIMS + (size_t)d] += v[d];
            ++sum_counts[best];
        }
        for (uint32_t list = 0; list < list_count; ++list) {
            if (sum_counts[list] == 0) continue;
            for (int d = 0; d < RINHA_DIMS; ++d) centroids[(size_t)list * RINHA_DIMS + (size_t)d] = mean_i16(sums[(size_t)list * RINHA_DIMS + (size_t)d], sum_counts[list]);
        }
    }

    /* Keep each list as 16-lane dimension-major blocks for the runtime AVX2
       scanner: block[d * 16 + lane]. Empty padded lanes are harmless legit
       sentinels that sit far from normal normalized values. */
    for (uint32_t i = 0; i < row; ++i) {
        uint32_t best = nearest_centroid_window(vectors + (size_t)i * RINHA_DIMS, centroids, list_count, rank_bucket[i], window);
        assignment[i] = best;
        ++counts[best];
    }
    offsets[0] = 0;
    for (uint32_t list = 0; list < list_count; ++list) offsets[list + 1U] = offsets[list] + ((counts[list] + 15U) >> 4);
    uint32_t total_blocks = offsets[list_count];
    uint32_t physical_count = total_blocks * 16U;
    int16_t *block_vectors = (int16_t *)calloc((size_t)total_blocks * RINHA_DIMS * 16U, sizeof(*block_vectors));
    uint8_t *block_labels = (uint8_t *)calloc((size_t)physical_count, sizeof(*block_labels));
    if (block_vectors == NULL || block_labels == NULL) {
        free(block_labels);
        free(block_vectors);
        goto fail_early;
    }

    memcpy(cursors, offsets, (size_t)list_count * sizeof(*cursors));
    for (uint32_t i = 0; i < row; ++i) {
        uint32_t list = assignment[i];
        uint32_t rel = cursors[list]++ - offsets[list];
        uint32_t block = offsets[list] + (rel >> 4);
        uint32_t lane = rel & 15U;
        const int16_t *v = vectors + (size_t)i * RINHA_DIMS;
        for (int d = 0; d < RINHA_DIMS; ++d) block_vectors[(size_t)block * RINHA_DIMS * 16U + (size_t)d * 16U + lane] = v[d];
        block_labels[(size_t)block * 16U + lane] = labels[i];
    }
    for (uint32_t list = 0; list < list_count; ++list) {
        if (counts[list] == 0 || (counts[list] & 15U) == 0U) continue;
        uint32_t block = offsets[list] + (counts[list] >> 4);
        uint32_t first_lane = counts[list] & 15U;
        for (uint32_t lane = first_lane; lane < 16U; ++lane) {
            for (int d = 0; d < RINHA_DIMS; ++d) block_vectors[(size_t)block * RINHA_DIMS * 16U + (size_t)d * 16U + lane] = INT16_MIN;
            block_labels[(size_t)block * 16U + lane] = 0U;
        }
    }
    /* Transposed centroids let search score eight list centers at once by
       loading consecutive centers for a single dimension. */
    for (int d = 0; d < RINHA_DIMS; ++d) {
        for (uint32_t list = 0; list < list_count; ++list) centroids_t[(size_t)d * list_count + list] = centroids[(size_t)list * RINHA_DIMS + (size_t)d];
    }

    rinha_index_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = RINHA_INDEX_MAGIC;
    header.version = RINHA_INDEX_VERSION;
    header.count = row;
    header.dims = RINHA_DIMS;
    header.scale = RINHA_SCALE;
    header.layout = RINHA_INDEX_LAYOUT_IVF_KMEANS_BLOCK16;
    header.list_count = list_count;
    header.default_nprobe = RINHA_INDEX_DEFAULT_NPROBE;
    header.reserved[0] = RINHA_INDEX_FLAG_TRANSPOSED_CENTROIDS;
    header.reserved[1] = total_blocks;

    int ok = fwrite(&header, sizeof(header), 1, out) == 1 &&
             fwrite(offsets, sizeof(uint32_t), (size_t)list_count + 1U, out) == (size_t)list_count + 1U &&
             fwrite(centroids_t, sizeof(int16_t), (size_t)RINHA_DIMS * list_count, out) == (size_t)RINHA_DIMS * list_count &&
             fwrite(block_labels, sizeof(uint8_t), (size_t)physical_count, out) == (size_t)physical_count &&
             fwrite(block_vectors, sizeof(int16_t), (size_t)total_blocks * RINHA_DIMS * 16U, out) == (size_t)total_blocks * RINHA_DIMS * 16U;
    free(block_labels);
    free(block_vectors);
    free(sum_counts);
    free(sums);
    free(centroids_t);
    free(centroids);
    free(cursors);
    free(offsets);
    free(counts);
    free(assignment);
    free(rank_bucket);
    free(items);
    return ok;

fail_early:
    free(sum_counts);
    free(sums);
    free(centroids_t);
    free(centroids);
    free(cursors);
    free(offsets);
    free(counts);
    free(assignment);
    free(rank_bucket);
    free(items);
    return 0;
}

typedef struct {
    uint32_t *roots;
    rinha_kd_node_disk_t *nodes;
    uint32_t node_count;
    uint32_t node_cap;
    int16_t *block_vectors;
    uint8_t *block_labels;
    uint32_t physical_count;
    uint32_t physical_cap;
    const int16_t *vectors;
    const uint8_t *labels;
    uint32_t *indices;
    uint32_t leaf_size;
} kd_builder_t;

static uint32_t semantic_partition_key(const int16_t *v) {
    uint32_t key = 0;
    key |= (uint32_t)(v[5] < 0) << 7;
    key |= (uint32_t)(v[9] > 0) << 6;
    key |= (uint32_t)(v[10] > 0) << 5;
    key |= (uint32_t)(v[11] > 0) << 4;
    key |= (positive_bucket(v[2], 4U) & 3U) << 2;
    key |= (positive_bucket(v[7], 4U) & 3U);
    return key;
}

static const int16_t *g_kd_sort_vectors;
static uint8_t g_kd_sort_axis;
static int cmp_kd_axis(const void *a, const void *b) {
    uint32_t ia = *(const uint32_t *)a;
    uint32_t ib = *(const uint32_t *)b;
    int16_t av = g_kd_sort_vectors[(size_t)ia * RINHA_DIMS + g_kd_sort_axis];
    int16_t bv = g_kd_sort_vectors[(size_t)ib * RINHA_DIMS + g_kd_sort_axis];
    if (av != bv) return (av > bv) - (av < bv);
    return (ia > ib) - (ia < ib);
}

static int kd_reserve_nodes(kd_builder_t *b, uint32_t extra) {
    if (b->node_count + extra <= b->node_cap) return 1;
    uint32_t cap = b->node_cap ? b->node_cap * 2U : 1024U;
    while (cap < b->node_count + extra) cap *= 2U;
    rinha_kd_node_disk_t *next = (rinha_kd_node_disk_t *)realloc(b->nodes, (size_t)cap * sizeof(*next));
    if (next == NULL) return 0;
    b->nodes = next;
    b->node_cap = cap;
    return 1;
}

static int kd_reserve_physical(kd_builder_t *b, uint32_t extra) {
    if (b->physical_count + extra <= b->physical_cap) return 1;
    uint32_t cap = b->physical_cap ? b->physical_cap * 2U : 8192U;
    while (cap < b->physical_count + extra) cap *= 2U;
    int16_t *nv = (int16_t *)realloc(b->block_vectors, (size_t)cap * RINHA_DIMS * sizeof(*nv));
    if (nv == NULL) return 0;
    uint8_t *nl = (uint8_t *)realloc(b->block_labels, (size_t)cap * sizeof(*nl));
    if (nl == NULL) return 0;
    b->block_vectors = nv;
    b->block_labels = nl;
    b->physical_cap = cap;
    return 1;
}

static void kd_bounds(kd_builder_t *b, uint32_t begin, uint32_t end, int16_t minv[RINHA_DIMS], int16_t maxv[RINHA_DIMS]) {
    for (int d = 0; d < RINHA_DIMS; ++d) { minv[d] = INT16_MAX; maxv[d] = INT16_MIN; }
    for (uint32_t p = begin; p < end; ++p) {
        const int16_t *v = b->vectors + (size_t)b->indices[p] * RINHA_DIMS;
        for (int d = 0; d < RINHA_DIMS; ++d) { if (v[d] < minv[d]) minv[d] = v[d]; if (v[d] > maxv[d]) maxv[d] = v[d]; }
    }
}

static uint32_t kd_build_rec(kd_builder_t *b, uint32_t begin, uint32_t end) {
    if (!kd_reserve_nodes(b, 1U)) return UINT32_MAX;
    uint32_t node_index = b->node_count++;
    rinha_kd_node_disk_t *n = &b->nodes[node_index];
    memset(n, 0, sizeof(*n));
    n->left = n->right = UINT32_MAX;
    kd_bounds(b, begin, end, n->min, n->max);
    uint32_t len = end - begin;
    if (len <= b->leaf_size) {
        uint32_t padded = (len + 7U) & ~7U;
        if (!kd_reserve_physical(b, padded)) return UINT32_MAX;
        n->leaf = 1U;
        n->start = b->physical_count;
        n->count = len;
        /* KD leaves reuse the block8 SoA contract consumed by search.c. */
        for (uint32_t rel = 0; rel < padded; ++rel) {
            uint32_t slot = b->physical_count + (rel & ~7U);
            uint32_t lane = rel & 7U;
            if (rel < len) {
                uint32_t src = b->indices[begin + rel];
                const int16_t *v = b->vectors + (size_t)src * RINHA_DIMS;
                for (int d = 0; d < RINHA_DIMS; ++d) b->block_vectors[(size_t)slot * RINHA_DIMS + (size_t)d * 8U + lane] = v[d];
                b->block_labels[slot + lane] = b->labels[src];
            } else {
                for (int d = 0; d < RINHA_DIMS; ++d) b->block_vectors[(size_t)slot * RINHA_DIMS + (size_t)d * 8U + lane] = INT16_MIN;
                b->block_labels[slot + lane] = 0U;
            }
        }
        b->physical_count += padded;
        return node_index;
    }
    uint8_t axis = 0;
    int32_t widest = -1;
    for (uint8_t d = 0; d < RINHA_DIMS; ++d) {
        int32_t width = (int32_t)n->max[d] - (int32_t)n->min[d];
        if (width > widest) { widest = width; axis = d; }
    }
    n->axis = axis;
    g_kd_sort_vectors = b->vectors;
    g_kd_sort_axis = axis;
    qsort(b->indices + begin, len, sizeof(uint32_t), cmp_kd_axis);
    uint32_t mid = begin + len / 2U;
    uint32_t left = kd_build_rec(b, begin, mid);
    uint32_t right = kd_build_rec(b, mid, end);
    if (left == UINT32_MAX || right == UINT32_MAX) return UINT32_MAX;
    b->nodes[node_index].left = left;
    b->nodes[node_index].right = right;
    return node_index;
}

static int write_kd_tree(FILE *out, const int16_t *vectors, const uint8_t *labels, uint32_t row) {
    const uint32_t parts = 256U;
    uint32_t *counts = (uint32_t *)calloc(parts, sizeof(*counts));
    uint32_t *offsets = (uint32_t *)malloc((parts + 1U) * sizeof(*offsets));
    uint32_t *cursor = (uint32_t *)malloc(parts * sizeof(*cursor));
    uint32_t *indices = (uint32_t *)malloc((size_t)row * sizeof(*indices));
    if (counts == NULL || offsets == NULL || cursor == NULL || indices == NULL) { free(indices); free(cursor); free(offsets); free(counts); return 0; }
    for (uint32_t i = 0; i < row; ++i) ++counts[semantic_partition_key(vectors + (size_t)i * RINHA_DIMS)];
    offsets[0] = 0;
    for (uint32_t p = 0; p < parts; ++p) offsets[p + 1U] = offsets[p] + counts[p];
    memcpy(cursor, offsets, parts * sizeof(*cursor));
    for (uint32_t i = 0; i < row; ++i) indices[cursor[semantic_partition_key(vectors + (size_t)i * RINHA_DIMS)]++] = i;
    kd_builder_t b;
    memset(&b, 0, sizeof(b));
    b.roots = (uint32_t *)malloc((parts + 1U) * sizeof(uint32_t));
    b.vectors = vectors; b.labels = labels; b.indices = indices;
    b.leaf_size = env_u32("RINHA_KD_LEAF_SIZE", 32U, 8U, 512U);
    if (b.roots == NULL) { free(indices); free(cursor); free(offsets); free(counts); return 0; }
    for (uint32_t p = 0; p < parts; ++p) {
        b.roots[p] = offsets[p] == offsets[p + 1U] ? UINT32_MAX : kd_build_rec(&b, offsets[p], offsets[p + 1U]);
        if (b.roots[p] == UINT32_MAX && offsets[p] != offsets[p + 1U]) goto fail;
    }
    uint32_t leaf_count = 0;
    for (uint32_t ni = 0; ni < b.node_count; ++ni) if (b.nodes[ni].leaf) ++leaf_count;
    b.roots[parts] = b.node_count;
    rinha_index_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = RINHA_INDEX_MAGIC; header.version = RINHA_INDEX_VERSION; header.count = row; header.dims = RINHA_DIMS; header.scale = RINHA_SCALE;
    header.layout = RINHA_INDEX_LAYOUT_KD_TREE_BLOCK8; header.list_count = parts; header.default_nprobe = parts;
    header.reserved[0] = RINHA_INDEX_FLAG_PROFILE_FASTPATH | RINHA_INDEX_FLAG_REFERENCE_FASTPATH;
    header.reserved[1] = b.node_count; header.reserved[2] = leaf_count; header.reserved[3] = b.physical_count;
    int ok = fwrite(&header, sizeof(header), 1, out) == 1 &&
             fwrite(b.roots, sizeof(uint32_t), parts + 1U, out) == parts + 1U &&
             fwrite(b.nodes, sizeof(*b.nodes), b.node_count, out) == b.node_count &&
             fwrite(b.block_vectors, sizeof(int16_t), (size_t)b.physical_count * RINHA_DIMS, out) == (size_t)b.physical_count * RINHA_DIMS &&
             fwrite(b.block_labels, sizeof(uint8_t), b.physical_count, out) == b.physical_count &&
             write_profile_fastpath(out, vectors, labels, row) &&
             write_reference_fastpaths(out, vectors, labels, row);
    free(b.block_labels); free(b.block_vectors); free(b.nodes); free(b.roots); free(indices); free(cursor); free(offsets); free(counts);
    return ok;
fail:
    free(b.block_labels); free(b.block_vectors); free(b.nodes); free(b.roots); free(indices); free(cursor); free(offsets); free(counts);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: build-index references.json.gz index.bin [ivf-list-count]\n");
        return 2;
    }
    uint32_t ivf_lists = argc >= 4 ? (uint32_t)strtoul(argv[3], NULL, 10) : 0U;

    size_t len = 0;
    char *raw = read_gzip(argv[1], &len);
    if (raw == NULL) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }

    uint32_t count = 0;
    for (char *p = raw; (p = find_token(p, "\"vector\"")) != NULL; ++p) ++count;
    if (count == 0) {
        free(raw);
        return 1;
    }

    size_t vector_count = (size_t)count * RINHA_DIMS;
    int16_t *vectors = (int16_t *)malloc(vector_count * sizeof(int16_t));
    uint8_t *labels = (uint8_t *)malloc((size_t)count);
    if (vectors == NULL || labels == NULL) {
        free(vectors);
        free(labels);
        free(raw);
        return 1;
    }

    char *p = raw;
    uint32_t row = 0;
    while ((p = find_token(p, "\"vector\"")) != NULL && row < count) {
        p = strchr(p, '[');
        if (p == NULL) break;
        ++p;
        for (int d = 0; d < RINHA_DIMS; ++d) {
            while (*p == ',' || *p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
            char *end = NULL;
            double value = strtod(p, &end);
            if (end == p) {
                free(vectors);
                free(labels);
                free(raw);
                return 1;
            }
            vectors[(size_t)row * RINHA_DIMS + (size_t)d] = rinha_qround(value);
            p = end;
        }
        char *label = find_token(p, "\"label\"");
        if (label == NULL) break;
        char *quote = strchr(strchr(label, ':') + 1, '"');
        if (quote == NULL) break;
        labels[row] = quote[1] == 'f' ? 1U : 0U;
        p = quote + 1;
        ++row;
    }

    FILE *out = fopen(argv[2], "wb");
    if (out == NULL) {
        free(vectors);
        free(labels);
        free(raw);
        return 1;
    }
    int use_kd = ivf_lists > 0 && env_enabled("RINHA_INDEX_KD_TREE");
    int use_kmeans_block16 = ivf_lists > 0 && !use_kd && env_enabled("RINHA_INDEX_V2");
    int ok = ivf_lists > 0 ? (use_kd ? write_kd_tree(out, vectors, labels, row) : (use_kmeans_block16 ? write_ivf_kmeans_block16(out, vectors, labels, row, ivf_lists) : write_ivf_block8(out, vectors, labels, row, ivf_lists))) : write_flat(out, vectors, labels, row);
    fclose(out);

    fprintf(stderr, "index written: %s rows=%u layout=%s lists=%u\n", argv[2], row, use_kd ? "kd-tree-block8" : (use_kmeans_block16 ? "ivf-kmeans-block16" : (ivf_lists > 0 ? "ivf-block8" : "flat")), use_kd ? 256U : ivf_lists);
    free(vectors);
    free(labels);
    free(raw);
    (void)len;
    return ok && row == count ? 0 : 1;
}
