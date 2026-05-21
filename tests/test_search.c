#include "common/index.h"
#include "common/index_format.h"
#include "common/search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_all(FILE *file, const void *data, size_t size) {
    return fwrite(data, 1U, size, file) == size ? 0 : -1;
}

int main(void) {
    int16_t vectors[6][RINHA_DIMS];
    unsigned char labels[6] = {0, 1, 1, 0, 1, 0};
    memset(vectors, 0, sizeof(vectors));
    for (int i = 0; i < 6; ++i) vectors[i][0] = (int16_t)(i * 1000);

    rinha_index_t index;
    memset(&index, 0, sizeof(index));
    index.count = 6;
    index.vectors = &vectors[0][0];
    index.labels = labels;

    int16_t query[RINHA_DIMS];
    memset(query, 0, sizeof(query));
    uint8_t fraud = rinha_search_fraud_count(&index, query);
    if (fraud != 3) {
        fprintf(stderr, "fraud=%u want=3\n", fraud);
        return 1;
    }

    int16_t far[RINHA_DIMS];
    for (int i = 0; i < RINHA_DIMS; ++i) {
        query[i] = 0;
        far[i] = 0;
    }
    far[9] = RINHA_SCALE;
    far[10] = RINHA_SCALE;
    uint64_t exact = rinha_dist_i16(query, far);
    uint64_t bounded = rinha_dist_i16_bounded(query, far, UINT64_MAX);
    if (bounded != exact) {
        fprintf(stderr, "bounded=%llu exact=%llu\n", (unsigned long long)bounded, (unsigned long long)exact);
        return 1;
    }
    bounded = rinha_dist_i16_bounded(query, far, 1);
    if (bounded < 1 || bounded > exact) {
        fprintf(stderr, "bounded abort=%llu exact=%llu\n", (unsigned long long)bounded, (unsigned long long)exact);
        return 1;
    }
    int16_t ivf_vectors[6][RINHA_DIMS];
    int16_t centroids[2][RINHA_DIMS];
    uint32_t offsets[3] = {0, 5, 6};
    unsigned char ivf_labels[6] = {1, 1, 0, 0, 1, 0};
    memset(ivf_vectors, 0, sizeof(ivf_vectors));
    memset(centroids, 0, sizeof(centroids));
    centroids[1][0] = RINHA_SCALE;
    for (int i = 0; i < 5; ++i) ivf_vectors[i][0] = (int16_t)((i + 1) * 10);
    ivf_vectors[5][0] = 0;

    memset(&index, 0, sizeof(index));
    index.count = 6;
    index.layout = RINHA_INDEX_LAYOUT_IVF;
    index.list_count = 2;
    index.nprobe = 1;
    index.repair_nprobe = 2;
    index.repair_min_fraud = 3;
    index.repair_max_fraud = 3;
    index.offsets = offsets;
    index.centroids = &centroids[0][0];
    index.vectors = &ivf_vectors[0][0];
    index.labels = ivf_labels;
    memset(query, 0, sizeof(query));
    fraud = rinha_search_fraud_count(&index, query);
    if (fraud != 2) {
        fprintf(stderr, "repair fraud=%u want=2\n", fraud);
        return 1;
    }

    int16_t fallback_vectors[10][RINHA_DIMS];
    int16_t fallback_centroids[2][RINHA_DIMS];
    int16_t fallback_min[2][RINHA_DIMS];
    int16_t fallback_max[2][RINHA_DIMS];
    uint32_t fallback_offsets[3] = {0, 5, 10};
    unsigned char fallback_labels[10] = {1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
    memset(fallback_vectors, 0, sizeof(fallback_vectors));
    memset(fallback_centroids, 0, sizeof(fallback_centroids));
    memset(fallback_min, 0, sizeof(fallback_min));
    memset(fallback_max, 0, sizeof(fallback_max));
    for (int i = 0; i < 5; ++i) {
        for (int d = 0; d < RINHA_DIMS; ++d) fallback_vectors[i][d] = (int16_t)((i & 1) ? -100 : 100);
    }
    for (int i = 5; i < 10; ++i) fallback_vectors[i][0] = (int16_t)(i - 5);
    for (int d = 0; d < RINHA_DIMS; ++d) {
        fallback_min[0][d] = -100;
        fallback_max[0][d] = 100;
    }
    fallback_max[1][0] = 4;
    fallback_centroids[1][0] = RINHA_SCALE;

    memset(&index, 0, sizeof(index));
    index.count = 10;
    index.layout = RINHA_INDEX_LAYOUT_IVF;
    index.list_count = 2;
    index.nprobe = 1;
    index.repair_nprobe = 1;
    index.repair_min_fraud = 5;
    index.repair_max_fraud = 5;
    index.exact_fallback = 1;
    index.offsets = fallback_offsets;
    index.centroids = &fallback_centroids[0][0];
    index.bounds_min = &fallback_min[0][0];
    index.bounds_max = &fallback_max[0][0];
    index.vectors = &fallback_vectors[0][0];
    index.labels = fallback_labels;
    memset(query, 0, sizeof(query));
    fraud = rinha_search_fraud_count(&index, query);
    if (fraud != 0) {
        fprintf(stderr, "fallback fraud=%u want=0\n", fraud);
        return 1;
    }

    int16_t block_vectors[16 * RINHA_DIMS];
    int16_t block_centroids[2][RINHA_DIMS];
    uint32_t block_offsets[3] = {0, 7, 10};
    uint32_t block_slot_offsets[3] = {0, 8, 16};
    unsigned char block_labels[16];
    memset(block_vectors, 0, sizeof(block_vectors));
    memset(block_centroids, 0, sizeof(block_centroids));
    for (int i = 0; i < 16; ++i) block_labels[i] = 1;
    for (int i = 0; i < 10; ++i) {
        uint32_t slot = i < 7 ? (uint32_t)i : (uint32_t)(8 + (i - 7));
        uint32_t block = slot & ~7U;
        uint32_t lane = slot & 7U;
        block_vectors[(size_t)block * RINHA_DIMS + lane] = (int16_t)(i * 10);
        block_labels[slot] = (unsigned char)((i == 0 || i == 1 || i == 3) ? 1 : 0);
    }
    block_centroids[1][0] = 100;

    memset(&index, 0, sizeof(index));
    index.count = 10;
    index.physical_count = 16;
    index.layout = RINHA_INDEX_LAYOUT_IVF_BLOCK8;
    index.list_count = 2;
    index.nprobe = 2;
    index.repair_nprobe = 2;
    index.offsets = block_offsets;
    index.slot_offsets = block_slot_offsets;
    index.centroids = &block_centroids[0][0];
    index.vectors = block_vectors;
    index.labels = block_labels;
    memset(query, 0, sizeof(query));
    fraud = rinha_search_fraud_count(&index, query);
    if (fraud != 3) {
        fprintf(stderr, "block8 fraud=%u want=3\n", fraud);
        return 1;
    }

    int16_t v2_vectors[2 * RINHA_DIMS * 16];
    int16_t v2_centroids_t[RINHA_DIMS * 2];
    uint32_t v2_offsets[3] = {0, 1, 2};
    unsigned char v2_labels[32];
    memset(v2_vectors, 0, sizeof(v2_vectors));
    memset(v2_centroids_t, 0, sizeof(v2_centroids_t));
    for (int i = 0; i < 32; ++i) v2_labels[i] = 0;
    for (int blk = 0; blk < 2; ++blk) {
        for (int lane = 0; lane < 16; ++lane) {
            v2_vectors[((size_t)blk * RINHA_DIMS + 0U) * 16U + (size_t)lane] = 1000;
        }
    }
    v2_centroids_t[0] = 1000;
    v2_centroids_t[1] = 0;
    unsigned char v2_near_labels[5] = {1, 0, 1, 0, 1};
    for (int lane = 0; lane < 5; ++lane) {
        v2_vectors[((size_t)1U * RINHA_DIMS + 0U) * 16U + (size_t)lane] = (int16_t)lane;
        v2_labels[16 + lane] = v2_near_labels[lane];
    }

    memset(&index, 0, sizeof(index));
    index.count = 32;
    index.physical_count = 32;
    index.layout = RINHA_INDEX_LAYOUT_IVF_KMEANS_BLOCK16;
    index.list_count = 2;
    index.nprobe = 1;
    index.offsets = v2_offsets;
    index.centroids = v2_centroids_t;
    index.vectors = v2_vectors;
    index.labels = v2_labels;
    memset(query, 0, sizeof(query));
    fraud = rinha_search_fraud_count(&index, query);
    if (fraud != 3) {
        fprintf(stderr, "block16 v2 fraud=%u want=3\n", fraud);
        return 1;
    }

    const char *v2_path = "/tmp/rinha_test_search_v2_index.bin";
    rinha_index_header_t v2_header;
    memset(&v2_header, 0, sizeof(v2_header));
    v2_header.magic = RINHA_INDEX_MAGIC;
    v2_header.version = RINHA_INDEX_VERSION;
    v2_header.count = 32;
    v2_header.dims = RINHA_DIMS;
    v2_header.scale = RINHA_SCALE;
    v2_header.layout = RINHA_INDEX_LAYOUT_IVF_KMEANS_BLOCK16;
    v2_header.list_count = 2;
    v2_header.default_nprobe = 1;
    v2_header.reserved[0] = RINHA_INDEX_FLAG_TRANSPOSED_CENTROIDS;
    v2_header.reserved[1] = 2;
    FILE *v2_file = fopen(v2_path, "wb");
    int v2_write_error = v2_file == NULL;
    if (!v2_write_error) {
        v2_write_error = write_all(v2_file, &v2_header, sizeof(v2_header)) != 0 ||
            write_all(v2_file, v2_offsets, sizeof(v2_offsets)) != 0 ||
            write_all(v2_file, v2_centroids_t, sizeof(v2_centroids_t)) != 0 ||
            write_all(v2_file, v2_labels, sizeof(v2_labels)) != 0 ||
            write_all(v2_file, v2_vectors, sizeof(v2_vectors)) != 0;
        if (fclose(v2_file) != 0) v2_write_error = 1;
    }
    if (v2_write_error) {
        remove(v2_path);
        fprintf(stderr, "failed to write v2 test index\n");
        return 1;
    }

    memset(&index, 0, sizeof(index));
    if (rinha_index_load(v2_path, &index) != 0) {
        remove(v2_path);
        fprintf(stderr, "failed to load v2 test index\n");
        return 1;
    }
    remove(v2_path);
    if (index.layout != RINHA_INDEX_LAYOUT_IVF_KMEANS_BLOCK16 || index.physical_count != 32 || index.offsets[2] != 2 || index.labels != (const uint8_t *)index.raw + sizeof(v2_header) + sizeof(v2_offsets) + sizeof(v2_centroids_t)) {
        fprintf(stderr, "loaded v2 index metadata mismatch\n");
        rinha_index_close(&index);
        return 1;
    }
    fraud = rinha_search_fraud_count(&index, query);
    if (fraud != 3) {
        fprintf(stderr, "loaded block16 v2 fraud=%u want=3\n", fraud);
        rinha_index_close(&index);
        return 1;
    }
    rinha_index_close(&index);

    return 0;
}
