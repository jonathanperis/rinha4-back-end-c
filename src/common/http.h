#ifndef RINHA_HTTP_H
#define RINHA_HTTP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *data;
    size_t len;
} rinha_response_t;

int rinha_starts_with(const char *data, size_t len, const char *prefix);
int rinha_content_length(const char *headers, size_t len);
rinha_response_t rinha_ready_response(int close_after_response);
rinha_response_t rinha_fraud_response(uint8_t fraud_count, int close_after_response);
rinha_response_t rinha_not_found_response(int close_after_response);

#endif
