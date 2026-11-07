/* The time modes: stopwatch, timer, daily alarm and hourly chime. The timer, the alarm and the chime
 * keep working whatever mode shows (time_tick). */
#include "mode_kit.h"

#include <stdlib.h>
#include <time.h>

#include "esp_timer.h"

#include "ledfx.h"
#include "assets/images.h"
#include "netdata.h"
#include "tzlite.h"

/* ============================================================== stopwatch */

static bool s_cr_run;
static int64_t s_cr_start_us, s_cr_acc_us;
static int64_t s_laps[5];             /* the stopwatch time at the last laps, newest first */
static int s_lap_n;

static int64_t chrono_us(void)
{
    return s_cr_acc_us + (s_cr_run ? esp_timer_get_time() - s_cr_start_us : 0);
}

static void chrono_key(const btn_event_t *e)
{
    if (e->kind == BTN_SHORT && e->key == KEY_POWER) {
        if (s_cr_run) s_cr_acc_us = chrono_us();
        else s_cr_start_us = esp_timer_get_time();
        s_cr_run = !s_cr_run;
    } else if (e->kind == BTN_DOWN && e->key == KEY_LEFT && !e->repeat) {
        if (s_cr_run) {
            memmove(s_laps + 1, s_laps, 4 * sizeof s_laps[0]);
            s_laps[0] = chrono_us();
            s_lap_n++;
        } else {
            s_cr_acc_us = 0;
            s_lap_n = 0;
        }
    }
}

static void big_pair(hud_fb_t *fb, int v, const char *caption, hud_color_t col)
{
    char t[8];
    snprintf(t, sizeof t, "%02d", v);
    hud_text(fb, 64, 80, t, &font_o48, col, "ms", 0);
    hud_text(fb, 64, 96, caption, &font_b8, C_LEGIBLE, "mm", 2);
}

static void chrono_draw(hud_fb_t *fb, int s)
{
    int64_t us = chrono_us();
    int64_t cs = us / 10000;
    char t[24];
    hud_color_t col = s_cr_run ? C_INK : (us ? C_INK2 : C_CAPTION);
    switch (s) {
    case 0:
        mk_title(fb, "STOPWATCH");
        hud_text(fb, 64, 56, s_cr_run ? "RUN" : us ? "STOP" : "READY", &font_o16, s_cr_run ? C_SUCCESS : C_INK2, "mm", 1);
        if (cs >= 360000) { snprintf(t, sizeof t, "+%dH", (int)(cs / 360000)); hud_text(fb, 64, 78, t, &font_b9, C_LEGIBLE, "mm", 1); }
        mk_softkey(fb, "LIGHT");
        break;
    case 1: big_pair(fb, (int)(cs / 6000 % 60), "MIN", col); mk_softkey(fb, s_cr_run ? "LAP" : "RESET"); break;
    case 2: big_pair(fb, (int)(cs / 100 % 60), "SEC", col); mk_softkey(fb, s_cr_run ? "STOP" : "START"); break;
    case 3: big_pair(fb, (int)(cs % 100), "1/100", col); break;
    case 4:
        mk_title(fb, "LAPS");
        if (!s_lap_n) hud_text(fb, 64, 60, "\xE2\x80\x94", &font_b12, C_CAPTION, "mm", 0);
        for (int i = 0; i < 4 && i < s_lap_n; i++) {
            int y = 34 + i * 18;
            snprintf(t, sizeof t, "%02d", s_lap_n - i);
            hud_text(fb, 16, y, t, &font_b8, C_LEGIBLE, "lm", 1);
            int64_t prev = i + 1 < s_lap_n ? s_laps[i + 1] : 0;
            mk_mmss(t, sizeof t, s_laps[i] - prev, true);
            hud_text(fb, 112, y, t, &font_sb9, i == 0 ? C_INK : C_INK2, "rm", 0);
        }
        mk_softkey(fb, "MODE");
        break;
    }
}

const mode_def_t MODE_DEF_CHRONO = { .name = "STOPWATCH", .key = chrono_key, .draw = chrono_draw };

/* ================================================================== timer */

#define s_tm_set_min ((int)g_cfg.timer_min)   /* the set length (saved) */
static bool s_tm_run, s_tm_armed;     /* armed: started at least once since the last reset */
static int64_t s_tm_left_us, s_tm_end_us;

static int64_t timer_left(void)
{
    if (!s_tm_armed) return (int64_t)s_tm_set_min * 60 * 1000000;
    return s_tm_run ? s_tm_end_us - esp_timer_get_time() : s_tm_left_us;
}

static void timer_reset(void)
{
    s_tm_run = false;
    s_tm_armed = false;
    s_tm_left_us = (int64_t)s_tm_set_min * 60 * 1000000;
}

static void timer_key(const btn_event_t *e)
{
    if (e->kind == BTN_SHORT && e->key == KEY_POWER) {
        if (s_tm_run) {
            s_tm_left_us = timer_left();
            s_tm_run = false;
        } else if (timer_left() > 0) {
            s_tm_end_us = esp_timer_get_time() + timer_left();
            s_tm_run = true;
            s_tm_armed = true;
        }
    } else if (e->kind == BTN_DOWN && (e->key == KEY_LEFT || e->key == KEY_RIGHT)) {
        if (!s_tm_armed) {
            g_cfg.timer_min = (uint16_t)clampi(g_cfg.timer_min + (e->key == KEY_RIGHT ? 1 : -1), 1, 180);
            mk_save_later();
            timer_reset();
        } else if (!s_tm_run && e->key == KEY_LEFT && !e->repeat) {
            timer_reset();
        }
    }
}

static void timer_leds(void)
{
    if (!s_tm_armed) { ledfx_scene(SCENE_NONE, 0, 0); return; }
    int64_t total = (int64_t)s_tm_set_min * 60 * 1000000;
    ledfx_scene(SCENE_PROGRESS, total ? (float)timer_left() / total : 0, s_tm_run);
}

static void timer_draw(hud_fb_t *fb, int s)
{
    int64_t left = timer_left();
    int64_t total = (int64_t)s_tm_set_min * 60 * 1000000;
    int64_t secs = (left + 999999) / 1000000;               /* count down in whole seconds, rounding up */
    char t[24];
    hud_color_t col = s_tm_run ? C_INK : s_tm_armed ? C_INK2 : C_INK;
    switch (s) {
    case 0:
        mk_title(fb, "TIMER");
        hud_text(fb, 64, 52, s_tm_run ? "RUN" : s_tm_armed ? "PAUSE" : "SET", &font_o16, s_tm_run ? C_SUCCESS : C_INK2, "mm", 1);
        snprintf(t, sizeof t, "%d MIN", s_tm_set_min);
        hud_text(fb, 64, 76, t, &font_b9, C_LEGIBLE, "mm", 1);
        mk_softkey(fb, "LIGHT");
        break;
    case 1:
        snprintf(t, sizeof t, "%02d", (int)(secs / 60));
        hud_text(fb, 64, 80, t, &font_o48, col, "ms", 0);
        hud_text(fb, 64, 96, "MIN", &font_b8, C_LEGIBLE, "mm", 2);
        mk_softkey(fb, !s_tm_armed ? "- 1 MIN" : !s_tm_run ? "RESET" : "");
        break;
    case 2:
        snprintf(t, sizeof t, "%02d", (int)(secs % 60));
        hud_text(fb, 64, 80, t, &font_o48, col, "ms", 0);
        hud_text(fb, 64, 96, "SEC", &font_b8, C_LEGIBLE, "mm", 2);
        mk_softkey(fb, s_tm_run ? "PAUSE" : "START");
        break;
    case 3: {
        float frac = total ? clamp01((float)left / total) : 0;
        hud_bezel(fb, 64, 60, 38, 10.0f, 0, C_HUD_DIM, C_HUD);
        hud_dial(fb, 64, 60, 34, frac, 25, 6, 2.0f, false, -1, C_ACCENT);
        snprintf(t, sizeof t, "%.0f%%", frac * 100);
        hud_text(fb, 64, 66, t, &font_o16, C_INK, "ms", 0);
        mk_softkey(fb, !s_tm_armed ? "+ 1 MIN" : "");
        break;
    }
    case 4:
        mk_title(fb, "ENDS");
        if (s_tm_run && netdata_time_valid()) {
            time_t end = time(NULL) + (time_t)(left / 1000000);
            struct tm tm;
            localtime_r(&end, &tm);
            strftime(t, sizeof t, g_cfg.hour24 ? "%H:%M" : "%I:%M", &tm);
            hud_text(fb, 64, 66, t, &font_o22, C_INK, "ms", 0);
        } else {
            hud_text(fb, 64, 66, "--:--", &font_o22, C_CAPTION, "ms", 0);
        }
        mk_softkey(fb, "MODE");
        break;
    }
}

const mode_def_t MODE_DEF_TIMER = { .name = "TIMER", .key = timer_key, .leds = timer_leds, .draw = timer_draw };

/* ================================================================== alarm */

/* Editing, Casio-style: power steps hour -> minute -> days -> done (saved); MODE cancels. */
enum { ED_NONE, ED_HOUR, ED_MIN, ED_DAYS };
static int s_ed;
static tg_settings_t s_al;            /* the alarm being edited */
static int s_last_min = -1, s_last_chime_hour = -1;
static uint32_t s_snooze_at;
static const uint8_t DAY_SETS[] = { 0x7F, 0x1F, 0x60 };    /* every day, Monday-Friday, weekends */
static const char *DAY_SET_NAMES[] = { "EVERY DAY", "WEEKDAYS", "WEEKENDS" };

static void alarm_parse(const char *hhmm, int *h, int *m)
{
    *h = 7;
    *m = 0;
    sscanf(hhmm, "%d:%d", h, m);
}

static void alarm_snooze(void)
{
    s_snooze_at = g_ms + 5 * 60 * 1000;
    mk_toast("SNOOZE", "5 MIN", NAN);
}

static void timer_rang(void) { timer_reset(); }

void time_tick(void)
{
    /* the timer ran out */
    if (s_tm_run && timer_left() <= 0) {
        s_tm_run = false;
        s_tm_left_us = 0;
        mk_ring("TIMER", timer_rang, NULL);
    }
    if (s_snooze_at && (int32_t)(g_ms - s_snooze_at) >= 0) {
        s_snooze_at = 0;
        mk_ring("ALARM", NULL, alarm_snooze);
    }
    if (!netdata_time_valid()) return;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_min == s_last_min) return;              /* the rest happens once a minute */
    bool first = s_last_min < 0;
    s_last_min = tm.tm_min;
    if (first) return;                                /* not on the minute the device starts */
    int ah, am;
    alarm_parse(g_cfg.alarm_time, &ah, &am);
    int day = (tm.tm_wday + 6) % 7;                   /* 0 = Monday */
    if (g_cfg.alarm_on && tm.tm_hour == ah && tm.tm_min == am && (g_cfg.alarm_days >> day & 1) && !mk_ringing()) {
        mk_ring("ALARM", NULL, alarm_snooze);
    } else if (g_cfg.chime && tm.tm_min == 0 && tm.tm_hour != s_last_chime_hour && !mk_ringing() &&
               !(g_cfg.quiet_night && settings_is_night(&g_cfg, true))) {
        s_last_chime_hour = tm.tm_hour;
        mk_beep(BZ_CHIME);
    }
}

static void alarm_key(const btn_event_t *e)
{
    int h, m;
    if (s_ed == ED_NONE) {
        if (e->kind == BTN_DOWN && !e->repeat && e->key == KEY_LEFT) {
            tg_settings_t c = g_cfg;
            c.alarm_on = !c.alarm_on;
            mk_save(&c);
        } else if (e->kind == BTN_DOWN && !e->repeat && e->key == KEY_RIGHT) {
            tg_settings_t c = g_cfg;
            c.chime = !c.chime;
            mk_save(&c);
            if (c.chime) mk_beep(BZ_CHIME);
        } else if (e->kind == BTN_SHORT && e->key == KEY_POWER) {
            s_al = g_cfg;
            s_ed = ED_HOUR;
        }
        return;
    }
    if (e->kind == BTN_SHORT && e->key == KEY_POWER) {
        if (++s_ed > ED_DAYS) {
            s_al.alarm_on = true;                     /* setting a time means you want it */
            mk_save(&s_al);
            s_ed = ED_NONE;
            mk_beep(BZ_UP);
        }
    } else if (e->kind == BTN_DOWN && (e->key == KEY_LEFT || e->key == KEY_RIGHT)) {
        int d = e->key == KEY_RIGHT ? 1 : -1;
        alarm_parse(s_al.alarm_time, &h, &m);
        if (s_ed == ED_HOUR) h = (h + d + 24) % 24;
        else if (s_ed == ED_MIN) m = (m + d + 60) % 60;
        else if (!e->repeat) {
            int k = 0;
            for (int i = 0; i < 3; i++) if (s_al.alarm_days == DAY_SETS[i]) k = i;
            s_al.alarm_days = DAY_SETS[(k + d + 3) % 3];
        }
        snprintf(s_al.alarm_time, sizeof s_al.alarm_time, "%02d:%02d", h, m);
    }
}

static bool alarm_escape(void)
{
    if (s_ed == ED_NONE) return false;
    s_ed = ED_NONE;
    return true;
}

static void alarm_leave(void) { s_ed = ED_NONE; }

/* minutes until the alarm next rings, or -1 */
static int alarm_in(const tg_settings_t *c)
{
    if (!c->alarm_on || !c->alarm_days || !netdata_time_valid()) return -1;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    int ah, am;
    alarm_parse(c->alarm_time, &ah, &am);
    int nowm = tm.tm_hour * 60 + tm.tm_min;
    for (int k = 0; k <= 7; k++) {
        int day = (tm.tm_wday + 6 + k) % 7;
        int at = k * 1440 + ah * 60 + am;
        if ((c->alarm_days >> day & 1) && at > nowm) return at - nowm;
    }
    return -1;
}

static void alarm_draw(hud_fb_t *fb, int s)
{
    const tg_settings_t *c = s_ed ? &s_al : &g_cfg;
    int h, m;
    alarm_parse(c->alarm_time, &h, &m);
    char t[24];
    bool hide = !mk_blink();
    switch (s) {
    case 0:
        mk_title(fb, "ALARM");
        hud_text(fb, 64, 60, c->alarm_on ? "ON" : "OFF", &font_o22, c->alarm_on ? C_SUCCESS : C_CAPTION, "ms", 0);
        hud_text(fb, 64, 80, s_ed ? "SETTING" : "DAILY", &font_b8, C_LEGIBLE, "mm", 2);
        mk_softkey(fb, "LIGHT");
        break;
    case 1:
    case 2: {
        bool hour = s == 1;
        int v = hour ? (g_cfg.hour24 ? h : (h % 12 ? h % 12 : 12)) : m;
        bool edit = s_ed == (hour ? ED_HOUR : ED_MIN);
        snprintf(t, sizeof t, "%02d", v);
        if (!(edit && hide)) hud_text(fb, 64, 80, t, &font_o48, edit ? C_INK : c->alarm_on ? C_INK2 : C_CAPTION, "ms", 0);
        if (hour && !g_cfg.hour24) hud_text(fb, 64, 96, h < 12 ? "HOUR \xC2\xB7 AM" : "HOUR \xC2\xB7 PM", &font_b8, C_LEGIBLE, "mm", 1);
        else hud_text(fb, 64, 96, hour ? "HOUR" : "MIN", &font_b8, C_LEGIBLE, "mm", 2);
        if (edit) {
            mk_arrow(fb, 10, 48, -1, 13, C_ACCENT);
            mk_arrow(fb, 117, 48, 1, 13, C_ACCENT);
        }
        if (hour) mk_softkey(fb, s_ed ? "-" : c->alarm_on ? "ALARM OFF" : "ALARM ON");
        else mk_softkey(fb, s_ed ? (s_ed == ED_DAYS ? "SAVE" : "NEXT") : "SET");
        break;
    }
    case 3: {
        mk_title(fb, "DAYS");
        static const char *D[7] = { "MO", "TU", "WE", "TH", "FR", "SA", "SU" };
        bool edit = s_ed == ED_DAYS;
        for (int i = 0; i < 7; i++) {
            int x = 18 + (i % 4) * 30, y = i < 4 ? 44 : 64;
            if (i >= 4) x += 15;
            bool on = c->alarm_days >> i & 1;
            hud_text(fb, x, y, D[i], &font_b9, on ? (edit && hide ? C_OFF : C_INK) : C_HUD_DIM, "mm", 1);
        }
        int k = -1;
        for (int i = 0; i < 3; i++) if (c->alarm_days == DAY_SETS[i]) k = i;
        hud_text(fb, 64, 88, k >= 0 ? DAY_SET_NAMES[k] : "CUSTOM", &font_b8, C_LEGIBLE, "mm", 1);
        mk_softkey(fb, s_ed ? "+" : g_cfg.chime ? "CHIME OFF" : "CHIME ON");
        break;
    }
    case 4: {
        mk_title(fb, "NEXT");
        int in = alarm_in(c);
        if (in < 0) snprintf(t, sizeof t, "\xE2\x80\x94");
        else snprintf(t, sizeof t, "%dH %02dM", in / 60, in % 60);
        hud_text(fb, 64, 58, t, &font_o16, in < 0 ? C_CAPTION : C_INK, "mm", 0);
        hud_text(fb, 64, 84, g_cfg.chime ? "HOURLY CHIME ON" : "HOURLY CHIME OFF", &font_b7, g_cfg.chime ? C_LEGIBLE : C_CAPTION, "mm", 1);
        mk_softkey(fb, s_ed ? "CANCEL" : "MODE");
        break;
    }
    }
}

const mode_def_t MODE_DEF_ALARM = {
    .name = "ALARM", .leave = alarm_leave, .key = alarm_key, .escape = alarm_escape, .draw = alarm_draw,
};

/* ================================================================== world */

/* Home (the settings' time zone) in the middle, the two other places either side, UTC on the left,
 * and on the right who is awake when: one 24-hour row per place on home's time axis. Left / right
 * shift every clock by half an hour (planning a call); power goes back to now. */
static int s_wc_shift;                /* minutes */

static void wc_key(const btn_event_t *e)
{
    if (e->kind == BTN_DOWN && (e->key == KEY_LEFT || e->key == KEY_RIGHT)) {
        s_wc_shift = clampi(s_wc_shift + (e->key == KEY_RIGHT ? 30 : -30), -24 * 60, 24 * 60);
    } else if (e->kind == BTN_SHORT && e->key == KEY_POWER) {
        s_wc_shift = 0;
    }
}

static void wc_leave(void) { s_wc_shift = 0; }

/* place 0 = home, 1 and 2 = the settings' world clocks, 3 = UTC */
static int wc_local(int place, time_t utc, struct tm *tm)
{
    tz_t z;
    const char *posix = place == 0 ? g_cfg.tz : place == 3 ? "UTC0" : g_cfg.world_tz[place - 1];
    if (!tz_parse(posix, &z)) tz_parse("UTC0", &z);
    return tz_local(&z, utc, tm);
}

static void wc_offset(char *out, size_t n, int secs)
{
    if (secs == 0) { snprintf(out, n, "SAME AS HOME"); return; }
    int m = abs(secs) / 60;
    if (m % 60) snprintf(out, n, "%c%d:%02d H", secs < 0 ? '-' : '+', m / 60, m % 60);
    else snprintf(out, n, "%c%d H", secs < 0 ? '-' : '+', m / 60);
}

static void wc_clock(hud_fb_t *fb, int place, const char *name, time_t utc, int home_off, bool home)
{
    struct tm tm;
    int off = wc_local(place, utc, &tm);
    char t[24];
    /* home is on the middle screen, whose header is the clock (modes_draw); its footer says HOME */
    if (!home) hud_header(fb, &font_b9, name, 12, 2, 0, 0, C_LEGIBLE, C_HUD_DIM);
    bool day = tm.tm_hour >= 6 && tm.tm_hour < 18;
    hud_image(fb, day ? &img_icon_clear_day : &img_icon_clear_night, 64 - 11, 23);
    strftime(t, sizeof t, g_cfg.hour24 ? "%H:%M" : "%I:%M", &tm);
    hud_text(fb, 64, 74, t, &font_o28, s_wc_shift ? C_ACCENT : C_INK, "ms", 0);
    strftime(t, sizeof t, "%a %d %b", &tm);
    for (char *c = t; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    if (!g_cfg.hour24) {
        size_t k = strlen(t);
        snprintf(t + k, sizeof t - k, tm.tm_hour < 12 ? " \xC2\xB7 AM" : " \xC2\xB7 PM");
    }
    hud_text(fb, 64, 87, t, &font_b8, C_LEGIBLE, "mm", 1);
    if (place == 3) snprintf(t, sizeof t, "UNIVERSAL");
    else if (home) snprintf(t, sizeof t, "HOME");
    else wc_offset(t, sizeof t, off - home_off);
    hud_text(fb, 64, 101, t, &font_b7, C_CAPTION, "mm", 1);
}

static void wc_draw(hud_fb_t *fb, int s)
{
    if (!netdata_time_valid()) {
        mk_title(fb, "WORLD");
        hud_text(fb, 64, 64, "NO CLOCK YET", &font_b8, C_CAPTION, "mm", 1);
        return;
    }
    time_t utc = time(NULL) + s_wc_shift * 60;
    struct tm home;
    int home_off = wc_local(0, utc, &home);
    char t[24];
    switch (s) {
    case 0: wc_clock(fb, 3, "UTC", utc, home_off, false); mk_softkey(fb, "LIGHT"); break;
    case 1: wc_clock(fb, 1, g_cfg.world_name[0], utc, home_off, false); mk_softkey(fb, "- 30 MIN"); break;
    case 2:
        wc_clock(fb, 0, "HOME", utc, home_off, true);
        if (s_wc_shift) {
            snprintf(t, sizeof t, "NOW %+d:%02d", s_wc_shift / 60, abs(s_wc_shift) % 60);
            mk_softkey(fb, t);
        } else {
            mk_softkey(fb, "NOW");
        }
        break;
    case 3: wc_clock(fb, 2, g_cfg.world_name[1], utc, home_off, false); mk_softkey(fb, "+ 30 MIN"); break;
    case 4: {
        mk_title(fb, "AWAKE");
        /* one row per place on home's 24 hours, its name above it: dim 7..22 h local (awake),
         * bright 9..17 h (work); the white line is now */
        const int x0 = 10, x1 = 118, ys[3] = { 34, 56, 78 };
        const char *labels[3] = { "HOME", g_cfg.world_name[0], g_cfg.world_name[1] };
        time_t midnight = utc - (home.tm_hour * 3600 + home.tm_min * 60 + home.tm_sec);
        for (int r = 0; r < 3; r++) {
            hud_text(fb, x0, ys[r] - 5, labels[r], &font_b7, r == 0 ? C_LEGIBLE : C_CAPTION, "lm", 1);
            for (int h = 0; h < 24; h++) {
                struct tm lt;
                wc_local(r, midnight + h * 3600 + 1800, &lt);
                int sx = x0 + (x1 - x0) * h / 24, ex = x0 + (x1 - x0) * (h + 1) / 24 - 1;
                hud_color_t c = lt.tm_hour >= 9 && lt.tm_hour < 17 ? C_ACCENT : lt.tm_hour >= 7 && lt.tm_hour < 22 ? C_HUD_DIM : C_OFF;
                hud_rect(fb, sx, ys[r], ex, ys[r] + 6, c);
            }
        }
        int nx = x0 + (x1 - x0) * (home.tm_hour * 60 + home.tm_min) / 1440;
        hud_rect(fb, nx, ys[0] - 2, nx + 1, ys[2] + 9, C_INK);
        hud_text(fb, x0, 94, "0", &font_b7, C_CAPTION, "lm", 0);
        hud_text(fb, (x0 + x1) / 2, 94, "12", &font_b7, C_CAPTION, "mm", 0);
        hud_text(fb, x1, 94, "24", &font_b7, C_CAPTION, "rm", 0);
        hud_text(fb, 64, 105, "WORK 9-17 \xC2\xB7 AWAKE 7-22", &font_b7, C_CAPTION, "mm", 0);
        mk_softkey(fb, "MODE");
        break;
    }
    }
}

const mode_def_t MODE_DEF_WORLD = {
    .name = "WORLD", .leave = wc_leave, .key = wc_key, .draw = wc_draw,
};
