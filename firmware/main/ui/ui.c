/*
 * The cockpit on the five screens:
 *
 *   HOME LAB (CPU, RAM, disk, battery) | CPU (load, temp, RAM) | CLOCK (emblem, HH:MM,
 *   date, weather + humidity) | GPU (load, temps, VRAM) | AUDIO (16-band EQ + loudness)
 *
 * The layout is a setting. A ~30 fps loop snapshots the data and re-renders a panel only when its
 * content or its animation step has changed. hud.c draws straight into the display driver's frame
 * buffer (the panels' pixel format), which then goes out in one DMA transfer.
 */
#include "ui/ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "core/power.h"
#include "core/settings.h"
#include "drivers/display.h"
#include "drivers/mic.h"
#include "gfx/assets/fonts.h"
#include "gfx/assets/images.h"
#include "gfx/hud.h"
#include "modes/modes.h"
#include "net/netdata.h"
#include "net/stream.h"

static const char *TAG = "ui";

typedef enum { P_LAB, P_CPU, P_NAV, P_GPU, P_AUDIO, P_OFF } panel_kind_t;

typedef struct {
    int screen;
    panel_kind_t kind;
    uint32_t key;              /* hash of the content drawn last */
    int anim;                  /* animation step drawn last */
} panel_t;

static struct {
    const hud_font_t *b7, *b8, *b9, *b12, *sb9, *sb11, *o14, *o16, *o22, *o28;
} F;

static panel_t s_panel[TG_SCREENS];

/* data snapshot, refreshed every frame (UI task only) */
static host_stats_t s_h[TG_SOURCES];
static weather_t s_w;
static mic_snapshot_t s_m;
static struct tm s_tm;
static bool s_time_ok, s_hour24;
static uint32_t s_ms, s_settings_gen;

/* performance counters, one-second windows, reported by ui_stats_json() */
static struct {
    uint32_t refr[TG_SCREENS], refr_us[TG_SCREENS], busy_us;
    uint32_t out_refr[TG_SCREENS], out_us[TG_SCREENS], out_busy_ms;
    int64_t window_start;
} S;

/* audio meter state (smoothed at the frame rate) */
#define EQ_SEGS 18
static float s_eq[MIC_BANDS], s_peak[MIC_BANDS], s_level, s_pa = 1e-3f;   /* s_pa: A-weighted power, smoothed */

/* ------------------------------------------------------------- helpers */

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

static int live_phase(void)            /* 8 steps of 150 ms, like the PC cockpit's loops */
{
    return (int)((s_ms / 150) % 8);
}

static float clamp01(float v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

static void uptime_str(char *out, size_t n, uint32_t s)
{
    unsigned d = s / 86400, h = s % 86400 / 3600, m = s % 3600 / 60;
    if (d) snprintf(out, n, "UP %uD %uH", d, h);
    else snprintf(out, n, "UP %uH %uM", h, m);
}

static void temp_str(char *out, size_t n, float c)
{
    if (isnan(c)) snprintf(out, n, "\xE2\x80\x94");          /* — */
    else snprintf(out, n, "%.0f\xC2\xB0", c);                 /* 49° */
}

/* Status light: nothing while the source is live, a steady red dot on the given side when it's down. */
static char led_side(bool ok, char side)
{
    return ok ? 0 : side;
}

static void linking(hud_fb_t *l)
{
    hud_text(l, 64, 66, "LINKING\xE2\x80\xA6", F.b9, live_phase() < 4 ? C_CAPTION : C_OFF, "mm", 2);
}

/* ------------------------------------------------------------ CPU / GPU */

/* Load alert for the CPU and GPU panels: from 60 % the frame, header and dial turn orange and the
 * frame blinks (1.2 s period); from 80 % red, blinking twice as fast. A few percent of hysteresis
 * keeps a load hovering at a threshold from flickering between the two. */
typedef struct { hud_color_t frame, rule, title, lit, value; } theme_t;
enum { AL_CPU, AL_GPU };
static int s_alert[2];

static hud_color_t dim(hud_color_t c)          /* the same hue at ~30 % */
{
    return (((c >> 16 & 0xFF) * 3 / 10) << 16) | (((c >> 8 & 0xFF) * 3 / 10) << 8) | ((c & 0xFF) * 3 / 10);
}

static theme_t load_theme(int which, float load)
{
    int lv = s_alert[which];
    if (isnan(load)) lv = 0;
    else if (load >= 80) lv = 2;
    else if (load >= 60) lv = (lv == 2 && load >= 75) ? 2 : 1;
    else if (load < 55) lv = 0;
    else lv = lv ? 1 : 0;
    s_alert[which] = lv;

    int ph = live_phase();
    if (lv == 0) return (theme_t){ C_HUD, C_HUD_DIM, C_LEGIBLE, HUD_AUTO, C_INK };
    hud_color_t c = lv == 2 ? C_DANGER : C_ORANGE;
    bool on = lv == 2 ? (ph % 4) < 2 : ph < 4;
    return (theme_t){ on ? c : dim(c), on ? c : dim(c), c, c, c };
}

/* "AMD Ryzen 7 7800X3D 8-Core Processor" -> "RYZEN 7 7800X3D", "AMD Radeon RX 9060 XT" -> "RX 9060 XT" */
static void short_name(char *out, size_t n, const char *raw)
{
    char s[64];
    strlcpy(s, raw, sizeof s);
    static const char *drop[] = { "AMD Radeon ", "NVIDIA GeForce ", "Intel(R) Core(TM) ", "Intel(R) ", "AMD " };
    for (size_t i = 0; i < sizeof drop / sizeof drop[0]; i++) {
        size_t k = strlen(drop[i]);
        if (strncmp(s, drop[i], k) == 0) { memmove(s, s + k, strlen(s + k) + 1); break; }
    }
    static const char *cut[] = { " with ", " CPU", " Processor", "-Core" };
    for (size_t i = 0; i < sizeof cut / sizeof cut[0]; i++) {
        char *c = strstr(s, cut[i]);
        if (!c) continue;
        if (i == 3) while (c > s && c[-1] != ' ') c--;          /* "8-Core" -> drop the "8" too */
        *c = 0;
    }
    size_t len = strlen(s);
    while (len && s[len - 1] == ' ') s[--len] = 0;
    for (char *c = s; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    strlcpy(out, s, n);
}

/* The panel's title is the device's name: the widest letter spacing that fits, else a smaller
 * size, else cut with an ellipsis. */
static void name_header(hud_fb_t *l, const char *raw, const char *fallback, char led, const theme_t *th)
{
    char name[64], shown[64];
    short_name(name, sizeof name, raw);
    if (!name[0]) strlcpy(name, fallback, sizeof name);
    const int max_w = 88;
    if (hud_text_w(name, F.b9, 2) <= max_w) { hud_header(l, F.b9, name, 12, 2, led, C_DANGER, th->title, th->rule); return; }
    if (hud_text_w(name, F.b9, 1) <= max_w) { hud_header(l, F.b9, name, 12, 1, led, C_DANGER, th->title, th->rule); return; }
    hud_ellipsize(shown, sizeof shown, name, F.b8, max_w, 1);
    hud_header(l, F.b8, shown, 12, 1, led, C_DANGER, th->title, th->rule);
}

/* The big tick dial shared by the CPU and GPU panels: the load inside (no % sign), the hottest
 * sensor (when there is one) at the top inside the dial, the native temperature in the dial's
 * open bottom. No sweep animation: only the needle tip blinks. */
static void big_gauge(hud_fb_t *l, float load, float temp, float hot, const theme_t *th)
{
    int ph = live_phase();
    float frac = isnan(load) ? 0 : clamp01(load / 100.0f);
    hud_bezel(l, 64, 56, 40, 7.5f, 40.0f, C_HUD_DIM, th->frame);
    hud_dial(l, 64, 56, 36, frac, 27, 7, 2.2f, ph % 2 == 0, -1, th->lit);
    char v[8];
    if (isnan(load)) snprintf(v, sizeof v, "\xE2\x80\x94");
    else snprintf(v, sizeof v, "%.0f", load);
    hud_text(l, 64, 62, v, F.o22, th->value, "ms", 0);
    char t[12];
    if (!isnan(hot)) {
        temp_str(t, sizeof t, hot);
        int tw = hud_text_w(t, F.sb9, 0), cw = hud_text_w("HOT", F.b7, 1);
        int x = 64 - (cw + 3 + tw) / 2;
        hud_text(l, x, 36, "HOT", F.b7, C_CAPTION, "lm", 1);
        hud_text(l, x + cw + 3, 36, t, F.sb9, temp_color(hot), "lm", 0);
    }
    temp_str(t, sizeof t, temp);
    hud_text(l, 64, 79, t, F.sb11, isnan(temp) ? C_CAPTION : temp_color(temp), "mm", 0);
}

/* label on the left; the percent, then the matching temperature, on the right; a full-width
 * running-light bar below */
#define ROW_SEGS 16
static void usage_row(hud_fb_t *l, int y, const char *label, float frac, float temp, int scan)
{
    char pct[12];
    snprintf(pct, sizeof pct, "%.0f%%", clamp01(frac) * 100);
    hud_text(l, 16, y, label, F.b9, C_LEGIBLE, "lm", 1);
    int x = 112;
    if (!isnan(temp)) {
        char t[12];
        temp_str(t, sizeof t, temp);
        hud_text(l, x, y, t, F.sb9, temp_color(temp), "rm", 0);
        x -= hud_text_w(t, F.sb9, 0) + 3;
        hud_text(l, x, y, "\xC2\xB7", F.sb9, C_CAPTION, "rm", 0);      /* middle dot */
        x -= hud_text_w("\xC2\xB7", F.sb9, 0) + 3;
    }
    hud_text(l, x, y, pct, F.sb9, frac >= 0.75f ? level_color(frac) : C_INK2, "rm", 0);
    hud_segbar(l, 16, y + 6, 112, y + 12, frac, ROW_SEGS, 1, scan);
}

static float frac_of(uint64_t used, uint64_t total)
{
    return total ? (float)used / total : 0;
}

static void draw_cpu(hud_fb_t *l)
{
    const host_stats_t *h = &s_h[0];
    int ph = live_phase();
    theme_t th = load_theme(AL_CPU, h->ever ? h->cpu_load : NAN);
    hud_frame(l, th.frame, C_P900, s_ms / 500);
    name_header(l, h->cpu_model, "CPU", led_side(h->ok, 'l'), &th);
    if (!h->ever) { linking(l); return; }
    big_gauge(l, h->cpu_load, h->cpu_temp, h->cpu_hot, &th);
    char up[24];
    uptime_str(up, sizeof up, h->uptime_s);
    hud_text(l, 64, 94, up, F.b8, C_CAPTION, "mm", 1);
    usage_row(l, 106, "RAM", frac_of(h->ram_used, h->ram_total), h->ram_temp, (ph * 2) % ROW_SEGS);
}

static void draw_gpu(hud_fb_t *l)
{
    const host_stats_t *h = &s_h[0];
    int ph = live_phase();
    theme_t th = load_theme(AL_GPU, h->ever && h->has_gpu ? h->gpu.load : NAN);
    hud_frame(l, th.frame, C_P900, s_ms / 500);
    name_header(l, h->has_gpu ? h->gpu.name : "", "GPU", led_side(h->ok, 'r'), &th);
    if (!h->ever) { linking(l); return; }
    if (!h->has_gpu) {
        hud_text(l, 64, 66, "NO DATA", F.b9, C_CAPTION, "mm", 2);
        return;
    }
    big_gauge(l, h->gpu.load, h->gpu.temp, NAN, &th);                /* no hotspot on this panel */
    usage_row(l, 106, "VRAM", frac_of(h->gpu.vram_used, h->gpu.vram_total), h->gpu.vram_temp,
              (ph * 2 + 7) % ROW_SEGS);
}

/* --------------------------------------------------------------- CLOCK */

static const hud_image_t *weather_icon(int code, bool day)
{
    if (code == 0) return day ? &img_icon_clear_day : &img_icon_clear_night;
    if (code == 1 || code == 2) return day ? &img_icon_partly_day : &img_icon_partly_night;
    if (code == 3) return &img_icon_overcast;
    if (code == 45 || code == 48) return &img_icon_fog;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return &img_icon_snow;
    if (code >= 95) return &img_icon_storm;
    if (code >= 80 && code <= 82) return day ? &img_icon_showers_day : &img_icon_rain;
    if (code >= 51 && code <= 57) return &img_icon_drizzle;
    if (code >= 61) return &img_icon_rain;
    return &img_icon_overcast;
}

static int nav_colon_on(void)  { return (s_ms / 500) % 2 == 0; }      /* blinks once a second */
static int nav_emblem(void)    { return (int)((s_ms * IMG_EMBLEM_FRAMES / IMG_EMBLEM_PERIOD_MS) % IMG_EMBLEM_FRAMES); }

static void draw_nav(hud_fb_t *l)
{
    hud_frame(l, C_ACCENT, C_P900, s_ms / 500);
    hud_image(l, &img_emblem[nav_emblem()], 64 - IMG_EMBLEM_SIZE / 2, 24 - IMG_EMBLEM_SIZE / 2);

    /* HH:MM, zero-padded; the colon blinks, the digits never move */
    char hh[4] = "--", mm[4] = "--", date[24] = "";
    if (s_time_ok) {
        strftime(hh, sizeof hh, s_hour24 ? "%H" : "%I", &s_tm);
        strftime(mm, sizeof mm, "%M", &s_tm);
        strftime(date, sizeof date, "%a %d %b", &s_tm);
        for (char *c = date; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    }
    int wh = hud_text_w(hh, F.o28, 0), wc = hud_text_w(":", F.o28, 0), wm = hud_text_w(mm, F.o28, 0);
    int x = 64 - (wh + wc + wm) / 2;
    hud_text(l, x, 72, hh, F.o28, C_INK, "ls", 0);
    hud_text(l, x + wh, 72, ":", F.o28, nav_colon_on() ? C_INK : C_OFF, "ls", 0);
    hud_text(l, x + wh + wc, 72, mm, F.o28, C_INK, "ls", 0);
    hud_text(l, 64, 85, date, F.b9, C_LEGIBLE, "mm", 2);

    /* the divider doubles as a seconds bar, with a tick under every quarter minute */
    int sec_x = 16 + (s_time_ok ? s_tm.tm_sec : 0) * 96 / 60;
    hud_rect(l, 16, 95, 112, 96, C_HUD_DIM);
    if (s_time_ok) hud_rect(l, 16, 95, sec_x, 96, C_HUD);
    for (int q = 0; q <= 4; q++) hud_rect(l, 16 + q * 24 - (q == 4), 96, 17 + q * 24 - (q == 4), 98, C_HUD_DIM);

    /* weather: icon, temperature, relative humidity */
    if (s_w.ok) {
        hud_image(l, weather_icon(s_w.code, s_w.is_day), 13, 98);
        char t[12], rh[12];
        snprintf(t, sizeof t, "%.0f\xC2\xB0", s_w.temp_c);
        hud_text(l, 38, 109, t, F.b12, C_INK, "lm", 0);
        if (!isnan(s_w.humidity)) {
            snprintf(rh, sizeof rh, "%.0f%%", s_w.humidity);
            int rw = hud_text_w(rh, F.b12, 0);
            hud_text(l, 115, 109, rh, F.b12, C_INK, "rm", 0);
            hud_text(l, 115 - rw - 3, 109, "RH", F.b7, C_CAPTION, "rm", 1);
        }
    } else {
        hud_text(l, 64, 109, "\xE2\x80\x94", F.b12, C_CAPTION, "mm", 0);
    }
}

/* ------------------------------------------------------------- HOME LAB */

static void color_bar(hud_fb_t *l, int x0, int y0, int x1, int y1, float frac, int segs, hud_color_t c)
{
    float w = (float)(x1 - x0 - (segs - 1)) / segs;
    int lit = (int)lroundf(clamp01(frac) * segs);
    for (int i = 0; i < segs; i++) {
        int sx = (int)lroundf(x0 + i * (w + 1)), ex = (int)lroundf(x0 + i * (w + 1) + w);
        hud_rect(l, sx, y0, ex, y1, i < lit ? c : C_OFF);
    }
}

static void draw_lab(hud_fb_t *l)
{
    const host_stats_t *h = &s_h[1];
    int ph = live_phase();
    hud_frame(l, C_HUD, C_P900, s_ms / 500);
    hud_header(l, F.b9, "HOME LAB", 12, 2, led_side(h->ok, 'r'), C_DANGER, C_LEGIBLE, C_HUD_DIM);
    if (!h->ever) { linking(l); return; }

    /* CPU dial (no % sign) with its temperature below */
    float frac = h->cpu_load / 100.0f;
    hud_bezel(l, 34, 54, 27, 10.0f, 0, C_HUD_DIM, C_HUD);
    hud_dial(l, 34, 54, 24, frac, 21, 5, 2.0f, ph % 2 == 0, -1, HUD_AUTO);
    char v[8];
    snprintf(v, sizeof v, "%.0f", h->cpu_load);
    hud_text(l, 34, 58, v, F.o16, frac >= 0.75f ? level_color(frac) : C_INK, "ms", 0);
    char t[12];
    temp_str(t, sizeof t, h->cpu_temp);
    hud_text(l, 34, 84, "CPU", F.b8, C_LEGIBLE, "mm", 1);
    hud_text(l, 34, 97, t, F.sb11, isnan(h->cpu_temp) ? C_CAPTION : temp_color(h->cpu_temp), "mm", 0);

    /* right column: RAM, disk, battery */
    char pct[16];
    float ram = frac_of(h->ram_used, h->ram_total);
    snprintf(pct, sizeof pct, "%.0f%%", ram * 100);
    hud_text(l, 66, 34, "RAM", F.b8, C_LEGIBLE, "lm", 1);
    hud_text(l, 114, 34, pct, F.sb9, ram >= 0.75f ? level_color(ram) : C_INK2, "rm", 0);
    hud_segbar(l, 66, 39, 114, 45, ram, 12, 1, (ph + 2) % 12);

    float disk = h->n_disks ? frac_of(h->disks[0].used, h->disks[0].total) : 0;
    snprintf(pct, sizeof pct, "%.0f%%", disk * 100);
    hud_text(l, 66, 58, "DISK", F.b8, C_LEGIBLE, "lm", 1);
    hud_text(l, 114, 58, pct, F.sb9, disk >= 0.75f ? level_color(disk) : C_INK2, "rm", 0);
    hud_segbar(l, 66, 63, 114, 69, disk, 12, 1, (ph + 6) % 12);

    if (h->has_battery) {
        snprintf(pct, sizeof pct, "%d%%", h->bat_pct);
        hud_text(l, 66, 82, h->ac ? "AC" : "BAT", F.b8, h->ac ? C_SUCCESS : C_LEGIBLE, "lm", 1);
        hud_text(l, 114, 82, pct, F.sb9, h->bat_pct <= 20 ? C_DANGER : C_INK2, "rm", 0);
        color_bar(l, 66, 87, 114, 93, h->bat_pct / 100.0f, 12,
                  h->bat_pct <= 20 ? C_DANGER : h->charging ? C_SUCCESS : C_ACCENT);
    }

    char up[24];
    uptime_str(up, sizeof up, h->uptime_s);
    hud_text(l, 64, 116, up, F.b8, C_CAPTION, "mm", 1);
}

/* ---------------------------------------------------------------- AUDIO */

#define DB_FLOOR (-90.0f)
#define DB_TOP   (-25.0f)

static void audio_update(void)       /* fast attack, slow release, falling peak caps */
{
    for (int b = 0; b < MIC_BANDS; b++) {
        float f = clamp01((s_m.bands[b] - DB_FLOOR) / (DB_TOP - DB_FLOOR));
        s_eq[b] = f > s_eq[b] ? f : fmaxf(0, s_eq[b] - 0.035f);
        s_peak[b] = s_eq[b] > s_peak[b] ? s_eq[b] : fmaxf(0, s_peak[b] - 0.008f);
    }
    /* the A-weighted level with a sound meter's FAST time weighting (125 ms), averaged as power */
    if (!isnan(s_m.dba)) s_pa += (powf(10.0f, s_m.dba / 10.0f) - s_pa) * 0.23f;
    float lf = clamp01((10.0f * log10f(s_pa) - 30.0f) / 70.0f);           /* bar: 30..100 dBA */
    s_level = lf > s_level ? s_level + (lf - s_level) * 0.5f : fmaxf(lf, s_level - 0.015f);
}

static void draw_audio(hud_fb_t *l)
{
    hud_frame(l, C_HUD, C_P900, s_ms / 500);
    /* a PC is streaming the mic: the header says so, in red (stream.c) */
    bool live = stream_listeners() > 0;
    hud_header(l, F.b9, live ? "MIC LIVE" : "AUDIO", 12, 2, live ? 'r' : led_side(mic_ok(), 'l'), C_DANGER,
               live ? C_DANGER : C_LEGIBLE, C_HUD_DIM);
    /* 16 columns of 18 cells (6x3, 1 px apart). Each column is drawn as a few colour blocks
     * (teal / amber / red / unlit / peak cap), then the gaps between rows are cut with 17 black
     * lines across the whole grid: ~100 rectangles a frame instead of 288. */
#define SEG_TOP(i) (92 - (i) * 4)
    static const int band_end[3] = { 13, 16, EQ_SEGS };   /* level_color() steps at 0.75 and 0.9 */
    const hud_color_t band_col[3] = { C_ACCENT, C_WARNING, C_DANGER };
    for (int b = 0; b < MIC_BANDS; b++) {
        int x0 = 9 + b * 7;
        int lit = (int)lroundf(s_eq[b] * EQ_SEGS);
        int peak = (int)lroundf(s_peak[b] * EQ_SEGS) - 1;
        int from = 0;
        for (int k = 0; k < 3 && from < lit; k++) {
            int to = lit < band_end[k] ? lit : band_end[k];
            hud_rect(l, x0, SEG_TOP(to - 1), x0 + 6, SEG_TOP(from) + 3, band_col[k]);
            from = to;
        }
        if (lit < EQ_SEGS) hud_rect(l, x0, SEG_TOP(EQ_SEGS - 1), x0 + 6, SEG_TOP(lit) + 3, C_OFF);
        if (peak >= lit && peak < EQ_SEGS) hud_rect(l, x0, SEG_TOP(peak), x0 + 6, SEG_TOP(peak) + 3, C_INK);
    }
    for (int i = 0; i < EQ_SEGS - 1; i++) {
        hud_rect(l, 9, SEG_TOP(i) - 1, 9 + MIC_BANDS * 7, SEG_TOP(i), C_CANVAS);
    }
#undef SEG_TOP
    /* loudness; SPL ~ dBFS + 120 for a typical -26 dBFS @ 94 dB SPL MEMS mic */
    char v[16];
    snprintf(v, sizeof v, "%.0f dBA", 10.0f * log10f(s_pa));
    hud_text(l, 9, 106, "LEVEL", F.b8, C_LEGIBLE, "lm", 1);
    hud_text(l, 119, 106, v, F.sb9, C_INK2, "rm", 0);
    hud_segbar(l, 9, 112, 119, 117, s_level, 24, 1, -1);
}

/* ------------------------------------------------------------ plumbing */

static panel_kind_t kind_of(const char *s)
{
    static const char *names[] = { "lab", "cpu", "nav", "gpu", "audio", "off" };
    for (int i = 0; i < 6; i++) if (strcmp(s, names[i]) == 0) return (panel_kind_t)i;
    return P_OFF;
}

static void render(hud_fb_t *fb, panel_kind_t kind)
{
    hud_clear(fb, C_CANVAS);
    switch (kind) {
    case P_CPU:   draw_cpu(fb); break;
    case P_GPU:   draw_gpu(fb); break;
    case P_NAV:   draw_nav(fb); break;
    case P_LAB:   draw_lab(fb); break;
    case P_AUDIO: draw_audio(fb); break;
    default: break;
    }
}

static uint32_t host_key(uint32_t h, const host_stats_t *s)
{
    host_stats_t c = *s;
    c.age_ms = 0;                     /* changes every frame, never drawn */
    return fnv(h, &c, sizeof c);
}

/* What a panel shows apart from its animation. */
static uint32_t content_key(const panel_t *p)
{
    uint32_t h = fnv(2166136261u, &p->kind, sizeof p->kind);
    switch (p->kind) {
    case P_CPU:
    case P_GPU:
        return host_key(h, &s_h[0]);
    case P_LAB:
        return host_key(h, &s_h[1]);
    case P_NAV: {
        int v[2] = { s_tm.tm_min + 60 * s_tm.tm_hour + 1440 * s_tm.tm_yday, s_hour24 };
        h = fnv(h, v, sizeof v);
        h = fnv(h, &s_w, sizeof s_w - sizeof s_w.age_ms);
        return host_key(h, &s_h[1]);
    }
    case P_AUDIO: {
        int ok = mic_ok();
        return fnv(h, &ok, sizeof ok);
    }
    default:
        return h;
    }
}

/* The animation step a panel is at; a change means a re-render. */
static int anim_step(const panel_t *p)
{
    switch (p->kind) {
    case P_CPU:
    case P_GPU:
    case P_LAB:   return live_phase();                        /* 150 ms steps */
    case P_NAV:   return nav_emblem() * 2 + nav_colon_on();   /* 15 fps emblem, colon blink */
    case P_AUDIO: return (int)(s_ms / 33);                    /* every frame */
    default:      return 0;
    }
}

static void snapshot(void)
{
    for (int i = 0; i < TG_SOURCES; i++) netdata_get_host(i, &s_h[i]);
    netdata_get_weather(&s_w);
    mic_get(&s_m);
    s_time_ok = netdata_time_valid();
    if (s_time_ok) {
        time_t now = time(NULL);
        localtime_r(&now, &s_tm);
    }
    if (settings_generation() != s_settings_gen) {
        s_settings_gen = settings_generation();
        tg_settings_t cfg;
        settings_get(&cfg);
        s_hour24 = cfg.hour24;
        for (int s = 0; s < TG_SCREENS; s++) {
            s_panel[s].kind = kind_of(cfg.layout[s]);
            s_panel[s].key = 0;
            s_panel[s].anim = -1;
        }
    }
}

static void stats_roll(void)
{
    int64_t now = esp_timer_get_time();
    if (now - S.window_start < 1000000) return;
    for (int s = 0; s < TG_SCREENS; s++) {
        S.out_refr[s] = S.refr[s];
        S.out_us[s] = S.refr[s] ? S.refr_us[s] / S.refr[s] : 0;
        S.refr[s] = S.refr_us[s] = 0;
    }
    S.out_busy_ms = S.busy_us / 1000;
    S.busy_us = 0;
    S.window_start = now;
}

/* The AUDIO panel is on a screen right now (it then shows when the mic is streamed). */
static bool audio_shown(void)
{
    if (modes_current() != MODE_DASH) return false;
    for (int s = 0; s < TG_SCREENS; s++) if (s_panel[s].kind == P_AUDIO) return true;
    return false;
}

/* One screen as it should look now: the dashboard panel or the mode's screen, and the toast on
 * the middle screen. */
static void render_screen(hud_fb_t *fb, int s)
{
    if (modes_current() == MODE_DASH) {
        render(fb, s_panel[s].kind);
    } else {
        hud_clear(fb, C_CANVAS);
        modes_draw(fb, s);
    }
    if (s == 2 && modes_toast_active()) modes_draw_toast(fb);
    if (s == 2 && stream_listeners() && !audio_shown()) {   /* the mic is streamed and no AUDIO screen says so */
        /* just under the header on the right: clear on the clock and on every mode's middle screen */
        hud_text(fb, 110, 24, "MIC", F.b7, C_DANGER, "rm", 1);
        hud_circle(fb, 115.5f, 24.0f, 3.0f, C_DANGER);
    }
}

esp_err_t ui_capture(int screen, uint16_t *out)
{
    if (screen < 0 || screen >= TG_SCREENS || !out) return ESP_ERR_INVALID_ARG;
    render_screen(out, screen);
    return ESP_OK;
}

/* The backlight: the settings' level (night or day), unless a mode overrides it (LIGHT, sleep). */
static void backlight_update(void)
{
    static int last = -1;
    tg_settings_t c;
    settings_get(&c);
    int base = settings_is_night(&c, s_time_ok) ? c.night_brightness : c.brightness;
    int pct = modes_backlight(base);
    if (power_idle() && last >= 0 && pct < last - 4) pct = last - 4;    /* into low power: fade, ~5 s */
    if (pct != last) {
        last = pct;
        tg_backlight_set((uint8_t)(pct * 255 / 100));
    }
}

int ui_stats_json(char *buf, int n)
{
    int k = snprintf(buf, n, "{\"busy_ms_per_s\":%lu,\"refresh_per_s\":[", (unsigned long)S.out_busy_ms);
    for (int s = 0; s < TG_SCREENS; s++) k += snprintf(buf + k, n - k, "%s%lu", s ? "," : "", (unsigned long)S.out_refr[s]);
    k += snprintf(buf + k, n - k, "],\"us_per_refresh\":[");
    for (int s = 0; s < TG_SCREENS; s++) k += snprintf(buf + k, n - k, "%s%lu", s ? "," : "", (unsigned long)S.out_us[s]);
    k += snprintf(buf + k, n - k, "]}");
    return k;
}

static void ui_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    static uint32_t shown_hash[TG_SCREENS];      /* what each screen shows, for the modes' screens */
    ui_mode_t was_mode = MODE_DASH;
    bool was_asleep = false;
    for (int frame = 0;; frame++) {
        int64_t t0 = esp_timer_get_time();
        bool idle = power_idle();
        TickType_t period = pdMS_TO_TICKS(idle ? 250 : 33);       /* low power: 4 fps, else 30 */
        s_ms = (uint32_t)(t0 / 1000);
        snapshot();
        audio_update();
        modes_update(s_ms);
        if (frame % 4 == 0 || idle || modes_sleeping() != was_asleep) backlight_update();
        ui_mode_t mode = modes_current();
        bool asleep = modes_sleeping();
        if (mode != was_mode || asleep != was_asleep) {      /* everything gets drawn afresh */
            for (int s = 0; s < TG_SCREENS; s++) {
                s_panel[s].key = 0;
                s_panel[s].anim = -1;
                shown_hash[s] = 0;
            }
            was_mode = mode;
            was_asleep = asleep;
        }
        if (asleep) {
            vTaskDelayUntil(&last, period);
            continue;
        }
        if (mode != MODE_DASH) {
            /* the modes draw every screen every frame; only changed pixels go to a panel */
            for (int s = 0; s < TG_SCREENS; s++) {
                int64_t r0 = esp_timer_get_time();
                hud_fb_t *fb = tg_display_lock();
                if (!fb) continue;
                render_screen(fb, s);
                uint32_t h = fnv(2166136261u, fb, HUD_W * HUD_H * sizeof(hud_fb_t));
                if (h != shown_hash[s]) {
                    shown_hash[s] = h;
                    tg_display_show(s);
                    S.refr[s]++;
                    S.refr_us[s] += (uint32_t)(esp_timer_get_time() - r0);
                }
                tg_display_unlock();
            }
            S.busy_us += (uint32_t)(esp_timer_get_time() - t0);
            stats_roll();
            vTaskDelayUntil(&last, period);
            continue;
        }
        for (int s = 0; s < TG_SCREENS; s++) {
            panel_t *p = &s_panel[s];
            uint32_t k = content_key(p);
            if (s == 2 && modes_toast_active()) k = fnv(k, (uint32_t[]){ modes_toast_gen() }, sizeof(uint32_t));
            if (s == 2) k = fnv(k, (uint32_t[]){ (uint32_t)stream_listeners() }, sizeof(uint32_t));
            int a = anim_step(p);
            if (k == p->key && a == p->anim) continue;
            p->key = k;
            p->anim = a;
            int64_t r0 = esp_timer_get_time();
            hud_fb_t *fb = tg_display_lock();
            if (!fb) continue;
            render_screen(fb, s);
            tg_display_show(s);
            tg_display_unlock();
            S.refr[s]++;
            S.refr_us[s] += (uint32_t)(esp_timer_get_time() - r0);
        }
        S.busy_us += (uint32_t)(esp_timer_get_time() - t0);
        stats_roll();
        vTaskDelayUntil(&last, period);
    }
}

esp_err_t ui_start(void)
{
    if (!tg_display_ok()) return ESP_ERR_INVALID_STATE;
    /* Fonts pre-rendered by tools/gen_fonts.py (PIL, the PC cockpit's theme.font()). */
    F.b7 = &font_b7;
    F.b8 = &font_b8;
    F.b9 = &font_b9;
    F.b12 = &font_b12;
    F.sb9 = &font_sb9;
    F.sb11 = &font_sb11;
    F.o14 = &font_o14;
    F.o16 = &font_o16;
    F.o22 = &font_o22;
    F.o28 = &font_o28;
    for (int s = 0; s < TG_SCREENS; s++) {
        s_panel[s] = (panel_t){ .screen = s, .kind = P_OFF, .key = 0, .anim = -1 };
    }
    s_settings_gen = 0;                   /* the first frame loads the layout */
    modes_init();
    xTaskCreatePinnedToCore(ui_task, "ui", 6144, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "cockpit running");
    return ESP_OK;
}
