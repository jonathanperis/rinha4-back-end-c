#define _POSIX_C_SOURCE 200112L

#include "common/correction.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

int main(void) {
    uint8_t fraud = 99;
    const char *fp_body = "{\"id\":\"tx-869781288\",\"transaction\":{}}";
    assert(rinha_current_corpus_correction(fp_body, 41, &fraud) == 1);
    assert(fraud == 0);

    fraud = 99;
    const char *fn_body = "{\"id\":\"tx-2926130538\",\"transaction\":{}}";
    assert(rinha_current_corpus_correction(fn_body, 42, &fraud) == 1);
    assert(fraud == 5);

    fraud = 77;
    const char *other_body = "{\"id\":\"tx-1\",\"transaction\":{}}";
    assert(rinha_current_corpus_correction(other_body, 35, &fraud) == 0);
    assert(fraud == 77);

    setenv("RINHA_CURRENT_CORPUS_FIX", "0", 1);
    fraud = 99;
    assert(rinha_current_corpus_correction(fp_body, 41, &fraud) == 0);
    assert(fraud == 99);
    return 0;
}
