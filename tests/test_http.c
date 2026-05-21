#include "common/http.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    const char *headers = "POST /fraud-score HTTP/1.1\r\nHost: x\r\nContent-Length: 123\r\n\r\n";
    if (!rinha_starts_with(headers, strlen(headers), "POST /fraud-score ")) {
        fprintf(stderr, "starts_with failed\n");
        return 1;
    }
    if (rinha_content_length(headers, strlen(headers)) != 123) {
        fprintf(stderr, "content_length failed\n");
        return 1;
    }
    rinha_response_t r0 = rinha_fraud_response(0, 0);
    rinha_response_t r5 = rinha_fraud_response(5, 1);
    if (strstr(r0.data, "\"approved\":true") == NULL || strstr(r5.data, "Connection: close") == NULL) {
        fprintf(stderr, "response failed\n");
        return 1;
    }
    return 0;
}
