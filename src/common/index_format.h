#ifndef RINHA_INDEX_FORMAT_H
#define RINHA_INDEX_FORMAT_H

#include <stdint.h>

#define RINHA_INDEX_MAGIC 0x3143444e49364852ULL
#define RINHA_INDEX_VERSION 3U
#define RINHA_INDEX_LAYOUT_FLAT 1U
#define RINHA_INDEX_LAYOUT_IVF 2U
#define RINHA_INDEX_LAYOUT_IVF_BLOCK8 3U
#define RINHA_INDEX_LAYOUT_IVF_KMEANS_BLOCK16 4U
#define RINHA_INDEX_LAYOUT_KD_TREE_BLOCK8 5U
#define RINHA_INDEX_FLAG_BOUNDS 1U
#define RINHA_INDEX_FLAG_TRANSPOSED_CENTROIDS 2U
#define RINHA_INDEX_FLAG_PROFILE_FASTPATH 4U
#define RINHA_INDEX_FLAG_REFERENCE_FASTPATH 8U
#define RINHA_PROFILE_FASTPATH_COUNT (1U << 22)
#define RINHA_REFERENCE_FASTPATH1_SLOTS (1U << 24)
#define RINHA_REFERENCE_FASTPATH2_SLOTS (1U << 20)
#define RINHA_REFERENCE_FASTPATH1_EDGES (16U + 8U + 64U + 2U + 8U + 16U + 2U + 4U)
#define RINHA_REFERENCE_FASTPATH2_EDGES (16U + 16U + 16U + 16U + 16U)
#define RINHA_FASTPATH_LEGIT_MASK 1U
#define RINHA_FASTPATH_FRAUD_MASK 2U
#define RINHA_INDEX_DEFAULT_NPROBE 40U
#define RINHA_INDEX_DEFAULT_REPAIR_NPROBE 64U
#define RINHA_INDEX_DEFAULT_REPAIR_MIN_FRAUD 2U
#define RINHA_INDEX_DEFAULT_REPAIR_MAX_FRAUD 3U
#define RINHA_INDEX_DEFAULT_EXACT_FALLBACK 1U

/* Binary mmap index header. Keep this layout stable unless the version changes.
   reserved[0] is the shared flag word. Layout-specific slots currently are:
   - IVF_BLOCK8: reserved[1] = physical padded vector count.
   - IVF_KMEANS_BLOCK16: reserved[1] = total block count.
   - KD_TREE_BLOCK8: reserved[1] = node count, reserved[2] = leaf count,
     reserved[3] = physical padded vector count. */
typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t dims;
    uint32_t scale;
    uint32_t layout;
    uint32_t list_count;
    uint32_t default_nprobe;
    uint32_t reserved[7];
} rinha_index_header_t;

typedef struct {
    uint32_t left;
    uint32_t right;
    uint32_t start;
    uint32_t count;
    uint8_t axis; /* Builder/debug split axis; runtime prunes by bounds instead. */
    uint8_t leaf;
    uint16_t reserved;
    int16_t min[14];
    int16_t max[14];
} rinha_kd_node_disk_t;

#endif
