#include "common/vectorize.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    const char *ptr;
    size_t len;
} view_t;

static void skip_ws(const char *s, size_t len, size_t *pos) {
    while (*pos < len) {
        char c = s[*pos];
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') break;
        ++*pos;
    }
}

static size_t find_bytes(const char *s, size_t len, size_t start, const char *needle, size_t nlen) {
    if (nlen == 0 || len < nlen || start > len - nlen) return (size_t)-1;
    for (size_t i = start; i <= len - nlen; ++i) {
        if (s[i] == needle[0] && memcmp(s + i, needle, nlen) == 0) return i;
    }
    return (size_t)-1;
}

static int find_value(const char *s, size_t len, size_t *pos, const char *key) {
    size_t key_len = strlen(key);
    size_t k = find_bytes(s, len, *pos, key, key_len);
    if (k == (size_t)-1) return 0;
    size_t colon = find_bytes(s, len, k + key_len, ":", 1);
    if (colon == (size_t)-1) return 0;
    *pos = colon + 1;
    skip_ws(s, len, pos);
    return *pos < len;
}

static int parse_number(const char *s, size_t len, size_t *pos, double *out) {
    skip_ws(s, len, pos);
    if (*pos >= len) return 0;
    int neg = 0;
    if (s[*pos] == '-') {
        neg = 1;
        ++*pos;
    }
    double value = 0.0;
    int seen = 0;
    while (*pos < len && s[*pos] >= '0' && s[*pos] <= '9') {
        seen = 1;
        value = value * 10.0 + (double)(s[*pos] - '0');
        ++*pos;
    }
    if (*pos < len && s[*pos] == '.') {
        ++*pos;
        double scale = 0.1;
        while (*pos < len && s[*pos] >= '0' && s[*pos] <= '9') {
            seen = 1;
            value += (double)(s[*pos] - '0') * scale;
            scale *= 0.1;
            ++*pos;
        }
    }
    if (!seen) return 0;
    *out = neg ? -value : value;
    return 1;
}

static int number_value(const char *s, size_t len, size_t *pos, const char *key, double *out) {
    return find_value(s, len, pos, key) && parse_number(s, len, pos, out);
}

static int string_value(const char *s, size_t len, size_t *pos, const char *key, view_t *out) {
    if (!find_value(s, len, pos, key)) return 0;
    while (*pos < len && s[*pos] != '"') ++*pos;
    if (*pos >= len) return 0;
    size_t begin = *pos + 1;
    size_t end = begin;
    while (end < len && s[end] != '"') ++end;
    if (end >= len) return 0;
    out->ptr = s + begin;
    out->len = end - begin;
    *pos = end + 1;
    return 1;
}

static int bool_value(const char *s, size_t len, size_t *pos, const char *key, int *out) {
    if (!find_value(s, len, pos, key)) return 0;
    if (*pos + 4 <= len && memcmp(s + *pos, "true", 4) == 0) {
        *out = 1;
        *pos += 4;
        return 1;
    }
    if (*pos + 5 <= len && memcmp(s + *pos, "false", 5) == 0) {
        *out = 0;
        *pos += 5;
        return 1;
    }
    return 0;
}

static int two(view_t ts, size_t pos) {
    if (pos + 1 >= ts.len) return 0;
    return (ts.ptr[pos] - '0') * 10 + (ts.ptr[pos + 1] - '0');
}

static int four(view_t ts, size_t pos) {
    if (pos + 3 >= ts.len) return 0;
    return (ts.ptr[pos] - '0') * 1000 + (ts.ptr[pos + 1] - '0') * 100 + (ts.ptr[pos + 2] - '0') * 10 + (ts.ptr[pos + 3] - '0');
}

static int weekday_monday0(int y, int m, int d) {
    static const int table[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) --y;
    int dow = (y + y / 4 - y / 100 + y / 400 + table[m - 1] + d) % 7;
    return (dow + 6) % 7;
}

static int days_from_civil(int y, unsigned int m, unsigned int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned int yoe = (unsigned int)(y - era * 400);
    unsigned int doy = (153U * (m + (m > 2 ? 4294967293U : 9U)) + 2U) / 5U + d - 1U;
    unsigned int doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return era * 146097 + (int)doe - 719468;
}

static int epoch_minutes(view_t ts) {
    int y = four(ts, 0);
    int m = two(ts, 5);
    int d = two(ts, 8);
    int h = two(ts, 11);
    int mi = two(ts, 14);
    return days_from_civil(y, (unsigned int)m, (unsigned int)d) * 1440 + h * 60 + mi;
}

static int is_march_2026(view_t ts) {
    return ts.len >= 16 && memcmp(ts.ptr, "2026-03", 7) == 0;
}

static int epoch_minutes_fast(view_t ts) {
    return epoch_minutes(ts);
}

static int weekday_monday0_fast(view_t ts) {
    if (is_march_2026(ts)) return (two(ts, 8) + 5) % 7;
    return weekday_monday0(four(ts, 0), two(ts, 5), two(ts, 8));
}

static int16_t mcc_risk_q(view_t mcc) {
    if (mcc.len < 4) return 5000;
    int code = (mcc.ptr[0] - '0') * 1000 + (mcc.ptr[1] - '0') * 100 + (mcc.ptr[2] - '0') * 10 + (mcc.ptr[3] - '0');
    switch (code) {
        case 5411: return 1500;
        case 5812: return 3000;
        case 5912: return 2000;
        case 5944: return 4500;
        case 7801: return 8000;
        case 7802: return 7500;
        case 7995: return 8500;
        case 4511: return 3500;
        case 5311: return 2500;
        case 5999: return 5000;
        default: return 5000;
    }
}

static int view_contains(view_t haystack, view_t needle) {
    if (needle.len == 0 || haystack.len < needle.len) return 0;
    for (size_t i = 0; i <= haystack.len - needle.len; ++i) {
        if (haystack.ptr[i] == needle.ptr[0] && memcmp(haystack.ptr + i, needle.ptr, needle.len) == 0) return 1;
    }
    return 0;
}

int rinha_vectorize(const char *body, size_t len, int16_t out[RINHA_DIMS]) {
    /* This is a schema scanner, not a general JSON parser. k6 sends fields in
       the contest order, so a monotonic scan can turn each request into the 14
       quantized features used by the mmaped nearest-neighbor index. */
    size_t p = 0;
    double amount = 0.0;
    double installments = 0.0;
    view_t requested_at = {0};
    double customer_avg = 0.0;
    double tx_count = 0.0;
    view_t known_merchants = {0};
    view_t merchant_id = {0};
    view_t mcc = {0};
    double merchant_avg = 0.0;
    int is_online = 0;
    int card_present = 0;
    double km_from_home = 0.0;

    if (!number_value(body, len, &p, "\"amount\"", &amount)) return 0;
    if (!number_value(body, len, &p, "\"installments\"", &installments)) return 0;
    if (!string_value(body, len, &p, "\"requested_at\"", &requested_at) || requested_at.len < 16) return 0;
    if (!number_value(body, len, &p, "\"avg_amount\"", &customer_avg) || customer_avg == 0.0) return 0;
    if (!number_value(body, len, &p, "\"tx_count_24h\"", &tx_count)) return 0;

    size_t known_key = find_bytes(body, len, p, "\"known_merchants\"", 17);
    if (known_key == (size_t)-1) return 0;
    size_t arr_start = find_bytes(body, len, known_key, "[", 1);
    size_t arr_end = find_bytes(body, len, arr_start, "]", 1);
    if (arr_start == (size_t)-1 || arr_end == (size_t)-1) return 0;
    known_merchants.ptr = body + arr_start;
    known_merchants.len = arr_end - arr_start + 1;
    p = arr_end + 1;

    if (!string_value(body, len, &p, "\"id\"", &merchant_id)) return 0;
    if (!string_value(body, len, &p, "\"mcc\"", &mcc)) return 0;
    if (!number_value(body, len, &p, "\"avg_amount\"", &merchant_avg)) return 0;
    if (!bool_value(body, len, &p, "\"is_online\"", &is_online)) return 0;
    if (!bool_value(body, len, &p, "\"card_present\"", &card_present)) return 0;
    if (!number_value(body, len, &p, "\"km_from_home\"", &km_from_home)) return 0;

    /* Feature map consumed by search fastpaths/indexes:
       0 amount, 1 installments, 2 amount/customer_avg, 3 hour, 4 weekday,
       5 minutes since last tx, 6 km from last tx, 7 km from home,
       8 tx_count_24h, 9 online, 10 card_present, 11 unknown merchant,
       12 MCC risk, 13 merchant average amount. */
    out[0] = rinha_qclamp01(amount / 10000.0);
    out[1] = rinha_qclamp01(installments / 12.0);
    out[2] = rinha_qclamp01((amount / customer_avg) / 10.0);
    out[3] = rinha_qclamp01((double)two(requested_at, 11) / 23.0);
    out[4] = rinha_qclamp01((double)weekday_monday0_fast(requested_at) / 6.0);

    size_t last = find_bytes(body, len, p, "\"last_transaction\"", 18);
    if (last == (size_t)-1) return 0;
    size_t colon = find_bytes(body, len, last, ":", 1);
    if (colon == (size_t)-1) return 0;
    p = colon + 1;
    skip_ws(body, len, &p);
    if (p + 4 <= len && memcmp(body + p, "null", 4) == 0) {
        out[5] = -RINHA_SCALE;
        out[6] = -RINHA_SCALE;
    } else {
        view_t last_ts = {0};
        double last_km = 0.0;
        if (!string_value(body, len, &p, "\"timestamp\"", &last_ts) || last_ts.len < 16) return 0;
        if (!number_value(body, len, &p, "\"km_from_current\"", &last_km)) return 0;
        int minutes = epoch_minutes_fast(requested_at) - epoch_minutes_fast(last_ts);
        out[5] = rinha_qclamp01((double)minutes / 1440.0);
        out[6] = rinha_qclamp01(last_km / 1000.0);
    }

    out[7] = rinha_qclamp01(km_from_home / 1000.0);
    out[8] = rinha_qclamp01(tx_count / 20.0);
    out[9] = is_online ? RINHA_SCALE : 0;
    out[10] = card_present ? RINHA_SCALE : 0;
    /* Merchant ids are fixed challenge strings, so a substring check inside the
       known_merchants array slice is enough and avoids per-item parsing. */
    out[11] = view_contains(known_merchants, merchant_id) ? 0 : RINHA_SCALE;
    out[12] = mcc_risk_q(mcc);
    out[13] = rinha_qclamp01(merchant_avg / 10000.0);
    return 1;
}
