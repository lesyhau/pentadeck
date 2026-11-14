#include "ui/viz.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core/settings.h"
#include "drivers/mic.h"

static const char *const NAMES[VIZ_COUNT] = { "SPECTRUM", "MIRROR", "SCOPE", "WATERFALL", "RIPPLE", "RADIAL" };
static const char *const KEYS[VIZ_COUNT] = { "spectrum", "mirror", "scope", "waterfall", "ripple", "radial" };

/* ------------------------------------------------------------------ state */

#define HIST_W  640                      /* the waterfall's memory: one column per mic block (32 ms), ~20 s */
#define RIPPLES 12
#define RIP_S   1.6f                     /* a ripple's life, seconds, at a quiet level */

static float s_band[MIC_BANDS], s_peak[MIC_BANDS];   /* 0..1: fast attack, slow release; falling peaks */
static float s_level;                    /* loudness, 0..1 */
static float s_bass, s_bass_avg;         /* the lowest four bands, 0..1, and their slow average */
static uint32_t s_beat_ms, s_last_ms, s_amb_ms;
static unsigned s_block;
static uint8_t s_hist[HIST_W][MIC_BANDS];
static int s_hist_w;                     /* the column written next */
static float s_wave[MIC_WAVE];
static int s_wave_n;
static float s_gain = 100;               /* the scope's automatic gain */
static struct { float life, hue, strength; bool on; } s_rip[RIPPLES];
static int s_rip_next;
static float s_hue;

static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static void ripple(float strength)
{
    s_rip[s_rip_next].on = true;
    s_rip[s_rip_next].life = 0;
    s_rip[s_rip_next].hue = s_hue;
    s_rip[s_rip_next].strength = strength;
    s_rip_next = (s_rip_next + 1) % RIPPLES;
    s_hue += 0.137f;
    if (s_hue >= 1) s_hue -= 1;
}

void viz_update(uint32_t ms)
{
    float dt = s_last_ms ? (ms - s_last_ms) / 1000.0f : 0;
    if (dt > 0.2f) dt = 0.2f;
    s_last_ms = ms;

    mic_snapshot_t m;
    mic_get(&m);
    if (m.blocks != s_block) {
        s_block = m.blocks;
        float bass = 0;
        for (int b = 0; b < MIC_BANDS; b++) {
            float f = clamp01((m.bands[b] - MIC_BAND_FLOOR_DB) / (MIC_BAND_TOP_DB - MIC_BAND_FLOOR_DB));
            s_band[b] = f > s_band[b] ? f : fmaxf(0, s_band[b] - 0.04f);
            s_peak[b] = s_band[b] > s_peak[b] ? s_band[b] : fmaxf(0, s_peak[b] - 0.008f);
            s_hist[s_hist_w][b] = (uint8_t)(s_band[b] * 255);
            if (b < 4) bass += f / 4;
        }
        s_hist_w = (s_hist_w + 1) % HIST_W;
        /* a beat: the bass well above its recent average (about 6 dB), at most four a second */
        if (bass > s_bass_avg + 0.09f && bass > 0.3f && ms - s_beat_ms > 250) {
            s_beat_ms = ms ? ms : 1;
            ripple(clamp01(0.5f + (bass - s_bass_avg) * 3));
        }
        s_bass_avg += (bass - s_bass_avg) * 0.06f;
        s_bass = bass > s_bass ? bass : s_bass * 0.88f;
        float lv = isnan(m.dbfs) ? 0 : clamp01((m.dbfs + 75.0f) / 55.0f);
        s_level += (lv - s_level) * (lv > s_level ? 0.5f : 0.08f);

        s_wave_n = mic_wave(s_wave, MIC_WAVE, NULL);
        float peak = 0;
        for (int i = 0; i < s_wave_n; i++) peak = fmaxf(peak, fabsf(s_wave[i]));
        float target = 0.9f / fmaxf(peak, 0.003f);        /* quieter than -50 dBFS stays small */
        s_gain = target < s_gain ? target : s_gain + (target - s_gain) * 0.04f;
    }
    /* something going on but no beat: a faint ripple now and then */
    if (s_level > 0.2f && ms - s_beat_ms > 900 && ms - s_amb_ms > 900) {
        s_amb_ms = ms;
        ripple(0.25f + s_level * 0.3f);
    }
    for (int i = 0; i < RIPPLES; i++) {
        if (!s_rip[i].on) continue;
        s_rip[i].life += dt * (0.7f + 0.8f * s_level) / RIP_S;
        if (s_rip[i].life >= 1) s_rip[i].on = false;
    }
}

uint32_t viz_beat_ms(void) { return s_beat_ms; }

const char *viz_name(viz_effect_t fx) { return fx < VIZ_COUNT ? NAMES[fx] : "?"; }

bool viz_from_key(const char *key, viz_effect_t *fx)
{
    for (int i = 0; i < VIZ_COUNT; i++) {
        if (strcmp(key, KEYS[i]) == 0) { *fx = (viz_effect_t)i; return true; }
    }
    return false;
}

/* ---------------------------------------------------------------- colours */

static hud_color_t heat(float v)              /* embers to white */
{
    if (v < 0.35f) return hud_mix(0x1A0000u, 0xE01000u, v / 0.35f);
    if (v < 0.7f) return hud_mix(0xE01000u, 0xFF9800u, (v - 0.35f) / 0.35f);
    return hud_mix(0xFF9800u, 0xFFF4C0u, (v - 0.7f) / 0.3f);
}

/* t: where along the effect (bass 0 .. treble 1, or a ripple's hue); v: how strong, 0..1 */
static hud_color_t pal(int p, float t, float v)
{
    v = clamp01(v);
    switch (p) {
    case VIZ_PAL_RAINBOW: return hud_hsv(t * 0.8f, 0.9f, 0.15f + 0.85f * v);
    case VIZ_PAL_FIRE:    return heat(0.08f + 0.92f * v);
    default:
        if (v < 0.45f) return hud_mix(hud_scale(C_P900, 0.6f), C_P600, v / 0.45f);
        if (v < 0.8f) return hud_mix(C_P600, C_P400, (v - 0.45f) / 0.35f);
        return hud_mix(C_P400, C_P300, (v - 0.8f) / 0.2f);
    }
}

/* the spectrum at u (0 = 50 Hz .. 1 = 12 kHz), smoothly between the bands */
static float band_at(const float *a, float u)
{
    float x = clamp01(u) * (MIC_BANDS - 1);
    int i = (int)x;
    if (i >= MIC_BANDS - 1) return a[MIC_BANDS - 1];
    float f = x - i;
    f = f * f * (3 - 2 * f);
    return a[i] * (1 - f) + a[i + 1] * f;
}

/* canvas -> screen */
#define SX(x) ((int)lroundf((float)(x)) + v->ox)
#define SY(y) ((int)lroundf((float)(y)) + v->oy)

static inline bool cols_shown(const viz_view_t *v, float x0, float x1)
{
    return x1 + v->ox > 0 && x0 + v->ox < HUD_W;
}

/* the canvas columns on this screen */
static void shown_cols(const viz_view_t *v, int *x0, int *x1)
{
    *x0 = -v->ox > 0 ? -v->ox : 0;
    *x1 = HUD_W - v->ox < v->w ? HUD_W - v->ox : v->w;
}

/* --------------------------------------------------------------- effects */

static void fx_spectrum(hud_fb_t *fb, const viz_view_t *v, int p)
{
    int n = v->w / 10;
    n = n < 16 ? 16 : n > 64 ? 64 : n;
    float pitch = (float)v->w / n;
    int bw = (int)(pitch * 0.75f);
    if (bw < 2) bw = 2;
    int cell = v->h >= 100 ? 4 : 3, cells = v->h / cell;
    for (int i = 0; i < n; i++) {
        float x0 = i * pitch + (pitch - bw) / 2;
        if (!cols_shown(v, x0, x0 + bw)) continue;
        float u = (i + 0.5f) / n;
        int lit = (int)lroundf(band_at(s_band, u) * cells);
        int pk = (int)lroundf(band_at(s_peak, u) * cells) - 1;
        int sx = SX(x0);
        for (int c = 0; c < lit && c < cells; c++) {
            int y1 = v->h - c * cell;
            hud_rect(fb, sx, SY(y1 - cell + 1), sx + bw, SY(y1), pal(p, u, 0.3f + 0.7f * (c + 1) / cells));
        }
        if (pk >= lit && pk < cells) {
            int y1 = v->h - pk * cell;
            hud_rect(fb, sx, SY(y1 - cell + 1), sx + bw, SY(y1), C_INK2);
        }
    }
}

static void fx_mirror(hud_fb_t *fb, const viz_view_t *v, int p)
{
    int pitch = v->w >= 256 ? 8 : 4, bw = pitch - (pitch >= 8 ? 2 : 1);
    float half = v->w / 2.0f, cy = v->h / 2.0f, maxh = v->h / 2.0f - 2;
    int n = (int)(half / pitch);
    for (int i = 0; i < n; i++) {
        float u = (i + 0.5f) / n, val = band_at(s_band, u), pk = band_at(s_peak, u);
        int hh = (int)(val * maxh), ph = (int)(pk * maxh);
        hud_color_t body = pal(p, u, 0.25f + 0.55f * val), tip = pal(p, u, 1.0f), cap = pal(p, u, 0.7f);
        for (int side = -1; side <= 1; side += 2) {
            float x0 = side > 0 ? half + i * pitch + 1 : half - (i + 1) * pitch + (pitch - bw) - 1;
            if (!cols_shown(v, x0, x0 + bw)) continue;
            int sx = SX(x0);
            hud_rect(fb, sx, SY(cy), sx + bw, SY(cy) + 1, pal(p, u, 0.15f));   /* the axis */
            if (hh > 0) {
                hud_rect(fb, sx, SY(cy - hh), sx + bw, SY(cy + hh) + 1, body);
                hud_rect(fb, sx, SY(cy - hh), sx + bw, SY(cy - hh) + 2, tip);
                hud_rect(fb, sx, SY(cy + hh) - 1, sx + bw, SY(cy + hh) + 1, tip);
            }
            if (ph > hh + 2) {
                hud_rect(fb, sx, SY(cy - ph), sx + bw, SY(cy - ph) + 1, cap);
                hud_rect(fb, sx, SY(cy + ph), sx + bw, SY(cy + ph) + 1, cap);
            }
        }
    }
}

static void fx_scope(hud_fb_t *fb, const viz_view_t *v, int p)
{
    int step = v->w >= 256 ? 1 : 4;              /* samples per column: 20 ms wide, 14 ms on one screen */
    int need = v->w * step, x0, x1;
    float cy = v->h / 2.0f, amp = v->h / 2.0f - 3;
    shown_cols(v, &x0, &x1);
    /* a faint graticule */
    hud_color_t grid = pal(p, 0.5f, 0.08f);
    for (int x = x0; x < x1; x++) {
        if (x % 4 == 0) hud_rect(fb, SX(x), SY(cy), SX(x) + 1, SY(cy) + 1, grid);
        if (x % 32 == 16) {
            for (int y = 2; y < v->h - 2; y += 4) hud_rect(fb, SX(x), SY(y), SX(x) + 1, SY(y) + 1, grid);
        }
    }
    if (s_wave_n < need + 8) return;
    /* hold the trace still: start on a rising zero crossing (with some hysteresis against noise) */
    int start = s_wave_n - need;
    float peak = 0;
    for (int i = 0; i < s_wave_n; i++) peak = fmaxf(peak, fabsf(s_wave[i]));
    for (int i = 8; i < s_wave_n - need; i++) {
        if (s_wave[i - 1] < 0 && s_wave[i] >= 0 && s_wave[i - 6] < -0.15f * peak) { start = i; break; }
    }
    int lo = x0 > 0 ? x0 - 1 : 0;
    static int16_t ys[HUD_W + 1];
    for (int x = lo; x < x1; x++) {
        float s = 0;
        for (int k = 0; k < step; k++) s += s_wave[start + x * step + k];
        s = s / step * s_gain;
        s = s < -1 ? -1 : s > 1 ? 1 : s;
        ys[x - lo] = (int16_t)lroundf(cy - s * amp);
    }
    for (int pass = 0; pass < 2; pass++) {       /* a glow, then the trace on it */
        for (int x = x0; x < x1; x++) {
            int y = ys[x - lo], yp = x > lo ? ys[x - lo - 1] : y;
            int ya = y < yp ? y : yp, yb = y < yp ? yp : y;
            float u = (float)x / v->w;
            if (pass == 0) hud_rect(fb, SX(x) - 1, SY(ya) - 1, SX(x) + 2, SY(yb) + 2, pal(p, u, 0.3f));
            else hud_rect(fb, SX(x), SY(ya), SX(x) + 1, SY(yb) + 1, pal(p, u, 1.0f));
        }
    }
}

static void fx_waterfall(hud_fb_t *fb, const viz_view_t *v, int p)
{
    bool wide = v->w >= 256;                     /* the newest in the middle, running out both ways */
    int x0, x1;
    shown_cols(v, &x0, &x1);
    /* every cell's colour from a table: 32 levels a band, rebuilt when the colours change */
    static hud_color_t lut[MIC_BANDS][32];
    static int lut_pal = -1;
    static uint32_t lut_accent;
    if (lut_pal != p || lut_accent != hud_accent()) {
        lut_pal = p;
        lut_accent = hud_accent();
        for (int b = 0; b < MIC_BANDS; b++) {
            for (int k = 0; k < 32; k++) lut[b][k] = pal(p, b / (MIC_BANDS - 1.0f), (k + 0.5f) / 32);
        }
    }
    for (int x = x0; x < x1; x++) {
        int age = wide ? abs(x - v->w / 2) : v->w - 1 - x;
        if (age >= HIST_W) continue;
        const uint8_t *col = s_hist[(s_hist_w - 1 - age + 2 * HIST_W) % HIST_W];
        for (int b = 0; b < MIC_BANDS; b++) {
            if (col[b] < 10) continue;
            int y0 = v->h - (b + 1) * v->h / MIC_BANDS, y1 = v->h - b * v->h / MIC_BANDS;
            hud_rect(fb, SX(x), SY(y0), SX(x) + 1, SY(y1), lut[b][col[b] >> 3]);
        }
    }
}

static void fx_ripple(hud_fb_t *fb, const viz_view_t *v, int p)
{
    float cx = v->w / 2.0f, cy = v->h / 2.0f, rmax = sqrtf(cx * cx + cy * cy) + 4;
    for (int i = 0; i < RIPPLES; i++) {
        if (!s_rip[i].on) continue;
        float life = s_rip[i].life, fade = (1 - life) * sqrtf(1 - life) * s_rip[i].strength;
        float w = 1.2f + 2.5f * s_rip[i].strength * (1 - life);
        hud_ring(fb, cx + v->ox, cy + v->oy, life * rmax, w, hud_scale(pal(p, s_rip[i].hue, 0.6f + 0.4f * s_rip[i].strength), fade));
    }
    /* the core, swelling with the bass */
    float side = fminf(v->w, v->h);
    if (cols_shown(v, cx - side / 2, cx + side / 2)) {
        float r = 3 + s_bass * side * 0.16f;
        hud_circle(fb, cx + v->ox, cy + v->oy, r + 3, pal(p, s_hue, 0.15f + 0.3f * s_bass));
        hud_circle(fb, cx + v->ox, cy + v->oy, r, pal(p, s_hue, 0.4f + 0.6f * s_bass));
    }
}

static void fx_radial(hud_fb_t *fb, const viz_view_t *v, int p)
{
    int K = v->w >= 256 ? v->w / 128 : 1;       /* five on the wide canvas: bass on the left */
    float cw = (float)v->w / K, cy = v->h / 2.0f;
    float side = fminf(cw, v->h), r0 = side * 0.2f, len = side * 0.5f - r0 - 3;
    int dots = (int)(len / 3);
    if (dots < 3) dots = 3;
    int M = (int)(2 * (float)M_PI * r0 / 3.2f) & ~1;
    M = M < 24 ? 24 : M > 64 ? 64 : M;
    for (int k = 0; k < K; k++) {
        float cx = (k + 0.5f) * cw;
        if (!cols_shown(v, cx - side / 2, cx + side / 2)) continue;
        float lo = (float)k / K, hi = (float)(k + 1) / K, e = 0;
        for (int j = 0; j <= 4; j++) e += band_at(s_band, lo + (hi - lo) * j / 4) / 5;
        hud_ring(fb, cx + v->ox, cy + v->oy, r0 - 4 + e * 3, 1.2f, pal(p, (lo + hi) / 2, 0.4f + 0.6f * e));
        for (int j = 0; j < M; j++) {
            /* mirrored: the slice runs down both sides, from the top to the bottom */
            float a = 2 * (float)M_PI * j / M, u = (j <= M / 2 ? j : M - j) / (M / 2.0f);
            float ub = lo + (hi - lo) * u;
            int lit = (int)lroundf(band_at(s_band, ub) * dots);
            int pk = (int)lroundf(band_at(s_peak, ub) * dots) - 1;
            float sx = sinf(a), sy = -cosf(a);
            for (int d = 0; d < dots; d++) {
                hud_color_t c;
                if (d < lit) c = pal(p, ub, 0.3f + 0.7f * (d + 1) / dots);
                else if (d == pk) c = C_INK2;
                else continue;
                float rr = r0 + d * 3;
                int px = SX(cx + sx * rr), py = SY(cy + sy * rr);
                hud_rect(fb, px - 1, py - 1, px + 1, py + 1, c);
            }
        }
    }
}

void viz_draw(hud_fb_t *fb, viz_effect_t fx, const viz_view_t *v, int palette)
{
    switch (fx) {
    case VIZ_SPECTRUM:  fx_spectrum(fb, v, palette); break;
    case VIZ_MIRROR:    fx_mirror(fb, v, palette); break;
    case VIZ_SCOPE:     fx_scope(fb, v, palette); break;
    case VIZ_WATERFALL: fx_waterfall(fb, v, palette); break;
    case VIZ_RIPPLE:    fx_ripple(fb, v, palette); break;
    case VIZ_RADIAL:    fx_radial(fb, v, palette); break;
    default: break;
    }
}
