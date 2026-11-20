/* rootclock -- time formatting for the screens. Pure.
 *
 * Seconds are floored (1999 ms shows as 0:01). Output is NUL-terminated;
 * `out` must hold at least RC_FMT_MAX bytes. */
#ifndef RC_FORMAT_H
#define RC_FORMAT_H
#include "rc_types.h"

#define RC_FMT_MAX 12u

/* Turn timer: "MM:SS" (zero-padded minutes) below 100 minutes, otherwise
 * "H:MM:SS" (hours unpadded, may exceed 9). Returns the string length. */
size_t rc_fmt_turn(uint32_t ms, char *out);

/* Totals and global: always "H:MM:SS" (hours unpadded). Returns length. */
size_t rc_fmt_hms(uint32_t ms, char *out);

/* Limit labels: "off" for 0, else "M:SS" (e.g. "0:30", "10:00").
 * Returns length. */
size_t rc_fmt_limit(uint32_t ms, char *out);

#endif
