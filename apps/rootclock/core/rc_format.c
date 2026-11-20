/* rootclock -- time formatting. See rc_format.h.
 *
 * Hand-rolled rather than snprintf: no printf code on the display CPU's hot
 * path, and no format-truncation warnings to reason about. The longest output
 * is rc_fmt_hms(UINT32_MAX) = "1193:02:47" (10 chars), under RC_FMT_MAX. */
#include "rc_format.h"

/* Unpadded decimal. Returns chars written (no NUL). */
static size_t rc_put_uint(char *out, uint32_t v) {
    char tmp[10];
    size_t n = 0u;
    do {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u);
    for (size_t i = 0u; i < n; i++) {
        out[i] = tmp[n - 1u - i];
    }
    return n;
}

/* Exactly two digits, v < 100. */
static size_t rc_put_2(char *out, uint32_t v) {
    out[0] = (char)('0' + (v / 10u) % 10u);
    out[1] = (char)('0' + v % 10u);
    return 2u;
}

size_t rc_fmt_turn(uint32_t ms, char *out) {
    uint32_t s = ms / 1000u;
    uint32_t m = s / 60u;
    if (m >= 100u) {
        return rc_fmt_hms(ms, out);
    }
    size_t n = rc_put_2(out, m);
    out[n++] = ':';
    n += rc_put_2(out + n, s % 60u);
    out[n] = '\0';
    return n;
}

size_t rc_fmt_hms(uint32_t ms, char *out) {
    uint32_t s = ms / 1000u;
    size_t n = rc_put_uint(out, s / 3600u);
    out[n++] = ':';
    n += rc_put_2(out + n, (s / 60u) % 60u);
    out[n++] = ':';
    n += rc_put_2(out + n, s % 60u);
    out[n] = '\0';
    return n;
}

size_t rc_fmt_limit(uint32_t ms, char *out) {
    if (ms == 0u) {
        out[0] = 'o';
        out[1] = 'f';
        out[2] = 'f';
        out[3] = '\0';
        return 3u;
    }
    uint32_t s = ms / 1000u;
    size_t n = rc_put_uint(out, s / 60u);
    out[n++] = ':';
    n += rc_put_2(out + n, s % 60u);
    out[n] = '\0';
    return n;
}
