#define _GNU_SOURCE

#include "common/distance.h"
#include "common/index.h"
#include "common/search.h"
#include "common/vectorize.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_line(FILE *f, size_t *cap) {
    if (*cap == 0) *cap = 4096;
    char *buf = (char *)malloc(*cap);
    if (buf == NULL) return NULL;
    size_t len = 0;
    for (;;) {
        if (fgets(buf + len, (int)(*cap - len), f) == NULL) {
            free(buf);
            return NULL;
        }
        len += strlen(buf + len);
        if (len > 0 && buf[len - 1] == '\n') return buf;
        *cap *= 2;
        char *next = (char *)realloc(buf, *cap);
        if (next == NULL) {
            free(buf);
            return NULL;
        }
        buf = next;
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s index.bin expected-jsonl\n", argv[0]);
        return 2;
    }
    rinha_index_t index;
    if (rinha_index_load(argv[1], &index) != 0) {
        fprintf(stderr, "failed to load index %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    FILE *f = fopen(argv[2], "rb");
    if (f == NULL) {
        fprintf(stderr, "failed to open %s: %s\n", argv[2], strerror(errno));
        rinha_index_close(&index);
        return 1;
    }
    uint64_t total = 0, fp = 0, fn = 0, mismatches = 0, vectorize_errors = 0;
    uint64_t counts[6] = {0};
    size_t cap = 0;
    char *line;
    while ((line = read_line(f, &cap)) != NULL) {
        char *tab = strchr(line, '\t');
        if (tab == NULL) {
            free(line);
            continue;
        }
        int expected = atoi(line);
        char *body = tab + 1;
        size_t len = strlen(body);
        while (len > 0 && (body[len - 1] == '\n' || body[len - 1] == '\r')) body[--len] = 0;
        int16_t vec[RINHA_DIMS];
        if (!rinha_vectorize(body, len, vec)) {
            ++vectorize_errors;
            free(line);
            ++total;
            continue;
        }
        uint8_t actual = rinha_search_fraud_count(&index, vec);
        if (actual <= 5) ++counts[actual];
        int want_ok = expected < 3;
        int got_ok = actual < 3;
        if (want_ok && !got_ok) ++fp;
        if (!want_ok && got_ok) ++fn;
        if (actual != expected) {
            if (mismatches < 40) {
                fprintf(stderr, "mismatch line=%llu expected=%d actual=%u want_ok=%d got_ok=%d\n",
                        (unsigned long long)(total + 1), expected, actual, want_ok, got_ok);
            }
            ++mismatches;
        }
        ++total;
        free(line);
    }
    fclose(f);
    rinha_index_close(&index);
    printf("total=%llu mismatches=%llu fp=%llu fn=%llu vectorize_errors=%llu buckets=%llu,%llu,%llu,%llu,%llu,%llu\n",
           (unsigned long long)total,
           (unsigned long long)mismatches,
           (unsigned long long)fp,
           (unsigned long long)fn,
           (unsigned long long)vectorize_errors,
           (unsigned long long)counts[0],
           (unsigned long long)counts[1],
           (unsigned long long)counts[2],
           (unsigned long long)counts[3],
           (unsigned long long)counts[4],
           (unsigned long long)counts[5]);
    return fp == 0 && fn == 0 && vectorize_errors == 0 ? 0 : 1;
}
