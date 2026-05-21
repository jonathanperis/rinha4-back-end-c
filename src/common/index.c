#define _POSIX_C_SOURCE 200112L

#include "common/index.h"

#include "common/index_format.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static volatile uint64_t g_warm_pages_sink;

static void clear_index(rinha_index_t *index) {
    memset(index, 0, sizeof(*index));
}

static uint32_t env_u32(const char *name, uint32_t fallback, uint32_t min, uint32_t max) {
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') return fallback;
    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || parsed > UINT_MAX) return fallback;
    if (parsed < min || parsed > max) return fallback;
    return (uint32_t)parsed;
}

static uint64_t env_u64(const char *name, uint64_t fallback) {
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') return fallback;
    char *end = NULL;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (end == value) return fallback;
    return (uint64_t)parsed;
}

static uint32_t env_bool(const char *name, uint32_t fallback) {
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') return fallback;
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 || strcmp(value, "FALSE") == 0) return 0;
    return 1;
}

int rinha_index_load(const char *path, rinha_index_t *index) {
    /* The runtime never owns parsed copies of the index. It mmaps the binary
       file and then points each field at validated offsets, so startup is cheap
       and the kernel can page/cache the shared read-only data. */
    clear_index(index);
    if (path == NULL || path[0] == '\0') return -1;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return -1;
    }

#ifdef POSIX_FADV_WILLNEED
    (void)posix_fadvise(fd, 0, st.st_size, POSIX_FADV_WILLNEED);
#endif

    index->size = (size_t)st.st_size;
    int mmap_flags = MAP_PRIVATE;
#ifdef MAP_POPULATE
    if (env_bool("INDEX_MAP_POPULATE", 0)) mmap_flags |= MAP_POPULATE;
#endif
    void *mapped = mmap(NULL, index->size, PROT_READ, mmap_flags, fd, 0);
    if (mapped != MAP_FAILED) {
        index->mapped = 1;
        index->raw = mapped;
#ifdef MADV_HUGEPAGE
        (void)madvise(mapped, index->size, MADV_HUGEPAGE);
#endif
#ifdef MADV_WILLNEED
        (void)madvise(mapped, index->size, MADV_WILLNEED);
#endif
        if (env_bool("INDEX_MLOCK", 0)) (void)mlock(mapped, index->size);
        if (env_bool("INDEX_WARM_PAGES", 1)) {
            const volatile uint8_t *bytes = (const volatile uint8_t *)mapped;
            uint64_t warm = 0;
            for (size_t off = 0; off < index->size; off += 4096U) warm += bytes[off];
            warm += bytes[index->size - 1U];
            g_warm_pages_sink += warm;
        }
        close(fd);
    } else {
        index->storage = (uint8_t *)malloc(index->size);
        if (index->storage == NULL) {
            close(fd);
            return -1;
        }
        size_t off = 0;
        while (off < index->size) {
            ssize_t n = read(fd, index->storage + off, index->size - off);
            if (n > 0) {
                off += (size_t)n;
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            free(index->storage);
            clear_index(index);
            close(fd);
            return -1;
        }
        close(fd);
        index->raw = index->storage;
    }

    if (index->size < sizeof(rinha_index_header_t)) {
        rinha_index_close(index);
        return -1;
    }
    const rinha_index_header_t *header = (const rinha_index_header_t *)index->raw;
    if (header->magic != RINHA_INDEX_MAGIC || header->version == 0 || header->version > RINHA_INDEX_VERSION || header->dims != RINHA_DIMS || header->scale != RINHA_SCALE) {
        rinha_index_close(index);
        return -1;
    }

    uint32_t layout = header->layout == 0 ? RINHA_INDEX_LAYOUT_FLAT : header->layout;
    uint32_t nprobe = header->default_nprobe == 0 ? RINHA_INDEX_DEFAULT_NPROBE : header->default_nprobe;
    uint32_t repair_nprobe = 0;
    uint32_t repair_min_fraud = RINHA_INDEX_DEFAULT_REPAIR_MIN_FRAUD;
    uint32_t repair_max_fraud = RINHA_INDEX_DEFAULT_REPAIR_MAX_FRAUD;
    uint32_t exact_fallback = 0;
    uint32_t physical_count = header->count;
    size_t vectors_size = (size_t)header->count * RINHA_DIMS * sizeof(int16_t);
    size_t labels_size = (size_t)header->count;
    size_t required = sizeof(rinha_index_header_t) + vectors_size + labels_size;
    size_t vectors_offset = sizeof(rinha_index_header_t);
    size_t labels_offset = vectors_offset + vectors_size;
    size_t slot_offsets_offset = 0;
    size_t centroids_offset = 0;
    size_t bounds_min_offset = 0;
    size_t bounds_max_offset = 0;
    size_t profile_counts_offset = 0;
    size_t profile_masks_offset = 0;
    size_t reference_fastpath1_edges_offset = 0;
    size_t reference_fastpath1_offset = 0;
    size_t reference_fastpath2_edges_offset = 0;
    size_t reference_fastpath2_offset = 0;
    size_t kd_nodes_offset = 0;
    uint32_t has_bounds = 0;

    if (layout == RINHA_INDEX_LAYOUT_IVF_KMEANS_BLOCK16) {
        /* K-means/block16 files store: header, list block offsets, transposed
           centroids [dim][list], padded labels, then SoA vector blocks. */
        if (header->list_count == 0 || header->reserved[1] == 0 || (header->reserved[0] & RINHA_INDEX_FLAG_TRANSPOSED_CENTROIDS) == 0) {
            rinha_index_close(index);
            return -1;
        }
        uint32_t total_blocks = header->reserved[1];
        if (total_blocks > UINT32_MAX / 16U || header->count > total_blocks * 16U) {
            rinha_index_close(index);
            return -1;
        }
        size_t offsets_size = ((size_t)header->list_count + 1U) * sizeof(uint32_t);
        size_t centroids_size = (size_t)RINHA_DIMS * header->list_count * sizeof(int16_t);
        physical_count = total_blocks * 16U;
        labels_size = (size_t)physical_count;
        vectors_size = (size_t)total_blocks * RINHA_DIMS * 16U * sizeof(int16_t);
        centroids_offset = sizeof(rinha_index_header_t) + offsets_size;
        labels_offset = centroids_offset + centroids_size;
        vectors_offset = labels_offset + labels_size;
        required = vectors_offset + vectors_size;
        nprobe = env_u32("INDEX_NPROBE", nprobe, 1U, header->list_count);
        repair_nprobe = env_u32("INDEX_REPAIR_NPROBE", RINHA_INDEX_DEFAULT_REPAIR_NPROBE, 0U, header->list_count);
        repair_min_fraud = env_u32("INDEX_REPAIR_MIN_FRAUD", repair_min_fraud, 0U, 5U);
        repair_max_fraud = env_u32("INDEX_REPAIR_MAX_FRAUD", repair_max_fraud, 0U, 5U);
        exact_fallback = env_u32("INDEX_EXACT_FALLBACK", RINHA_INDEX_DEFAULT_EXACT_FALLBACK, 0U, 1U);
        if (repair_min_fraud > repair_max_fraud) repair_nprobe = 0;
        index->repair_worst_threshold[0] = env_u64("INDEX_REPAIR0_WORST_THRESHOLD", 0U);
        index->repair_worst_threshold[1] = env_u64("INDEX_REPAIR1_WORST_THRESHOLD", 0U);
        index->repair_worst_threshold[2] = env_u64("INDEX_REPAIR2_WORST_THRESHOLD", 0U);
        index->repair_worst_threshold[3] = env_u64("INDEX_REPAIR3_WORST_THRESHOLD", 0U);
        index->repair_worst_threshold[4] = env_u64("INDEX_REPAIR4_WORST_THRESHOLD", 0U);
        index->repair_worst_threshold[5] = env_u64("INDEX_REPAIR5_WORST_THRESHOLD", 0U);
    } else if (layout == RINHA_INDEX_LAYOUT_IVF || layout == RINHA_INDEX_LAYOUT_IVF_BLOCK8) {
        /* Legacy IVF keeps plain vector rows; the current default IVF_BLOCK8
           adds slot_offsets so each list can be scanned as padded 8-lane SoA
           blocks plus optional min/max bounds for certification. */
        if (header->list_count == 0 || header->list_count > header->count) {
            rinha_index_close(index);
            return -1;
        }
        size_t offsets_size = ((size_t)header->list_count + 1U) * sizeof(uint32_t);
        size_t slot_offsets_size = layout == RINHA_INDEX_LAYOUT_IVF_BLOCK8 ? offsets_size : 0U;
        size_t centroids_size = (size_t)header->list_count * RINHA_DIMS * sizeof(int16_t);
        size_t bounds_size = (size_t)header->list_count * RINHA_DIMS * sizeof(int16_t);
        has_bounds = header->version >= 2U && (header->reserved[0] & RINHA_INDEX_FLAG_BOUNDS) != 0;
        if (layout == RINHA_INDEX_LAYOUT_IVF_BLOCK8) {
            physical_count = header->reserved[1];
            if (physical_count < header->count || (physical_count & 7U) != 0U) {
                rinha_index_close(index);
                return -1;
            }
            vectors_size = (size_t)physical_count * RINHA_DIMS * sizeof(int16_t);
            labels_size = (size_t)physical_count;
        }
        slot_offsets_offset = sizeof(rinha_index_header_t) + offsets_size;
        centroids_offset = slot_offsets_offset + slot_offsets_size;
        bounds_min_offset = centroids_offset + centroids_size;
        bounds_max_offset = bounds_min_offset + bounds_size;
        vectors_offset = bounds_min_offset + (has_bounds ? bounds_size * 2U : 0U);
        labels_offset = vectors_offset + vectors_size;
        required = labels_offset + labels_size;
        nprobe = env_u32("INDEX_NPROBE", nprobe, 1U, header->list_count);
        repair_nprobe = env_u32("INDEX_REPAIR_NPROBE", RINHA_INDEX_DEFAULT_REPAIR_NPROBE, 0U, header->list_count);
        repair_min_fraud = env_u32("INDEX_REPAIR_MIN_FRAUD", repair_min_fraud, 0U, 5U);
        repair_max_fraud = env_u32("INDEX_REPAIR_MAX_FRAUD", repair_max_fraud, 0U, 5U);
        exact_fallback = env_u32("INDEX_EXACT_FALLBACK", RINHA_INDEX_DEFAULT_EXACT_FALLBACK, 0U, 1U);
        if (repair_min_fraud > repair_max_fraud) repair_nprobe = 0;
        index->repair_worst_threshold[0] = env_u64("INDEX_REPAIR0_WORST_THRESHOLD", 0U);
        index->repair_worst_threshold[1] = env_u64("INDEX_REPAIR1_WORST_THRESHOLD", 0U);
        index->repair_worst_threshold[2] = env_u64("INDEX_REPAIR2_WORST_THRESHOLD", 0U);
        index->repair_worst_threshold[3] = env_u64("INDEX_REPAIR3_WORST_THRESHOLD", 0U);
        index->repair_worst_threshold[4] = env_u64("INDEX_REPAIR4_WORST_THRESHOLD", 0U);
        index->repair_worst_threshold[5] = env_u64("INDEX_REPAIR5_WORST_THRESHOLD", 0U);
    } else if (layout == RINHA_INDEX_LAYOUT_KD_TREE_BLOCK8) {
        /* KD_TREE_BLOCK8 stores 256 semantic partition roots, KD nodes with
           bounding boxes, then the same padded 8-lane SoA leaf vectors. */
        if (header->list_count != 256U || header->reserved[1] == 0U || header->reserved[3] < header->count || (header->reserved[3] & 7U) != 0U) {
            rinha_index_close(index);
            return -1;
        }
        size_t roots_size = ((size_t)header->list_count + 1U) * sizeof(uint32_t);
        size_t nodes_size = (size_t)header->reserved[1] * sizeof(rinha_kd_node_disk_t);
        physical_count = header->reserved[3];
        labels_size = (size_t)physical_count;
        vectors_size = (size_t)physical_count * RINHA_DIMS * sizeof(int16_t);
        kd_nodes_offset = sizeof(rinha_index_header_t) + roots_size;
        vectors_offset = kd_nodes_offset + nodes_size;
        labels_offset = vectors_offset + vectors_size;
        required = labels_offset + labels_size;
        nprobe = header->list_count;
        repair_nprobe = 0;
        exact_fallback = 0;
    } else if (layout != RINHA_INDEX_LAYOUT_FLAT) {
        rinha_index_close(index);
        return -1;
    }

    uint32_t has_profile_fastpath = (header->reserved[0] & RINHA_INDEX_FLAG_PROFILE_FASTPATH) != 0;
    uint32_t has_reference_fastpath = (header->reserved[0] & RINHA_INDEX_FLAG_REFERENCE_FASTPATH) != 0;
    if (has_profile_fastpath) {
        profile_counts_offset = required;
        profile_masks_offset = profile_counts_offset + (size_t)RINHA_PROFILE_FASTPATH_COUNT * sizeof(uint16_t);
        required = profile_masks_offset + (size_t)RINHA_PROFILE_FASTPATH_COUNT * sizeof(uint8_t);
    }
    if (has_reference_fastpath) {
        reference_fastpath1_edges_offset = required;
        reference_fastpath1_offset = reference_fastpath1_edges_offset + (size_t)RINHA_REFERENCE_FASTPATH1_EDGES * sizeof(int16_t);
        reference_fastpath2_edges_offset = reference_fastpath1_offset + (size_t)RINHA_REFERENCE_FASTPATH1_SLOTS;
        reference_fastpath2_offset = reference_fastpath2_edges_offset + (size_t)RINHA_REFERENCE_FASTPATH2_EDGES * sizeof(int16_t);
        required = reference_fastpath2_offset + (size_t)RINHA_REFERENCE_FASTPATH2_SLOTS;
    }

    if (required > index->size) {
        rinha_index_close(index);
        return -1;
    }
    if (layout == RINHA_INDEX_LAYOUT_IVF_KMEANS_BLOCK16) {
        const uint32_t *offsets = (const uint32_t *)((const uint8_t *)index->raw + sizeof(rinha_index_header_t));
        uint32_t total_blocks = header->reserved[1];
        if (offsets[0] != 0 || offsets[header->list_count] != total_blocks) {
            rinha_index_close(index);
            return -1;
        }
        for (uint32_t list = 0; list < header->list_count; ++list) {
            if (offsets[list] > offsets[list + 1U] || offsets[list + 1U] > total_blocks) {
                rinha_index_close(index);
                return -1;
            }
        }
    }

    index->count = header->count;
    index->physical_count = physical_count;
    index->layout = layout;
    index->list_count = header->list_count;
    index->nprobe = nprobe;
    index->repair_nprobe = repair_nprobe;
    index->repair_min_fraud = repair_min_fraud;
    index->repair_max_fraud = repair_max_fraud;
    index->exact_fallback = exact_fallback;
    if (layout == RINHA_INDEX_LAYOUT_IVF || layout == RINHA_INDEX_LAYOUT_IVF_BLOCK8 || layout == RINHA_INDEX_LAYOUT_IVF_KMEANS_BLOCK16) {
        index->offsets = (const uint32_t *)((const uint8_t *)index->raw + sizeof(rinha_index_header_t));
        if (layout == RINHA_INDEX_LAYOUT_IVF_BLOCK8) index->slot_offsets = (const uint32_t *)((const uint8_t *)index->raw + slot_offsets_offset);
        index->centroids = (const int16_t *)((const uint8_t *)index->raw + centroids_offset);
        if (has_bounds) {
            index->bounds_min = (const int16_t *)((const uint8_t *)index->raw + bounds_min_offset);
            index->bounds_max = (const int16_t *)((const uint8_t *)index->raw + bounds_max_offset);
        }
    } else if (layout == RINHA_INDEX_LAYOUT_KD_TREE_BLOCK8) {
        index->offsets = (const uint32_t *)((const uint8_t *)index->raw + sizeof(rinha_index_header_t));
        index->kd_nodes = (const void *)((const uint8_t *)index->raw + kd_nodes_offset);
        index->kd_node_count = header->reserved[1];
        /* Metadata only today: useful for index diagnostics even though search
           walks node bounds and does not need the precomputed leaf count. */
        index->kd_leaf_count = header->reserved[2];
        uint64_t early_milli = env_u64("INDEX_KD_EARLY_DISTANCE_MILLI", 0U);
        if (early_milli != 0U) {
            uint64_t scaled = (uint64_t)RINHA_SCALE * early_milli / 1000U;
            index->kd_early_distance_limit = scaled * scaled;
        }
    }
    index->vectors = (const int16_t *)((const uint8_t *)index->raw + vectors_offset);
    index->labels = (const uint8_t *)index->raw + labels_offset;
    if (has_profile_fastpath) {
        index->profile_counts = (const uint16_t *)((const uint8_t *)index->raw + profile_counts_offset);
        index->profile_masks = (const uint8_t *)index->raw + profile_masks_offset;
        index->profile_fastpath = env_bool("BUCKET_PROFILE_FASTPATH", 1U);
        index->profile_legit_min_count = env_u32("BUCKET_PROFILE_LEGIT_MIN_COUNT", 1000U, 1U, UINT16_MAX);
        index->profile_fraud_min_count = env_u32("BUCKET_PROFILE_FRAUD_MIN_COUNT", 1000U, 1U, UINT16_MAX);
    }
    if (has_reference_fastpath) {
        index->reference_fastpath1_edges = (const int16_t *)((const uint8_t *)index->raw + reference_fastpath1_edges_offset);
        index->reference_fastpath1 = (const uint8_t *)index->raw + reference_fastpath1_offset;
        index->reference_fastpath2_edges = (const int16_t *)((const uint8_t *)index->raw + reference_fastpath2_edges_offset);
        index->reference_fastpath2 = (const uint8_t *)index->raw + reference_fastpath2_offset;
        index->reference_fastpath = env_bool("BUCKET_REFERENCE_FASTPATH", 1U);
        index->reference_fastpath_legit = env_bool("BUCKET_REFERENCE_FASTPATH_LEGIT", 0U);
        index->reference_fastpath_fraud = env_bool("BUCKET_REFERENCE_FASTPATH_FRAUD", 1U);
        index->reference_fastpath2_legit = env_bool("BUCKET_REFERENCE_FASTPATH2_LEGIT", 0U);
        index->reference_fastpath2_fraud = env_bool("BUCKET_REFERENCE_FASTPATH2_FRAUD", index->reference_fastpath_fraud);
    }
    return 0;
}

void rinha_index_close(rinha_index_t *index) {
    if (index->mapped && index->raw != NULL) {
        munmap(index->raw, index->size);
    }
    free(index->storage);
    clear_index(index);
}
