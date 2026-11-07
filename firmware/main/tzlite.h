#pragma once
/*
 * Another place's local time from a POSIX TZ string, without touching the process time zone (which
 * the clock, the night window and the alarm all read). Handles the usual forms:
 *   "IST-5:30"                      a fixed offset (India)
 *   "CET-1CEST,M3.5.0,M10.5.0/3"    with daylight saving by Mm.w.d rules (Central Europe)
 *   "<+07>-7"                       a quoted name
 * Other rule forms (Jn, n) are treated as no daylight saving.
 */
#include <stdbool.h>
#include <time.h>

typedef struct {
    int std_off, dst_off;            /* seconds east of UTC */
    bool has_dst;
    struct { int mon, week, wday, secs; } start, end;   /* Mm.w.d/time: local time of the change */
} tz_t;

bool tz_parse(const char *posix, tz_t *out);
/* The offset (seconds east of UTC) in force at `utc`, and the local broken-down time. */
int tz_local(const tz_t *z, time_t utc, struct tm *out);
