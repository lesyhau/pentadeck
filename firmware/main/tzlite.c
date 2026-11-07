#include "tzlite.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* a zone name: letters, or anything inside <...> */
static const char *skip_name(const char *p)
{
    if (*p == '<') {
        const char *e = strchr(p, '>');
        return e ? e + 1 : NULL;
    }
    const char *s = p;
    while (isalpha((unsigned char)*p)) p++;
    return p - s >= 3 ? p : NULL;
}

/* [+|-]hh[:mm[:ss]] -> seconds */
static const char *parse_hms(const char *p, int *secs)
{
    int sign = 1;
    if (*p == '+' || *p == '-') sign = *p++ == '-' ? -1 : 1;
    if (!isdigit((unsigned char)*p)) return NULL;
    int h = (int)strtol(p, (char **)&p, 10), m = 0, s = 0;
    if (*p == ':') { p++; m = (int)strtol(p, (char **)&p, 10); }
    if (*p == ':') { p++; s = (int)strtol(p, (char **)&p, 10); }
    *secs = sign * (h * 3600 + m * 60 + s);
    return p;
}

static const char *parse_rule(const char *p, int *mon, int *week, int *wday, int *secs)
{
    if (*p != 'M') return NULL;
    p++;
    *mon = (int)strtol(p, (char **)&p, 10);
    if (*p++ != '.') return NULL;
    *week = (int)strtol(p, (char **)&p, 10);
    if (*p++ != '.') return NULL;
    *wday = (int)strtol(p, (char **)&p, 10);
    *secs = 2 * 3600;
    if (*p == '/') p = parse_hms(p + 1, secs);
    if (!p || *mon < 1 || *mon > 12 || *week < 1 || *week > 5 || *wday < 0 || *wday > 6) return NULL;
    return p;
}

bool tz_parse(const char *posix, tz_t *out)
{
    memset(out, 0, sizeof *out);
    const char *p = posix ? skip_name(posix) : NULL;
    int off;
    if (!p || !(p = parse_hms(p, &off))) return false;
    out->std_off = -off;                               /* POSIX offsets are west-positive */
    out->dst_off = out->std_off + 3600;
    if (!*p) return true;
    if (!(p = skip_name(p))) return false;
    if (*p && *p != ',') {
        if (!(p = parse_hms(p, &off))) return false;
        out->dst_off = -off;
    }
    if (*p != ',') return true;                       /* a DST name without rules: no switching */
    p = parse_rule(p + 1, &out->start.mon, &out->start.week, &out->start.wday, &out->start.secs);
    if (!p || *p != ',') return true;
    p = parse_rule(p + 1, &out->end.mon, &out->end.week, &out->end.wday, &out->end.secs);
    out->has_dst = p != NULL;
    return true;
}

/* days since 1970-01-01 of a civil date (Howard Hinnant's algorithm) */
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* the UTC instant of a rule's change in year y; `local_off` is the offset its time is given in */
static time_t rule_utc(int y, int mon, int week, int wday, int secs, int local_off)
{
    static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    long first = days_from_civil(y, mon, 1);
    int wd_first = (int)((first + 4) % 7);            /* 1970-01-01 was a Thursday */
    if (wd_first < 0) wd_first += 7;
    int day = 1 + (wday - wd_first + 7) % 7 + (week - 1) * 7;
    int len = mdays[mon - 1] + (mon == 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0));
    while (day > len) day -= 7;                       /* week 5 = the last one */
    return (time_t)(first + day - 1) * 86400 + secs - local_off;
}

int tz_local(const tz_t *z, time_t utc, struct tm *out)
{
    int off = z->std_off;
    if (z->has_dst) {
        struct tm g;
        time_t t = utc + z->std_off;
        gmtime_r(&t, &g);
        int y = g.tm_year + 1900;
        time_t on = rule_utc(y, z->start.mon, z->start.week, z->start.wday, z->start.secs, z->std_off);
        time_t off_at = rule_utc(y, z->end.mon, z->end.week, z->end.wday, z->end.secs, z->dst_off);
        bool dst = on < off_at ? (utc >= on && utc < off_at) : !(utc >= off_at && utc < on);   /* south: wraps */
        if (dst) off = z->dst_off;
    }
    time_t t = utc + off;
    gmtime_r(&t, out);
    return off;
}
