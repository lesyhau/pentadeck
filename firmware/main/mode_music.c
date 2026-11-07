/* The music modes: tuner, long-tone practice, tempo finder, metronome. */
#include "mode_kit.h"

#include <stdlib.h>

#include "freertos/FreeRTOS.h"

#include "esp_timer.h"

#include "ledfx.h"
#include "mic.h"

static const char *NOTE[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
#define REF ((float)g_cfg.tuner_a4)    /* A4, shared by the tuner and the long tones (saved) */

/* A clean pitch reading from the mic, or NAN */
static float heard_hz(const mic_snapshot_t *m)
{
    if (isnan(m->pitch_hz) || m->pitch_clarity < 0.8f || m->pitch_hz < 38 || m->pitch_hz > 2400) return NAN;
    return m->pitch_hz;
}

static float midi_of(float hz) { return 69.0f + 12.0f * log2f(hz / REF); }

static void note_name(char *out, size_t n, int midi)
{
    snprintf(out, n, "%s%d", NOTE[((midi % 12) + 12) % 12], midi / 12 - 1);
}

static void ref_key(const btn_event_t *e)
{
    if (e->kind == BTN_DOWN && (e->key == KEY_LEFT || e->key == KEY_RIGHT)) {
        g_cfg.tuner_a4 = (uint16_t)clampi(g_cfg.tuner_a4 + (e->key == KEY_RIGHT ? 1 : -1), 430, 450);
        mk_save_later();
        char v[12];
        snprintf(v, sizeof v, "%d HZ", g_cfg.tuner_a4);
        mk_toast("A4 REFERENCE", v, NAN);
    }
}

/* ================================================================== tuner */

static bool s_hold;
static float s_hz = NAN, s_cents_shown = NAN, s_hist[5];
static int s_hist_n;
static uint32_t s_heard_at;

static void tuner_update(void)
{
    if (s_hold) return;
    mic_snapshot_t m;
    mic_get(&m);
    float hz = heard_hz(&m);
    if (!isnan(hz)) {
        /* the median of the last five readings throws out a stray octave */
        if (s_hist_n < 5) s_hist[s_hist_n++] = hz;
        else { memmove(s_hist, s_hist + 1, 4 * sizeof(float)); s_hist[4] = hz; }
        float v[5];
        memcpy(v, s_hist, s_hist_n * sizeof(float));
        for (int i = 1; i < s_hist_n; i++) for (int j = i; j > 0 && v[j] < v[j - 1]; j--) { float t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
        s_hz = v[s_hist_n / 2];
        float midi = midi_of(s_hz);
        float c = (midi - lroundf(midi)) * 100.0f;
        s_cents_shown = isnan(s_cents_shown) || fabsf(c - s_cents_shown) > 30 ? c : s_cents_shown + (c - s_cents_shown) * 0.35f;
        s_heard_at = g_ms;
    } else if (g_ms - s_heard_at > 700) {
        s_hz = s_cents_shown = NAN;
        s_hist_n = 0;
    }
}

static void tuner_key(const btn_event_t *e)
{
    ref_key(e);
    if (e->kind == BTN_SHORT && e->key == KEY_POWER) s_hold = !s_hold && !isnan(s_hz);
}

static void tuner_leave(void) { s_hold = false; }

/* the cents scale across screens 1..3 as one 384 px strip: -50 at x 12, 0 at 192, +50 at 372 */
#define SCALE_Y 96
static int cents_x(float c) { return 192 + (int)lroundf(fmaxf(-50, fminf(50, c)) * 3.6f); }

static void tuner_scale(hud_fb_t *fb, int screen)
{
    int off = (screen - 1) * 128;
    hud_rect(fb, 0, SCALE_Y, 128, SCALE_Y + 1, C_HUD_DIM);
    for (int c = -50; c <= 50; c += 5) {
        int x = cents_x(c) - off;
        if (x < 0 || x >= 128) continue;
        int len = c == 0 ? 9 : c % 25 == 0 ? 6 : c % 10 == 0 ? 4 : 2;
        hud_rect(fb, x, SCALE_Y - len, x + 1, SCALE_Y, c == 0 ? C_SUCCESS : c % 25 == 0 ? C_HUD : C_HUD_DIM);
        if (c % 25 == 0 && c) {
            char t[6];
            snprintf(t, sizeof t, "%+d", c);
            hud_text(fb, x, SCALE_Y + 7, t, &font_b7, C_CAPTION, "mm", 0);
        }
    }
    if (!isnan(s_cents_shown)) {
        int x = cents_x(s_cents_shown) - off;
        float a = fabsf(s_cents_shown);
        hud_color_t col = a <= 5 ? C_SUCCESS : a <= 25 ? C_ACCENT : C_ORANGE;
        if (x >= -2 && x < 130) {
            hud_rect(fb, x - 1, SCALE_Y - 16, x + 2, SCALE_Y + 3, col);
            hud_rect(fb, x - 3, SCALE_Y - 19, x + 4, SCALE_Y - 16, col);
        }
    }
}

static void tuner_draw(hud_fb_t *fb, int s)
{
    bool in_tune = !isnan(s_cents_shown) && fabsf(s_cents_shown) <= 5;
    char t[24];
    switch (s) {
    case 0:
        mk_title(fb, "TUNER");
        hud_text(fb, 64, 42, "A4 REFERENCE", &font_b7, C_CAPTION, "mm", 1);
        snprintf(t, sizeof t, "%.0f", REF);
        hud_text(fb, 64, 74, t, &font_o28, C_INK, "ms", 0);
        hud_text(fb, 64, 88, "HZ", &font_b8, C_LEGIBLE, "mm", 2);
        mk_softkey(fb, "LIGHT");
        break;
    case 1:
    case 3: {
        bool flat = s == 1;
        mk_title(fb, flat ? "FLAT" : "SHARP");
        float c = isnan(s_cents_shown) ? 0 : (flat ? -s_cents_shown : s_cents_shown);
        for (int i = 0; i < 3; i++) {                 /* chevrons at 5, 15 and 30 cents */
            bool on = c > (i == 0 ? 5 : i == 1 ? 15 : 30);
            hud_color_t col = on ? (i == 2 ? C_ORANGE : C_ACCENT) : C_OFF;
            int x = flat ? 90 - i * 18 : 38 + i * 18;
            mk_arrow(fb, x, 34, flat ? -1 : 1, 31, col);
        }
        tuner_scale(fb, s);
        mk_softkey(fb, flat ? "REF -" : "REF +");
        break;
    }
    case 2:
        if (isnan(s_hz)) {
            hud_text(fb, 64, 52, "LISTENING", &font_b9, (g_ms / 600) % 2 ? C_CAPTION : C_HUD, "mm", 2);
        } else {
            int n = (int)lroundf(midi_of(s_hz));
            const char *name = NOTE[((n % 12) + 12) % 12];
            int w = hud_text_w(name, &font_o48, 0);
            hud_text(fb, 64 - 6 - w / 2, 70, name, &font_o48, in_tune ? C_SUCCESS : C_INK, "ls", 0);
            snprintf(t, sizeof t, "%d", n / 12 - 1);
            hud_text(fb, 64 - 6 + w / 2 + 3, 70, t, &font_o16, C_LEGIBLE, "ls", 0);
        }
        tuner_scale(fb, s);
        mk_softkey(fb, s_hold ? "HOLD ON" : "HOLD");
        break;
    case 4:
        mk_title(fb, "PITCH");
        if (isnan(s_hz)) {
            hud_text(fb, 64, 62, "\xE2\x80\x94", &font_o22, C_CAPTION, "ms", 0);
        } else {
            snprintf(t, sizeof t, s_hz < 1000 ? "%.1f" : "%.0f", s_hz);
            hud_text(fb, 64, 60, t, &font_o22, C_INK, "ms", 0);
            hud_text(fb, 64, 72, "HZ", &font_b8, C_LEGIBLE, "mm", 2);
            snprintf(t, sizeof t, "%+.0f CENTS", s_cents_shown);
            hud_text(fb, 64, 92, t, &font_b9, in_tune ? C_SUCCESS : C_INK2, "mm", 1);
        }
        mk_softkey(fb, "MODE");
        break;
    }
}

static void tuner_leds(void) { ledfx_scene(SCENE_TUNER, s_cents_shown, 0); }

const mode_def_t MODE_DEF_TUNER = {
    .name = "TUNER", .mic = MK_MIC_PITCH, .leave = tuner_leave, .key = tuner_key, .update = tuner_update,
    .leds = tuner_leds, .draw = tuner_draw,
};

/* ============================================================= long tones */

/* One reading per mic block (31 a second), ten seconds: the trace runs across all five screens,
 * two pixels a reading, the newest on the right. */
#define LT_N 320
static float lt_cents[LT_N], lt_db[LT_N];
static int lt_w;
static unsigned lt_block;
static int lt_note = -1000;           /* the note being held, MIDI */
static uint32_t lt_start, lt_last_heard;
static float lt_sum, lt_sum2, lt_dur, lt_best;
static int lt_cnt;

static void lt_clear(void)
{
    for (int i = 0; i < LT_N; i++) lt_cents[i] = lt_db[i] = NAN;
    lt_note = -1000;
    lt_best = lt_dur = 0;
    lt_cnt = 0;
}

static void lt_enter(void)
{
    if (lt_block == 0) lt_clear();
}

static void lt_update(void)
{
    mic_snapshot_t m;
    mic_get(&m);
    if (m.blocks == lt_block) return;
    lt_block = m.blocks;
    float hz = heard_hz(&m), c = NAN;
    if (!isnan(hz)) {
        float midi = midi_of(hz);
        int n = (int)lroundf(midi);
        c = (midi - n) * 100.0f;
        if (n != lt_note) {                           /* a new tone starts */
            lt_note = n;
            lt_start = g_ms;
            lt_sum = lt_sum2 = 0;
            lt_cnt = 0;
        }
        lt_sum += c;
        lt_sum2 += c * c;
        lt_cnt++;
        lt_last_heard = g_ms;
        lt_dur = (g_ms - lt_start) / 1000.0f;
        if (lt_dur > lt_best) lt_best = lt_dur;
    } else if (lt_note > -1000 && g_ms - lt_last_heard > 300) {
        lt_note = -1000;                              /* the tone ended */
    }
    lt_cents[lt_w] = c;
    lt_db[lt_w] = m.dbfs;
    lt_w = (lt_w + 1) % LT_N;
}

static void lt_key(const btn_event_t *e)
{
    ref_key(e);
    if (e->kind == BTN_SHORT && e->key == KEY_POWER) lt_clear();
}

static int lt_y(float c) { return 66 - (int)lroundf(fmaxf(-50, fminf(50, c)) * 0.84f); }

static void lt_draw(hud_fb_t *fb, int s)
{
    char t[32];
    /* guides: in tune (green dashes), +-10 cents (dim) */
    for (int x = (s * 128) % 6 == 0 ? 0 : 6 - (s * 128) % 6; x < 128; x += 6) hud_rect(fb, x, 66, x + 3, 67, 0x1F5F3Fu);
    for (int x = 0; x < 128; x += 4) {
        hud_rect(fb, x, lt_y(10), x + 1, lt_y(10) + 1, C_HUD_DIM);
        hud_rect(fb, x, lt_y(-10), x + 1, lt_y(-10) + 1, C_HUD_DIM);
    }
    if (s == 0) {
        hud_text(fb, 4, lt_y(25), "+25", &font_b7, C_HUD_DIM, "lm", 0);
        hud_text(fb, 4, lt_y(-25), "-25", &font_b7, C_HUD_DIM, "lm", 0);
    }
    /* loudness: a dim fill along the bottom, then the pitch trace */
    int prev_y = -1;
    for (int i = LT_N - 1; i >= 0; i--) {             /* oldest first */
        int idx = (lt_w + LT_N - 1 - i) % LT_N;       /* i readings ago */
        int x = 638 - 2 * i - s * 128;
        if (x < -1 || x >= 128) { prev_y = -1; continue; }
        float db = lt_db[idx];
        if (!isnan(db)) {
            int h = (int)lroundf(clamp01((db + 70.0f) / 60.0f) * 18);
            if (h) hud_rect(fb, x, 109 - h, x + 2, 109, C_P900);
        }
        float c = lt_cents[idx];
        if (isnan(c)) { prev_y = -1; continue; }
        int y = lt_y(c);
        float a = fabsf(c);
        hud_color_t col = a <= 5 ? C_SUCCESS : a <= 15 ? C_ACCENT : C_ORANGE;
        int y0 = prev_y < 0 ? y : (prev_y < y ? prev_y : y), y1 = prev_y < 0 ? y : (prev_y > y ? prev_y : y);
        hud_rect(fb, x, y0, x + 2, y1 + 2, col);
        prev_y = y;
    }
    switch (s) {
    case 0:
        if (lt_note > -1000) { note_name(t, sizeof t, lt_note); hud_header(fb, &font_b9, t, 12, 2, 0, 0, C_INK, C_HUD_DIM); }
        else mk_title(fb, "LONG TONE");
        mk_softkey(fb, "LIGHT");
        break;
    case 1:                                           /* (the middle screen's top is the clock) */
        if (lt_cnt > 3) {
            float mean = lt_sum / lt_cnt, sd = sqrtf(fmaxf(0, lt_sum2 / lt_cnt - mean * mean));
            snprintf(t, sizeof t, "AVG %+.0f \xC2\xB7 SD %.0f", mean, sd);
            hud_header(fb, &font_b9, t, 12, 1, 0, 0, fabsf(mean) <= 5 ? C_SUCCESS : C_INK2, C_HUD_DIM);
        } else {
            mk_title(fb, "INTONATION");
        }
        mk_softkey(fb, "REF -");
        break;
    case 2: mk_softkey(fb, "CLEAR"); break;
    case 3:
        snprintf(t, sizeof t, "BEST %.1f S", lt_best);
        hud_header(fb, &font_b9, t, 12, 1, 0, 0, C_LEGIBLE, C_HUD_DIM);
        mk_softkey(fb, "REF +");
        break;
    case 4:
        if (lt_note > -1000) snprintf(t, sizeof t, "%.1f S", lt_dur);
        else snprintf(t, sizeof t, "HOLD A NOTE");
        hud_header(fb, &font_b9, t, 12, 1, 0, 0, lt_note > -1000 ? C_INK : C_LEGIBLE, C_HUD_DIM);
        mk_softkey(fb, "MODE");
        break;
    }
}

static void lt_leds(void)
{
    float c = lt_note > -1000 ? lt_cents[(lt_w + LT_N - 1) % LT_N] : NAN;
    ledfx_scene(SCENE_TUNER, c, 0);
}

const mode_def_t MODE_DEF_LONGTONE = {
    .name = "LONG TONE", .mic = MK_MIC_PITCH, .enter = lt_enter, .key = lt_key, .update = lt_update,
    .leds = lt_leds, .draw = lt_draw,
};

/* ================================================================== tempo */

/* The onset envelope (125 frames/s, 8 s) is autocorrelated over 50..220 BPM; each lag's score adds
 * half of the score at twice the lag (the bar-level repeat) and is weighted towards ~120 BPM (a
 * log-normal prior, one octave wide), which settles most half/double confusions. The beat's phase is
 * where a comb at that period catches the most onset over the last four seconds. */
#define TP_N     1000
#define TP_FPS   ((float)MIC_ONSET_HZ)
#define TP_LMIN  34                    /* 220 BPM */
#define TP_LMAX  150                   /* 50 BPM */
static float tp_env[MIC_ONSETS], tp_acf[2 * TP_LMAX + 2];
static float tp_found = NAN, tp_hist[6], tp_conf;
static int tp_hist_n, tp_oct;          /* tp_oct: -1 halve, 0, +1 double */
static bool tp_locked;
static uint32_t tp_est_at, tp_next_beat, tp_beat_n, tp_found_at;
static uint32_t tap_t[8];
static int tap_n;
static float tap_bpm = NAN;
static uint32_t tap_at;
/* HOLD: a tempo that stays put (tapped, or the detector's reading frozen) until released */
static bool tp_held;
static float tp_held_bpm = NAN;

static float tp_base(void) { return tp_held ? tp_held_bpm : tp_found; }

static float tp_shown(void)
{
    float b = tp_base();
    if (isnan(b)) return NAN;
    return b * (tp_oct > 0 ? 2.0f : tp_oct < 0 ? 0.5f : 1.0f);
}

static void tp_estimate(void)
{
    uint32_t last_ms;
    mic_onsets(tp_env, MIC_ONSETS, &last_ms);
    /* smooth the envelope (a 7-tap binomial, ~25 ms wide): onsets are one frame wide, so without
     * it a tempo whose period falls between whole frames, or a slightly uneven beat, loses its peak */
    static float sm[TP_N];
    const float *raw = tp_env + MIC_ONSETS - TP_N;
    static const float K[7] = { 1, 6, 15, 20, 15, 6, 1 };
    for (int i = 0; i < TP_N; i++) {
        float a = 0, w = 0;
        for (int k = -3; k <= 3; k++) if (i + k >= 0 && i + k < TP_N) { a += raw[i + k] * K[k + 3]; w += K[k + 3]; }
        sm[i] = a / w;
    }
    const float *o = sm;
    float mean = 0;
    for (int i = 0; i < TP_N; i++) mean += o[i];
    mean /= TP_N;
    static float x[TP_N];
    float e0 = 0;
    for (int i = 0; i < TP_N; i++) { x[i] = o[i] - mean; e0 += x[i] * x[i]; }
    if (mean < 0.02f || e0 < 1e-4f) {                 /* quiet or steady: nothing to lock on */
        tp_conf = 0;
        if (g_ms - tp_found_at > 4000) { tp_found = NAN; tp_locked = false; tp_hist_n = 0; }
        return;
    }
    for (int L = TP_LMIN; L <= 2 * TP_LMAX; L++) {
        float a = 0;
        for (int i = L; i < TP_N; i++) a += x[i] * x[i - L];
        tp_acf[L] = a / (TP_N - L);
    }
    float best = -1e9f;
    int bl = 0;
    static float score[TP_LMAX + 2];
    for (int L = TP_LMIN; L <= TP_LMAX; L++) {
        float bpm = 60.0f * TP_FPS / L, oct = log2f(bpm / 120.0f);
        float w = expf(-0.5f * oct * oct);
        score[L] = (tp_acf[L] + 0.5f * tp_acf[2 * L]) * w;
        if (score[L] > best) { best = score[L]; bl = L; }
    }
    float lag = bl;
    if (bl > TP_LMIN && bl < TP_LMAX) {
        float a = score[bl - 1], b = score[bl], c = score[bl + 1], den = a - 2 * b + c;
        if (den < 0) lag += 0.5f * (a - c) / den;
    }
    tp_conf = clamp01(tp_acf[bl] / (e0 / TP_N));
    if (tp_conf < 0.08f) return;
    float bpm = 60.0f * TP_FPS / lag;
    /* the shown tempo: the median of the last six estimates; "locked" when they agree within 2 % */
    if (tp_hist_n < 6) tp_hist[tp_hist_n++] = bpm;
    else { memmove(tp_hist, tp_hist + 1, 5 * sizeof(float)); tp_hist[5] = bpm; }
    float v[6];
    memcpy(v, tp_hist, tp_hist_n * sizeof(float));
    for (int i = 1; i < tp_hist_n; i++) for (int j = i; j > 0 && v[j] < v[j - 1]; j--) { float t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
    tp_found = v[tp_hist_n / 2];
    tp_locked = tp_hist_n >= 4 && (v[tp_hist_n - 1] - v[0]) / tp_found < 0.04f;
    tp_found_at = g_ms;

    /* phase: the offset where a comb at the found period gathers the most onset (last 4 s) */
    float period = 60.0f * TP_FPS / tp_found;
    int best_ph = 0;
    float best_sum = -1;
    for (int ph = 0; ph < (int)period; ph++) {
        float sum = 0;
        for (float k = 0; ph + k < 500; k += period) sum += o[TP_N - 1 - ph - (int)k];
        if (sum > best_sum) { best_sum = sum; best_ph = ph; }
    }
    if (tp_held) return;                              /* the held tempo keeps its own beat */
    uint32_t beat = last_ms - (uint32_t)(best_ph * 1000.0f / TP_FPS);
    float pms = 60000.0f / tp_shown();
    while ((int32_t)(beat - g_ms) <= 0) beat += (uint32_t)pms;
    tp_next_beat = beat;
}

static void tp_update(void)
{
    if (g_ms - tp_est_at >= 250) {
        tp_est_at = g_ms;
        tp_estimate();
    }
    float shown = tp_shown();
    if (!isnan(shown) && tp_next_beat && (int32_t)(g_ms - tp_next_beat) >= 0) {
        tp_beat_n++;
        ledfx_beat(tp_next_beat, tp_beat_n % 4 == 1);
        tp_next_beat += (uint32_t)(60000.0f / shown);
    }
}

static void tp_key(const btn_event_t *e)
{
    if (e->kind == BTN_DOWN && !e->repeat && e->key == KEY_POWER) {      /* tap: on the press, not the release */
        if (tap_n && g_ms - tap_t[(tap_n - 1) % 8] > 2000) tap_n = 0;
        tap_t[tap_n % 8] = g_ms;
        tap_n++;
        if (tap_n >= 2) {
            int k = tap_n < 8 ? tap_n : 8;
            uint32_t first = tap_t[(tap_n - k) % 8], last = tap_t[(tap_n - 1) % 8];
            tap_bpm = 60000.0f * (k - 1) / (float)(last - first);
            tap_at = g_ms;
            /* tapping holds the tapped tempo; the last tap is a beat */
            tp_held = true;
            tp_held_bpm = tap_bpm;
            tp_oct = 0;
            tp_beat_n = 0;
            tp_next_beat = g_ms + (uint32_t)(60000.0f / tap_bpm);
        }
    } else if (e->kind == BTN_DOWN && !e->repeat && e->key == KEY_LEFT) {
        if (tp_held) {                                /* release: listen again */
            tp_held = false;
            tp_next_beat = 0;
        } else if (!isnan(tp_found)) {                /* hold what was heard, beat and all */
            tp_held = true;
            tp_held_bpm = tp_found;
        }
    } else if (e->kind == BTN_DOWN && !e->repeat && e->key == KEY_RIGHT) {
        tp_oct = tp_oct == 0 ? 1 : tp_oct > 0 ? -1 : 0;    /* 1x -> 2x -> 1/2x -> 1x */
        float shown = tp_shown();
        if (tp_held && !isnan(shown)) tp_next_beat = g_ms + (uint32_t)(60000.0f / shown);
        else tp_next_beat = 0;
    }
}

static void tp_leds(void)
{
    if (!isnan(tp_shown())) ledfx_scene(SCENE_BEAT, 0, 0);
    else ledfx_scene(SCENE_NONE, 0, 0);
}

static void tp_draw(hud_fb_t *fb, int s)
{
    char t[24];
    float shown = tp_shown();
    switch (s) {
    case 0:
        mk_title(fb, "TEMPO");
        if (tp_held) hud_text(fb, 64, 52, "HELD", &font_o14, C_ACCENT, "mm", 1);
        else hud_text(fb, 64, 52, isnan(shown) ? "LISTENING" : tp_locked ? "LOCKED" : "FINDING", &font_o14,
                      isnan(shown) ? ((g_ms / 600) % 2 ? C_CAPTION : C_HUD) : tp_locked ? C_SUCCESS : C_INK2, "mm", 1);
        hud_text(fb, 64, 76, "CONFIDENCE", &font_b7, C_CAPTION, "mm", 1);
        mk_level_bar(fb, 24, 84, 104, 89, clamp01((tp_conf - 0.08f) / 0.45f), 10);
        mk_softkey(fb, "LIGHT");
        break;
    case 1: {
        mk_title(fb, "BEAT");
        int cur = isnan(shown) ? -1 : (int)((tp_beat_n + 3) % 4);
        bool fresh = !isnan(shown) && g_ms - (tp_next_beat - (uint32_t)(60000.0f / shown)) < 150;
        for (int i = 0; i < 4; i++) {
            int x = 14 + i * 26;
            hud_color_t c = i == cur && fresh ? (i == 0 ? C_ORANGE : C_ACCENT) : i == cur ? C_HUD_DIM : C_OFF;
            hud_rect(fb, x, 48, x + 22, 70, c);
        }
        mk_softkey(fb, tp_held ? "RELEASE" : "HOLD");
        break;
    }
    case 2:
        if (isnan(shown)) snprintf(t, sizeof t, "--");
        else snprintf(t, sizeof t, "%d", (int)lroundf(shown));
        hud_text(fb, 64, 74, t, &font_o48, isnan(shown) ? C_CAPTION : tp_held ? C_ACCENT : tp_locked ? C_INK : C_INK2, "ms", 0);
        hud_text(fb, 64, 92, tp_oct ? (tp_oct > 0 ? "BPM \xC2\xB7 DOUBLED" : "BPM \xC2\xB7 HALVED") : tp_held ? "BPM \xC2\xB7 HELD" : "BPM", &font_b8, C_LEGIBLE, "mm", 1);
        mk_softkey(fb, "TAP");
        break;
    case 3:
        mk_title(fb, "TAP");
        if (isnan(tap_bpm)) snprintf(t, sizeof t, "\xE2\x80\x94");
        else snprintf(t, sizeof t, "%.0f", tap_bpm);
        hud_text(fb, 64, 64, t, &font_o22, isnan(tap_bpm) ? C_CAPTION : C_INK, "ms", 0);
        snprintf(t, sizeof t, tap_n ? "%d TAPS" : "TAP POWER", tap_n);
        hud_text(fb, 64, 82, t, &font_b8, C_LEGIBLE, "mm", 1);
        mk_softkey(fb, "OCTAVE");
        break;
    case 4:
        mk_title(fb, "OCTAVES");
        if (!isnan(tp_base())) {
            float b = tp_base();
            snprintf(t, sizeof t, "%.0f", b / 2);
            hud_text(fb, 23, 56, t, &font_o14, tp_oct < 0 ? C_INK : C_CAPTION, "mm", 0);
            snprintf(t, sizeof t, "%.0f", b);
            hud_text(fb, 64, 56, t, &font_o14, tp_oct == 0 ? C_INK : C_CAPTION, "mm", 0);
            snprintf(t, sizeof t, "%.0f", b * 2);
            hud_text(fb, 105, 56, t, &font_o14, tp_oct > 0 ? C_INK : C_CAPTION, "mm", 0);
            hud_text(fb, 23, 72, "1/2", &font_b8, C_LEGIBLE, "mm", 0);
            hud_text(fb, 64, 72, "1X", &font_b8, C_LEGIBLE, "mm", 0);
            hud_text(fb, 105, 72, "2X", &font_b8, C_LEGIBLE, "mm", 0);
        }
        hud_text(fb, 64, 96, "METRONOME USES IT", &font_b7, C_CAPTION, "mm", 1);
        mk_softkey(fb, "MODE");
        break;
    }
}

const mode_def_t MODE_DEF_TEMPO = {
    .name = "TEMPO", .mic = MK_MIC_ONSETS, .key = tp_key, .update = tp_update, .leds = tp_leds, .draw = tp_draw,
};

float music_tempo_bpm(void) { return tp_shown(); }

/* The tempo the metronome should start from: a tap or a locked reading from the last two minutes. */
static float tempo_found(void)
{
    if (tp_held) return tp_shown();                   /* a held tempo, however old */
    bool tap_ok = !isnan(tap_bpm) && g_ms - tap_at < 120000;
    bool det_ok = tp_locked && !isnan(tp_shown()) && g_ms - tp_found_at < 120000;
    if (tap_ok && (!det_ok || tap_at > tp_found_at)) return tap_bpm;
    return det_ok ? tp_shown() : NAN;
}

/* ============================================================== metronome */

static bool s_mt_run;
static esp_timer_handle_t s_mt_timer;
static volatile uint32_t s_beat_n, s_beat_ms;
static float s_handed = NAN;          /* the tempo last taken from the tempo finder */

static void beat_cb(void *arg)
{
    uint32_t n = s_beat_n + 1;
    bool accent = (n - 1) % 4 == 0;
    s_beat_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_beat_n = n;
    tg_settings_t c;
    settings_get(&c);
    if (c.buzzer_volume) buzzer_play(accent ? BZ_ACCENT : BZ_TICK, c.buzzer_volume);
    ledfx_beat(s_beat_ms, accent);
}

static void metro_set(bool run)
{
    if (!s_mt_timer) {
        const esp_timer_create_args_t a = { .callback = beat_cb, .name = "metronome" };
        esp_timer_create(&a, &s_mt_timer);
    }
    esp_timer_stop(s_mt_timer);
    s_mt_run = run;
    if (run) {
        s_beat_n = 0;
        beat_cb(NULL);
        esp_timer_start_periodic(s_mt_timer, 60000000ULL / g_cfg.metro_bpm);
    }
}

static void metro_enter(void)
{
    float f = tempo_found();
    if (!isnan(f) && f != s_handed) {
        s_handed = f;
        g_cfg.metro_bpm = (uint16_t)clampi((int)lroundf(f), 30, 240);
        mk_save_later();
        char v[12];
        snprintf(v, sizeof v, "%d BPM", g_cfg.metro_bpm);
        mk_toast("FROM TEMPO", v, NAN);
    }
}

static void metro_leave(void) { if (s_mt_run) metro_set(false); }

static void metro_key(const btn_event_t *e)
{
    if (e->kind == BTN_SHORT && e->key == KEY_POWER) {
        metro_set(!s_mt_run);
    } else if (e->kind == BTN_DOWN && (e->key == KEY_LEFT || e->key == KEY_RIGHT)) {
        g_cfg.metro_bpm = (uint16_t)clampi(g_cfg.metro_bpm + (e->key == KEY_RIGHT ? 1 : -1), 30, 240);
        mk_save_later();
        if (s_mt_run) {
            esp_timer_stop(s_mt_timer);
            esp_timer_start_periodic(s_mt_timer, 60000000ULL / g_cfg.metro_bpm);
        }
    }
}

static const char *tempo_name(int bpm)
{
    return bpm < 60 ? "LARGO" : bpm < 76 ? "ADAGIO" : bpm < 108 ? "ANDANTE" : bpm < 120 ? "MODERATO" : bpm < 168 ? "ALLEGRO" : "PRESTO";
}

static void metro_leds(void)
{
    if (s_mt_run) ledfx_scene(SCENE_BEAT, 0, 0);
    else ledfx_scene(SCENE_NONE, 0, 0);
}

static void metro_draw(hud_fb_t *fb, int s)
{
    char t[24];
    uint32_t n = s_beat_n;
    float period = 60000.0f / g_cfg.metro_bpm;
    float frac = s_mt_run ? clamp01((float)(g_ms - s_beat_ms) / period) : 0;
    switch (s) {
    case 0:
        mk_title(fb, "METRONOME");
        hud_text(fb, 64, 62, "4/4", &font_o22, C_INK, "ms", 0);
        hud_text(fb, 64, 84, tempo_name(g_cfg.metro_bpm), &font_b9, C_LEGIBLE, "mm", 2);
        mk_softkey(fb, "LIGHT");
        break;
    case 1: {
        /* the pendulum: at one end on every beat, crossing the middle halfway between */
        float side = (n % 2) ? 1.0f : -1.0f;
        float th = s_mt_run ? side * cosf((float)M_PI * frac) * 0.42f : 0;
        float px = 64, py = 104, len = 74;
        hud_bezel(fb, px, py, len + 6, 6.0f, 0, C_HUD_DIM, C_HUD_DIM);
        hud_line(fb, px, py, px + sinf(th) * len, py - cosf(th) * len, 2.0f, C_ACCENT);
        hud_circle(fb, px + sinf(th) * (len - 22), py - cosf(th) * (len - 22), 5.0f, C_INK);
        hud_rect(fb, 54, 104, 75, 107, C_HUD);
        mk_softkey(fb, "SLOWER");
        break;
    }
    case 2:
        snprintf(t, sizeof t, "%d", g_cfg.metro_bpm);
        hud_text(fb, 64, 74, t, &font_o48, s_mt_run ? C_INK : C_INK2, "ms", 0);
        hud_text(fb, 64, 92, "BPM", &font_b8, C_LEGIBLE, "mm", 2);
        mk_softkey(fb, s_mt_run ? "STOP" : "START");
        break;
    case 3: {
        mk_title(fb, "BEAT");
        int cur = s_mt_run ? (int)((n - 1) % 4) : -1;
        for (int i = 0; i < 4; i++) {
            int x = 14 + i * 26;
            bool on = i == cur && frac < 0.5f;
            hud_color_t c = on ? (i == 0 ? C_ORANGE : C_ACCENT) : (i == cur ? C_HUD_DIM : C_OFF);
            hud_rect(fb, x, 48, x + 22, 70, c);
        }
        snprintf(t, sizeof t, "%d / 4", cur < 0 ? 0 : cur + 1);
        hud_text(fb, 64, 88, t, &font_b9, C_INK2, "mm", 1);
        mk_softkey(fb, "FASTER");
        break;
    }
    case 4:
        mk_title(fb, "TEMPO");
        snprintf(t, sizeof t, "%.2f", 60.0f / g_cfg.metro_bpm);
        hud_text(fb, 64, 62, t, &font_o22, C_INK, "ms", 0);
        hud_text(fb, 64, 76, "SEC / BEAT", &font_b8, C_LEGIBLE, "mm", 1);
        mk_softkey(fb, "MODE");
        break;
    }
}

const mode_def_t MODE_DEF_METRO = {
    .name = "METRONOME", .enter = metro_enter, .leave = metro_leave, .key = metro_key, .leds = metro_leds,
    .draw = metro_draw,
};

/* ================================================================= chords */

/* The chord: the last ~0.5 s of pitch classes matched against the 24 major and minor triads (cosine
 * similarity), changing only when two readings agree. The key: pitch classes averaged over the last
 * ~30 s, correlated with the Krumhansl-Kessler major and minor key profiles. Both are worked out on
 * every fourth mic frame (256 ms); the NOTES bars follow every frame (64 ms), eased each screen frame.
 * The key is shown the way a dizi is marked: "1 = X", the do of the major scale that shares the song's
 * notes (a minor song rests on la, 6, of its relative major). */
static const char *CH_ROOT[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
static const char *KEY_MAJ[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
static const char *KEY_MIN[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B" };
static const float KK_MAJ[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
static const float KK_MIN[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
static const char *DEGREE[12] = { "I", "bII", "II", "bIII", "III", "IV", "#IV", "V", "bVI", "VI", "bVII", "VII" };

static const int MAJ_STEP[7] = { 0, 2, 4, 5, 7, 9, 11 };   /* do re mi fa sol la si, in semitones */

static float ch_short[12], ch_long[12];
static float ch_bar[12], ch_bar_shown[12];            /* the NOTES bars: per frame, and eased */
static uint32_t ch_frame, ch_slow_frame, ch_frames;
static int ch_chord = -1, ch_cand = -1, ch_cand_n;   /* 0..11 major, 12..23 minor, -1 none */
static int ch_hist[6], ch_hist_n;
static int ch_key = -1;                               /* 0..11 major, 12..23 minor */
static float ch_key_conf;
static bool ch_hold, ch_sound;

static void ch_reset(void)
{
    memset(ch_long, 0, sizeof ch_long);
    ch_frames = 0;
    ch_key = -1;
    ch_hist_n = 0;
    ch_chord = ch_cand = -1;
}

static float corr(const float *a, const float *b)
{
    float ma = 0, mb = 0;
    for (int i = 0; i < 12; i++) { ma += a[i]; mb += b[i]; }
    ma /= 12;
    mb /= 12;
    float sab = 0, saa = 0, sbb = 0;
    for (int i = 0; i < 12; i++) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return saa > 0 && sbb > 0 ? sab / sqrtf(saa * sbb) : 0;
}

static void ch_update(void)
{
    if (ch_hold) return;
    for (int i = 0; i < 12; i++) ch_bar_shown[i] += (ch_bar[i] - ch_bar_shown[i]) * 0.45f;
    float v[12], e;
    uint32_t fr;
    if (!mic_chroma(v, &fr, &e) || fr == ch_frame) return;
    ch_frame = fr;
    mic_snapshot_t m;
    mic_get(&m);
    float mx = 0;
    for (int i = 0; i < 12; i++) if (v[i] > mx) mx = v[i];
    ch_sound = m.dbfs > -58.0f && mx > 0;
    for (int i = 0; i < 12; i++) ch_bar[i] = ch_sound ? 0.5f * ch_bar[i] + 0.5f * v[i] / mx : ch_bar[i] * 0.75f;
    if (fr - ch_slow_frame < 4) return;               /* the chord and the key: every 256 ms, as tuned */
    ch_slow_frame = fr;
    if (!ch_sound) {                                  /* silence: let the short memory fade */
        for (int i = 0; i < 12; i++) ch_short[i] *= 0.6f;
        ch_cand = -1;
        return;
    }
    for (int i = 0; i < 12; i++) {
        float c = v[i] / mx;
        ch_short[i] = 0.45f * ch_short[i] + 0.55f * c;
        ch_long[i] = ch_long[i] * 0.992f + c;         /* ~30 s memory */
    }
    ch_frames++;
    /* the chord */
    float n2 = 0;
    for (int i = 0; i < 12; i++) n2 += ch_short[i] * ch_short[i];
    float best = 0;
    int bi = -1;
    for (int q = 0; q < 24; q++) {
        int r = q % 12, third = q < 12 ? 4 : 3;
        float s = (ch_short[r] + ch_short[(r + third) % 12] + ch_short[(r + 7) % 12]) / (sqrtf(3.0f * n2) + 1e-9f);
        if (s > best) { best = s; bi = q; }
    }
    if (best < 0.80f) bi = -1;                        /* chords score 0.87+, a lone note at most 0.75 */
    if (bi == ch_cand) ch_cand_n++;
    else { ch_cand = bi; ch_cand_n = 1; }
    if (ch_cand_n >= 2 && ch_cand != ch_chord) {
        ch_chord = ch_cand;
        if (ch_chord >= 0) {
            memmove(ch_hist + 1, ch_hist, 5 * sizeof(int));
            ch_hist[0] = ch_chord;
            if (ch_hist_n < 6) ch_hist_n++;
        }
    }
    /* the key, once there is enough to go on (~3 s of sound) */
    if (ch_frames >= 12) {
        float r1 = -2, r2 = -2, rot[12];
        int k1 = -1;
        for (int t = 0; t < 12; t++) {
            for (int i = 0; i < 12; i++) rot[i] = ch_long[(t + i) % 12];
            float a = corr(rot, KK_MAJ), b = corr(rot, KK_MIN);
            if (a > r1) { r2 = r1; r1 = a; k1 = t; } else if (a > r2) r2 = a;
            if (b > r1) { r2 = r1; r1 = b; k1 = t + 12; } else if (b > r2) r2 = b;
        }
        ch_key = k1;
        ch_key_conf = r1 - r2;
    }
}

static void ch_key_fn(const btn_event_t *e)
{
    if (e->kind == BTN_SHORT && e->key == KEY_POWER) {
        ch_reset();
        mk_toast("NEW SONG", "LISTENING", NAN);
    } else if (e->kind == BTN_DOWN && !e->repeat && e->key == KEY_LEFT) {
        ch_hold = !ch_hold;
    }
}

static void ch_leave(void) { ch_hold = false; }

/* a note of the major scale on do (deg 0..6 = do..si), spelt for that key: "Bb", "E#" */
static void scale_note(char *out, size_t n, int doh, int deg)
{
    static const char LET[] = "CDEFGAB";
    int l = ((int)(strchr(LET, KEY_MAJ[doh][0]) - LET) + deg) % 7;
    int acc = (doh + MAJ_STEP[deg] - MAJ_STEP[l] + 24) % 12;    /* 1 sharp, 11 flat */
    snprintf(out, n, "%c%s", LET[l], acc == 1 ? "#" : acc == 11 ? "b" : "");
}

static void chord_name(char *out, size_t n, int q)
{
    snprintf(out, n, "%s%s", CH_ROOT[q % 12], q >= 12 ? "m" : "");
}

/* the chord's degree in the key: "IV", "vi", "bVII" (lower case for minor chords) */
static void degree(char *out, size_t n, int q)
{
    if (ch_key < 0) { out[0] = 0; return; }
    snprintf(out, n, "%s", DEGREE[(q % 12 - ch_key % 12 + 12) % 12]);
    if (q >= 12) for (char *c = out; *c; c++) if (*c == 'I' || *c == 'V') *c += 32;
}

static void ch_draw(hud_fb_t *fb, int s)
{
    char t[32], d[8];
    int tonic = ch_key < 0 ? 0 : ch_key % 12;
    bool minor = ch_key >= 12;
    int doh = minor ? (tonic + 3) % 12 : tonic;       /* the dizi's key: where do is */
    switch (s) {
    case 0: {
        mk_title(fb, "KEY");
        if (ch_key < 0) {
            hud_text(fb, 64, 60, "LISTENING", &font_b9, (g_ms / 600) % 2 ? C_CAPTION : C_HUD, "mm", 2);
        } else {
            hud_text(fb, 64, 74, KEY_MAJ[doh], &font_o48, ch_hold ? C_ACCENT : C_SUCCESS, "ms", 0);
            if (minor) snprintf(t, sizeof t, "%s MINOR \xC2\xB7 6 = %s", KEY_MIN[tonic], KEY_MIN[tonic]);
            else snprintf(t, sizeof t, "MAJOR");
            hud_text(fb, 64, 89, t, &font_b8, C_LEGIBLE, "mm", 1);
            mk_level_bar(fb, 34, 100, 94, 103, clamp01(ch_key_conf / 0.25f), 8);
        }
        mk_softkey(fb, "LIGHT");
        break;
    }
    case 1: {
        /* the dizi's notes from all holes closed (low sol) up: 5 6 7 1 2 3 4; the loudest note lit */
        mk_title(fb, "DIZI");
        if (ch_key < 0) {
            hud_text(fb, 64, 64, "\xE2\x80\x94", &font_o28, C_CAPTION, "ms", 0);
        } else {
            static const int DEG[7] = { 4, 5, 6, 0, 1, 2, 3 };
            int hot = -1;
            float hm = 0.3f;
            if (ch_sound) for (int i = 0; i < 12; i++) if (ch_bar_shown[i] > hm) { hm = ch_bar_shown[i]; hot = i; }
            for (int j = 0; j < 7; j++) {
                int x = 10 + j * 18, deg = DEG[j];
                bool on = (doh + MAJ_STEP[deg]) % 12 == hot;
                hud_color_t c = on ? C_SUCCESS : deg == 0 ? C_INK : C_INK2;
                t[0] = (char)('1' + deg);
                t[1] = 0;
                hud_text(fb, x, 58, t, &font_o16, c, "mm", 0);
                if (j < 3) hud_circle(fb, x, 69, 1.5f, c);   /* the low octave: a dot below */
                scale_note(t, sizeof t, doh, deg);
                hud_text(fb, x, 83, t, &font_b8, on ? C_SUCCESS : C_LEGIBLE, "mm", 0);
            }
        }
        mk_softkey(fb, ch_hold ? "RELEASE" : "HOLD");
        break;
    }
    case 2:
        if (ch_chord >= 0) {
            chord_name(t, sizeof t, ch_chord);
            hud_text(fb, 64, 70, t, &font_o48, ch_hold ? C_ACCENT : C_INK, "ms", 0);
            degree(d, sizeof d, ch_chord);
            hud_text(fb, 64, 90, d[0] ? d : "CHORD", &font_b9, C_LEGIBLE, "mm", 1);
        } else {
            hud_text(fb, 64, 62, ch_sound ? "?" : "--", &font_o28, C_CAPTION, "ms", 0);
            hud_text(fb, 64, 90, ch_sound ? "NO CLEAR CHORD" : "PLAY SOMETHING", &font_b8, C_CAPTION, "mm", 1);
        }
        mk_softkey(fb, "NEW SONG");
        break;
    case 3:
        mk_title(fb, "RECENT");
        for (int i = 0; i < ch_hist_n; i++) {
            int x = i % 2 ? 92 : 36, y = 38 + (i / 2) * 22;
            chord_name(t, sizeof t, ch_hist[i]);
            hud_text(fb, x, y, t, &font_o16, i == 0 ? C_INK : C_INK2, "mm", 0);
            degree(d, sizeof d, ch_hist[i]);
            if (d[0]) hud_text(fb, x, y + 10, d, &font_b7, C_CAPTION, "mm", 0);
        }
        if (!ch_hist_n) hud_text(fb, 64, 60, "\xE2\x80\x94", &font_b12, C_CAPTION, "mm", 0);
        break;
    case 4: {
        mk_title(fb, "NOTES");
        float mx = 0;
        for (int i = 0; i < 12; i++) if (ch_bar_shown[i] > mx) mx = ch_bar_shown[i];
        static const bool black[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
        static const char *nat[12] = { "C", "", "D", "", "E", "F", "", "G", "", "A", "", "B" };
        for (int i = 0; i < 12; i++) {
            int x = 10 + i * 9;
            int h = mx > 0.02f ? (int)lroundf(ch_bar_shown[i] / mx * 58) : 0;
            bool tone = false;
            if (ch_chord >= 0) {
                int r = ch_chord % 12, th = ch_chord >= 12 ? 3 : 4;
                tone = i == r || i == (r + th) % 12 || i == (r + 7) % 12;
            }
            hud_rect(fb, x, 88 - 58, x + 7, 88, C_OFF);
            if (h) hud_rect(fb, x, 88 - h, x + 7, 88, tone ? C_INK : black[i] ? C_HUD : C_ACCENT);
            if (nat[i][0]) hud_text(fb, x + 4, 96, nat[i], &font_b7, tone ? C_INK : C_CAPTION, "mm", 0);
        }
        mk_softkey(fb, "MODE");
        break;
    }
    }
}

const mode_def_t MODE_DEF_CHORDS = {
    .name = "CHORDS", .mic = MK_MIC_CHROMA, .leave = ch_leave, .key = ch_key_fn, .update = ch_update,
    .draw = ch_draw,
};
