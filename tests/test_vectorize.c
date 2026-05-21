#include "common/vectorize.h"

#include <stdio.h>
#include <string.h>

static int expect_i16(const char *name, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s got=%d want=%d\n", name, got, want);
        return 0;
    }
    return 1;
}

int main(void) {
    const char *body = "{\"id\":\"tx-test\",\"transaction\":{\"amount\":384.88,\"installments\":3,\"requested_at\":\"2026-03-11T20:23:35Z\"},\"customer\":{\"avg_amount\":769.76,\"tx_count_24h\":3,\"known_merchants\":[\"MERC-009\",\"MERC-001\"]},\"merchant\":{\"id\":\"MERC-001\",\"mcc\":\"5912\",\"avg_amount\":298.95},\"terminal\":{\"is_online\":false,\"card_present\":true,\"km_from_home\":13.7},\"last_transaction\":{\"timestamp\":\"2026-03-11T20:08:35Z\",\"km_from_current\":18.8}}";
    int16_t v[RINHA_DIMS];
    if (!rinha_vectorize(body, strlen(body), v)) {
        fprintf(stderr, "vectorize failed\n");
        return 1;
    }
    int ok = 1;
    ok &= expect_i16("amount", v[0], 385);
    ok &= expect_i16("installments", v[1], 2500);
    ok &= expect_i16("hour", v[3], 8696);
    ok &= expect_i16("day", v[4], 3333);
    ok &= expect_i16("minutes", v[5], 104);
    ok &= expect_i16("last_km", v[6], 188);
    ok &= expect_i16("home", v[7], 137);
    ok &= expect_i16("tx_count", v[8], 1500);
    ok &= expect_i16("is_online", v[9], 0);
    ok &= expect_i16("card_present", v[10], 10000);
    ok &= expect_i16("known", v[11], 0);
    ok &= expect_i16("mcc", v[12], 2000);
    ok &= expect_i16("merchant_avg", v[13], 299);

    const char *null_body = "{\"id\":\"tx-test\",\"transaction\":{\"amount\":41.12,\"installments\":2,\"requested_at\":\"2026-03-11T18:45:53Z\"},\"customer\":{\"avg_amount\":82.24,\"tx_count_24h\":3,\"known_merchants\":[\"MERC-003\"]},\"merchant\":{\"id\":\"MERC-016\",\"mcc\":\"5411\",\"avg_amount\":60.25},\"terminal\":{\"is_online\":false,\"card_present\":true,\"km_from_home\":29.23},\"last_transaction\":null}";
    if (!rinha_vectorize(null_body, strlen(null_body), v)) {
        fprintf(stderr, "null vectorize failed\n");
        return 1;
    }
    ok &= expect_i16("last_null_minutes", v[5], -10000);
    ok &= expect_i16("last_null_km", v[6], -10000);
    ok &= expect_i16("unknown", v[11], 10000);
    return ok ? 0 : 1;
}
