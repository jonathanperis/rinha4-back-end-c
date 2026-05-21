#ifndef RINHA_INDEX_H
#define RINHA_INDEX_H

#include <stddef.h>
#include <stdint.h>

#include "common/distance.h"

typedef struct {
    size_t size;
    uint32_t count;
    uint32_t physical_count;
    uint32_t layout;
    uint32_t list_count;
    uint32_t nprobe;
    uint32_t repair_nprobe;
    uint32_t repair_min_fraud;
    uint32_t repair_max_fraud;
    uint32_t exact_fallback;
    uint64_t repair_worst_threshold[6];
    int mapped;
    void *raw;
    uint8_t *storage;
    const uint32_t *offsets;
    const uint32_t *slot_offsets;
    const int16_t *centroids;
    const int16_t *bounds_min;
    const int16_t *bounds_max;
    const int16_t *vectors;
    const uint8_t *labels;
    const void *kd_nodes;
    uint32_t kd_node_count;
    uint32_t kd_leaf_count;
    uint64_t kd_early_distance_limit;
    const uint16_t *profile_counts;
    const uint8_t *profile_masks;
    const int16_t *reference_fastpath1_edges;
    const uint8_t *reference_fastpath1;
    const int16_t *reference_fastpath2_edges;
    const uint8_t *reference_fastpath2;
    uint32_t profile_fastpath;
    uint32_t profile_legit_min_count;
    uint32_t profile_fraud_min_count;
    uint32_t reference_fastpath;
    uint32_t reference_fastpath_legit;
    uint32_t reference_fastpath_fraud;
    uint32_t reference_fastpath2_legit;
    uint32_t reference_fastpath2_fraud;
} rinha_index_t;

int rinha_index_load(const char *path, rinha_index_t *index);
void rinha_index_close(rinha_index_t *index);

#endif
