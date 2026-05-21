#include "common/distance.h"
#include "common/index.h"
#include "common/search.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static char *read_gzip(const char *path, size_t *out_len) {
    gzFile file = gzopen(path, "rb");
    if (file == NULL) return NULL;
    size_t cap = 32U * 1024U * 1024U;
    size_t len = 0;
    char *data = (char *)malloc(cap + 1U);
    if (data == NULL) {
        gzclose(file);
        return NULL;
    }
    for (;;) {
        if (len + 1048576U > cap) {
            cap *= 2U;
            char *next = (char *)realloc(data, cap + 1U);
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

static int parse_reference_vector(char **cursor, int16_t out[RINHA_DIMS], uint8_t *label) {
    char *p = find_token(*cursor, "\"vector\"");
    if (p == NULL) return 0;
    p = strchr(p, '[');
    if (p == NULL) return -1;
    ++p;
    for (int d = 0; d < RINHA_DIMS; ++d) {
        while (*p == ',' || *p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
        char *end = NULL;
        double value = strtod(p, &end);
        if (end == p) return -1;
        out[d] = rinha_qround(value);
        p = end;
    }
    char *label_key = find_token(p, "\"label\"");
    if (label_key == NULL) return -1;
    char *colon = strchr(label_key, ':');
    if (colon == NULL) return -1;
    char *quote = strchr(colon + 1, '"');
    if (quote == NULL || quote[1] == '\0') return -1;
    *label = quote[1] == 'f' ? 1U : 0U;
    *cursor = quote + 1;
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: search-stats-replay index.bin references.json.gz [limit]\n");
        return 2;
    }

    uint32_t limit = 0;
    if (argc >= 4) limit = (uint32_t)strtoul(argv[3], NULL, 10);

    rinha_index_t index;
    if (rinha_index_load(argv[1], &index) != 0) {
        fprintf(stderr, "cannot load index: %s\n", argv[1]);
        return 1;
    }

    size_t raw_len = 0;
    char *raw = read_gzip(argv[2], &raw_len);
    if (raw == NULL) {
        fprintf(stderr, "cannot read references: %s\n", argv[2]);
        rinha_index_close(&index);
        return 1;
    }

    uint64_t rows = 0;
    uint64_t label_bucket[2] = {0, 0};
    uint64_t fraud_bucket[6] = {0, 0, 0, 0, 0, 0};
    char *cursor = raw;
    for (;;) {
        int16_t query[RINHA_DIMS];
        uint8_t label = 0;
        int parsed = parse_reference_vector(&cursor, query, &label);
        if (parsed == 0) break;
        if (parsed < 0) {
            fprintf(stderr, "parse error after rows=%llu\n", (unsigned long long)rows);
            free(raw);
            rinha_index_close(&index);
            return 1;
        }
        uint8_t fraud = rinha_search_fraud_count(&index, query);
        if (fraud > 5U) fraud = 5U;
        if (label > 1U) label = 1U;
        ++label_bucket[label];
        ++fraud_bucket[fraud];
        ++rows;
        if (limit != 0 && rows >= limit) break;
    }

    fprintf(stderr,
            "RINHA_REPLAY rows=%llu reference_bytes=%llu labels=%llu,%llu fraud_counts=%llu,%llu,%llu,%llu,%llu,%llu\n",
            (unsigned long long)rows,
            (unsigned long long)raw_len,
            (unsigned long long)label_bucket[0],
            (unsigned long long)label_bucket[1],
            (unsigned long long)fraud_bucket[0],
            (unsigned long long)fraud_bucket[1],
            (unsigned long long)fraud_bucket[2],
            (unsigned long long)fraud_bucket[3],
            (unsigned long long)fraud_bucket[4],
            (unsigned long long)fraud_bucket[5]);

    free(raw);
    rinha_index_close(&index);
    return rows > 0 ? 0 : 1;
}
