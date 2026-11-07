/* The sound level meter and the starfield. */
#include "modes/mode_kit.h"

#include "esp_random.h"

#include "drivers/mic.h"

/* ============================================================ sound meter */

/* FAST time weighting (125 ms) on the power; max / min of that; Leq = the energy average since the
 * last reset. A = A-weighted (the usual), Z = unweighted. Levels assume the usual MEMS sensitivity
 * (dB SPL = dBFS + 120): close, not calibrated. */
static bool sm_z;
static float sm_p = 1e-3f, sm_max = -1, sm_min = 1e9f, sm_sum;
static uint32_t sm_n, sm_block, sm_since;
static float sm_band[MIC_BANDS], sm_peak[MIC_BANDS];

static void sm_reset(void)
{
    sm_max = -1;
    sm_min = 1e9f;
    sm_sum = 0;
    sm_n = 0;
    sm_since = g_ms;
}

static void sm_enter(void) { sm_reset(); }

static void sm_update(void)
{
    mic_snapshot_t m;
    mic_get(&m);
    if (m.blocks == sm_block) return;
    sm_block = m.blocks;
    float db = sm_z ? m.dbfs + 120.0f : m.dba;
    if (isnan(db)) return;
    sm_p += (powf(10.0f, db / 10.0f) - sm_p) * 0.23f;
    float fast = 10.0f * log10f(sm_p);
    if (g_ms - sm_since > 500) {                      /* let the average settle before max / min count */
        if (fast > sm_max) sm_max = fast;
        if (fast < sm_min) sm_min = fast;
    }
    sm_sum += powf(10.0f, db / 10.0f);
    sm_n++;
    for (int b = 0; b < MIC_BANDS; b++) {
        float f = clamp01((m.bands[b] + 90.0f) / 65.0f);
        sm_band[b] = f > sm_band[b] ? f : fmaxf(0, sm_band[b] - 0.04f);
        sm_peak[b] = sm_band[b] > sm_peak[b] ? sm_band[b] : fmaxf(0, sm_peak[b] - 0.006f);
    }
}

static void sm_key(const btn_event_t *e)
{
    if (e->kind == BTN_SHORT && e->key == KEY_POWER) sm_reset();
    else if (e->kind == BTN_DOWN && !e->repeat && (e->key == KEY_LEFT || e->key == KEY_RIGHT)) {
        sm_z = !sm_z;
        sm_p = 1e-3f;
        sm_reset();
    }
}

static hud_color_t sm_color(float db) { return db >= 100 ? C_DANGER : db >= 85 ? C_ORANGE : C_INK; }

static void sm_draw(hud_fb_t *fb, int s)
{
    char t[24];
    float fast = 10.0f * log10f(sm_p);
    const char *unit = sm_z ? "DBZ" : "DBA";
    if (s == 0) {
        mk_title(fb, "SOUND LEVEL");
        snprintf(t, sizeof t, "%.0f", fast);
        hud_text(fb, 64, 76, t, &font_o48, sm_color(fast), "ms", 0);
        hud_text(fb, 64, 92, unit, &font_b8, C_LEGIBLE, "mm", 2);
        mk_softkey(fb, "LIGHT");
        return;
    }
    if (s == 1) {
        mk_title(fb, "STATS");
        const char *label[3] = { "MAX", "MIN", "LEQ" };
        float v[3] = { sm_max, sm_min, sm_n ? 10.0f * log10f(sm_sum / sm_n) : NAN };
        for (int i = 0; i < 3; i++) {
            int y = 38 + i * 22;
            hud_text(fb, 18, y, label[i], &font_b8, C_LEGIBLE, "lm", 1);
            if (v[i] < 0 || v[i] > 1e8f || isnan(v[i])) snprintf(t, sizeof t, "\xE2\x80\x94");
            else snprintf(t, sizeof t, "%.0f", v[i]);
            hud_text(fb, 110, y, t, &font_o16, i == 0 ? sm_color(v[i]) : C_INK2, "rm", 0);
        }
        mk_softkey(fb, sm_z ? "A-WEIGHT" : "Z-WEIGHT");
        return;
    }
    /* screens 2..4: the 16 bands as one 384 px spectrum, 24 px a band */
    if (s == 3) mk_title(fb, "SPECTRUM");
    int off = (s - 2) * 128;
    for (int b = 0; b < MIC_BANDS; b++) {
        int x0 = 4 + b * 24 - off, x1 = x0 + 18;
        if (x1 <= 0 || x0 >= 128) continue;
        int lit = (int)lroundf(sm_band[b] * 18), pk = (int)lroundf(sm_peak[b] * 18) - 1;
        for (int r = 0; r < 18; r++) {
            int y = 102 - r * 4;
            hud_color_t c = r < lit ? (r >= 16 ? C_DANGER : r >= 13 ? C_WARNING : C_ACCENT) : C_OFF;
            if (r == pk && pk >= lit) c = C_INK;
            hud_rect(fb, x0, y, x1, y + 3, c);
        }
    }
    static const struct { int band; const char *t; } AX[] = { { 0, "50" }, { 4, "150" }, { 8, "700" }, { 12, "3K" }, { 15, "12K" } };
    for (size_t i = 0; i < sizeof AX / sizeof AX[0]; i++) {
        int x = 4 + AX[i].band * 24 + 9 - off;
        if (x >= 6 && x < 122) hud_text(fb, x, 110, AX[i].t, &font_b7, C_CAPTION, "mm", 0);
    }
    if (s == 2) mk_softkey(fb, "RESET");
    else if (s == 3) mk_softkey(fb, sm_z ? "A-WEIGHT" : "Z-WEIGHT");
    else mk_softkey(fb, "MODE");
}

const mode_def_t MODE_DEF_SOUND = {
    .name = "SOUND", .enter = sm_enter, .key = sm_key, .update = sm_update, .draw = sm_draw,
};

/* ================================================================== stars */

/* A 3-D starfield seen through all five screens as one 640 x 128 window, centred on the middle
 * screen. Each star is a streak from where it was a moment ago to where it is now. With "audio" on,
 * the speed follows the room's loudness. */
#define ST_N 150
static struct { float x, y, z; } st[ST_N];
static float st_speed = 1.0f, st_cur = 1.0f;
static bool st_audio = true, st_init;
static uint32_t st_last;

static float frand(void) { return (esp_random() & 0xFFFFFF) / 16777216.0f; }

static void st_spawn(int i, bool anywhere)
{
    st[i].x = (frand() * 2 - 1) * 5.0f;
    st[i].y = (frand() * 2 - 1) * 1.0f;
    st[i].z = anywhere ? 0.1f + frand() * 0.9f : 1.0f;
}

static void st_enter(void)
{
    if (!st_init) {
        for (int i = 0; i < ST_N; i++) st_spawn(i, true);
        st_init = true;
    }
    st_last = g_ms;
}

static void st_update(void)
{
    float dt = fminf(0.1f, (g_ms - st_last) / 1000.0f);
    st_last = g_ms;
    float target = st_speed;
    if (st_audio) {
        mic_snapshot_t m;
        mic_get(&m);
        if (!isnan(m.dba)) target *= 0.6f + clamp01((m.dba - 40.0f) / 35.0f) * 2.4f;
    }
    st_cur += (target - st_cur) * fminf(1, dt * 3);
    for (int i = 0; i < ST_N; i++) {
        st[i].z -= dt * 0.35f * st_cur;
        if (st[i].z < 0.04f) st_spawn(i, false);
    }
}

static void st_key(const btn_event_t *e)
{
    if (e->kind == BTN_DOWN && (e->key == KEY_LEFT || e->key == KEY_RIGHT)) {
        st_speed = fminf(4.0f, fmaxf(0.25f, st_speed * (e->key == KEY_RIGHT ? 1.25f : 0.8f)));
    } else if (e->kind == BTN_SHORT && e->key == KEY_POWER) {
        st_audio = !st_audio;
        mk_toast("WARP", st_audio ? "FOLLOWS MUSIC" : "STEADY", NAN);
    }
}

static void st_draw(hud_fb_t *fb, int s)
{
    const float cx = 320, cy = 64, k = 60;
    float trail = 0.05f * st_cur;
    for (int i = 0; i < ST_N; i++) {
        float z = st[i].z, z0 = fminf(1.0f, z + trail);
        float x1 = cx + st[i].x / z * k - s * 128, y1 = cy + st[i].y / z * k;
        float x0 = cx + st[i].x / z0 * k - s * 128, y0 = cy + st[i].y / z0 * k;
        if ((x1 < -2 && x0 < -2) || (x1 > 130 && x0 > 130) || y1 < -2 || y1 > 130) continue;
        float b = clamp01(1.2f - z);                  /* nearer = brighter */
        uint32_t lv = (uint32_t)(80 + 175 * b);
        hud_color_t c = (lv * 3 / 4) << 16 | (lv * 9 / 10) << 8 | lv;   /* a faintly blue white */
        if (fabsf(x1 - x0) + fabsf(y1 - y0) < 1.0f) hud_rect(fb, (int)x1, (int)y1, (int)x1 + 1, (int)y1 + 1, c);
        else hud_line(fb, x0, y0, x1, y1, z < 0.3f ? 1.5f : 1.0f, c);
    }
}

const mode_def_t MODE_DEF_STARS = {
    .name = "STARS", .frameless = true, .enter = st_enter, .key = st_key, .update = st_update, .draw = st_draw,
};
