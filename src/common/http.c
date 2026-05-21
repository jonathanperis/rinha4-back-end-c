#include "common/http.h"

#include <string.h>

#define RESP(s) { s, sizeof(s) - 1 }

int rinha_starts_with(const char *data, size_t len, const char *prefix) {
    size_t plen = strlen(prefix);
    return len >= plen && memcmp(data, prefix, plen) == 0;
}

static size_t find_header(const char *data, size_t len, const char *needle) {
    size_t nlen = strlen(needle);
    if (len < nlen) return (size_t)-1;
    for (size_t i = 0; i <= len - nlen; ++i) {
        if (data[i] == needle[0] && memcmp(data + i, needle, nlen) == 0) return i;
    }
    return (size_t)-1;
}

static int parse_content_length_value(const char *headers, size_t len, size_t p) {
    if (p == (size_t)-1) return 0;
    p += 15;
    while (p < len && headers[p] == ' ') ++p;
    int value = 0;
    while (p < len && headers[p] >= '0' && headers[p] <= '9') {
        value = value * 10 + headers[p] - '0';
        ++p;
    }
    return value;
}

int rinha_content_length(const char *headers, size_t len) {
    /* k6 sends Content-Length as the last header. Parse that fast path first,
       then fall back to the generic case-insensitive-ish scan for safety. */
    if (len >= 19) {
        size_t end = len;
        if (end >= 2 && headers[end - 2] == '\r' && headers[end - 1] == '\n') end -= 2;
        if (end >= 2 && headers[end - 2] == '\r' && headers[end - 1] == '\n') {
            size_t line = end - 2;
            while (line > 0 && headers[line - 1] != '\n') --line;
            if (line < end && headers[line] == '\r') ++line;
            if (end - line >= 15 && memcmp(headers + line, "Content-Length:", 15) == 0) {
                return parse_content_length_value(headers, len, line);
            }
        }
    }

    size_t p = find_header(headers, len, "Content-Length:");
    if (p == (size_t)-1) p = find_header(headers, len, "content-length:");
    return parse_content_length_value(headers, len, p);
}

rinha_response_t rinha_ready_response(int close_after_response) {
    static const rinha_response_t keep = RESP("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 15\r\nConnection: keep-alive\r\n\r\n{\"status\":\"ok\"}");
    static const rinha_response_t close = RESP("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 15\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}");
    return close_after_response ? close : keep;
}

rinha_response_t rinha_fraud_response(uint8_t fraud_count, int close_after_response) {
    /* Prebuilt responses avoid per-request formatting/allocation. The fraud
       count is the number of fraudulent labels in the nearest five references,
       so it maps directly to scores 0.0, 0.2, ... 1.0. */
    static const rinha_response_t keep[] = {
        RESP("HTTP/1.1 200 OK\r\nContent-Length: 35\r\n\r\n{\"approved\":true,\"fraud_score\":0.0}"),
        RESP("HTTP/1.1 200 OK\r\nContent-Length: 35\r\n\r\n{\"approved\":true,\"fraud_score\":0.2}"),
        RESP("HTTP/1.1 200 OK\r\nContent-Length: 35\r\n\r\n{\"approved\":true,\"fraud_score\":0.4}"),
        RESP("HTTP/1.1 200 OK\r\nContent-Length: 36\r\n\r\n{\"approved\":false,\"fraud_score\":0.6}"),
        RESP("HTTP/1.1 200 OK\r\nContent-Length: 36\r\n\r\n{\"approved\":false,\"fraud_score\":0.8}"),
        RESP("HTTP/1.1 200 OK\r\nContent-Length: 36\r\n\r\n{\"approved\":false,\"fraud_score\":1.0}")
    };
    static const rinha_response_t close[] = {
        RESP("HTTP/1.1 200 OK\r\nContent-Length: 35\r\nConnection: close\r\n\r\n{\"approved\":true,\"fraud_score\":0.0}"),
        RESP("HTTP/1.1 200 OK\r\nContent-Length: 35\r\nConnection: close\r\n\r\n{\"approved\":true,\"fraud_score\":0.2}"),
        RESP("HTTP/1.1 200 OK\r\nContent-Length: 35\r\nConnection: close\r\n\r\n{\"approved\":true,\"fraud_score\":0.4}"),
        RESP("HTTP/1.1 200 OK\r\nContent-Length: 36\r\nConnection: close\r\n\r\n{\"approved\":false,\"fraud_score\":0.6}"),
        RESP("HTTP/1.1 200 OK\r\nContent-Length: 36\r\nConnection: close\r\n\r\n{\"approved\":false,\"fraud_score\":0.8}"),
        RESP("HTTP/1.1 200 OK\r\nContent-Length: 36\r\nConnection: close\r\n\r\n{\"approved\":false,\"fraud_score\":1.0}")
    };
    uint8_t idx = fraud_count <= 5 ? fraud_count : 5;
    return close_after_response ? close[idx] : keep[idx];
}

rinha_response_t rinha_not_found_response(int close_after_response) {
    static const rinha_response_t keep = RESP("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    static const rinha_response_t close = RESP("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    return close_after_response ? close : keep;
}
