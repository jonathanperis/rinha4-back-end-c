#ifndef RINHA_TOPK_H
#define RINHA_TOPK_H

#include <stdint.h>

/* Fixed-size nearest-5 accumulator used by every search layout.
   The entries are intentionally unsorted: `worst` points at the current
   farthest slot, so inserting a better distance only scans five elements.
   Labels are 0/1 fraud markers; summing the five labels gives fraud_count. */
typedef struct {
    uint64_t dist[5];
    uint8_t label[5];
    int worst;
} rinha_top5_t;

static inline void rinha_top5_init(rinha_top5_t *top) {
    for (int i = 0; i < 5; ++i) {
        top->dist[i] = UINT64_MAX;
        top->label[i] = 0;
    }
    top->worst = 0;
}

static inline uint64_t rinha_top5_worst_dist(const rinha_top5_t *top) {
    return top->dist[top->worst];
}

static inline void rinha_top5_add(rinha_top5_t *top, uint64_t dist, uint8_t label) {
    if (dist >= top->dist[top->worst]) return;
    top->dist[top->worst] = dist;
    top->label[top->worst] = label;
    top->worst = 0;
    for (int i = 1; i < 5; ++i) {
        if (top->dist[i] > top->dist[top->worst]) top->worst = i;
    }
}

static inline uint8_t rinha_top5_fraud_count(const rinha_top5_t *top) {
    return (uint8_t)(top->label[0] + top->label[1] + top->label[2] + top->label[3] + top->label[4]);
}

#endif
