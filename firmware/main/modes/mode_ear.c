/* EAR: ear training. The buzzer plays a note; sing or play it back and the tuner scores how close
 * you came, whatever the octave. Power: a new note. Left: play it again. Right: how close counts
 * (easy 50 cents, normal 25, hard 10). The LED strips show flat (left) or sharp (right) as you hold it. */
#include "modes/mode_kit.h"

#include <stdlib.h>

#include "esp_random.h"

#include "drivers/mic.h"
#include "ui/ledfx.h"

#define PLAY_MS   900                    /* the note */
#define ECHO_MS   350                    /* then the room's echo of it dies away */
#define ANSWER_MS 6000                   /* how long to wait for an answer */
#define HOLD_N    8                      /* readings that must agree (~0.25 s held) */

static const char *const NOTE[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const int TOL[3] = { 50, 25, 10 };
static const char *const TOL_NAME[3] = { "EASY", "NORMAL", "HARD" };

static enum { EA_IDLE, EA_PLAY, EA_LISTEN, EA_DONE } ea_state;
static int ea_target = -1;               /* MIDI note */
static uint32_t ea_t;                    /* when the state began */
static float ea_got = NAN, ea_err = NAN; /* the note sung (MIDI, fractional) and its cents off, octave-free */
static bool ea_ok;
static int ea_tol = 1, ea_streak, ea_best, ea_right, ea_total;
static float ea_reads[HOLD_N];
static int ea_nr;
static float ea_live = NAN;              /* what is heard now (MIDI), NAN = no clear pitch */
static unsigned ea_block;

static float midi_of(float hz) { return 69.0f + 12.0f * log2f(hz / g_cfg.tuner_a4); }
static float hz_of(int midi) { return g_cfg.tuner_a4 * powf(2.0f, (midi - 69) / 12.0f); }

static void name_of(char *out, size_t n, int midi)
{
    snprintf(out, n, "%s%d", NOTE[((midi % 12) + 12) % 12], midi / 12 - 1);
}

/* cents from the target's pitch class, -600..600 */
static float cents_off(float midi)
{
    float d = (midi - ea_target) * 100.0f;
    return d - 1200.0f * roundf(d / 1200.0f);
}

static void ea_play(void)
{
    if (ea_target < 0) return;
    if (g_cfg.buzzer_volume) buzzer_tone((int)lroundf(hz_of(ea_target)), PLAY_MS, g_cfg.buzzer_volume);
    else mk_toast("VOLUME", "IS AT 0", NAN);
    ea_state = EA_PLAY;
    ea_t = g_ms;
    ea_nr = 0;
    ea_got = ea_err = NAN;
}

static void ea_next(void)
{
    int prev = ea_target;
    do ea_target = 72 + (int)(esp_random() % 24); while (ea_target == prev);   /* C5 .. B6: the buzzer sings there */
    ea_play();
}

static void ea_enter(void)
{
    ea_state = EA_IDLE;
    ea_live = NAN;
}

static void ea_judge(float midi)
{
    ea_got = midi;
    ea_err = cents_off(midi);
    ea_ok = fabsf(ea_err) <= TOL[ea_tol];
    ea_total++;
    if (ea_ok) {
        ea_right++;
        if (++ea_streak > ea_best) ea_best = ea_streak;
        mk_beep(BZ_UP);
    } else {
        ea_streak = 0;
    }
    ea_state = EA_DONE;
    ea_t = g_ms;
}

static int cmpf(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return (x > y) - (x < y); }

static void ea_update(void)
{
    mic_snapshot_t m;
    mic_get(&m);
    bool fresh = m.blocks != ea_block;
    ea_block = m.blocks;
    ea_live = !buzzer_busy() && !isnan(m.pitch_hz) && m.pitch_clarity > 0.85f ? midi_of(m.pitch_hz) : NAN;
    if (ea_state == EA_PLAY && g_ms - ea_t > PLAY_MS + ECHO_MS) {
        ea_state = EA_LISTEN;
        ea_t = g_ms;
    }
    if (ea_state != EA_LISTEN) return;
    if (g_ms - ea_t > ANSWER_MS) {                    /* nothing held long enough */
        ea_state = EA_DONE;
        ea_got = ea_err = NAN;
        ea_t = g_ms;
        return;
    }
    if (!fresh) return;
    if (isnan(ea_live)) { ea_nr = 0; return; }
    if (ea_nr == HOLD_N) memmove(ea_reads, ea_reads + 1, (HOLD_N - 1) * sizeof(float)), ea_nr--;
    ea_reads[ea_nr++] = ea_live;
    if (ea_nr < HOLD_N) return;
    float sorted[HOLD_N];
    memcpy(sorted, ea_reads, sizeof sorted);
    qsort(sorted, HOLD_N, sizeof(float), cmpf);
    float med = (sorted[HOLD_N / 2 - 1] + sorted[HOLD_N / 2]) / 2;
    if (sorted[HOLD_N - 1] - sorted[0] <= 0.6f) ea_judge(med);     /* held steady: within 60 cents */
}

static void ea_key(const btn_event_t *e)
{
    if (e->kind == BTN_SHORT && e->key == KEY_POWER) {
        ea_next();
    } else if (e->kind == BTN_DOWN && !e->repeat && e->key == KEY_LEFT) {
        if (ea_target < 0) ea_next();
        else ea_play();
    } else if (e->kind == BTN_DOWN && !e->repeat && e->key == KEY_RIGHT) {
        ea_tol = (ea_tol + 1) % 3;
        char v[16];
        snprintf(v, sizeof v, "%s %d", TOL_NAME[ea_tol], TOL[ea_tol]);
        mk_toast("CLOSE ENOUGH", v, NAN);
    }
}

static void ea_leds(void)
{
    float c = ea_state == EA_LISTEN && !isnan(ea_live) && ea_target >= 0 ? cents_off(ea_live)
            : ea_state == EA_DONE && !isnan(ea_err) && g_ms - ea_t < 3000 ? ea_err : NAN;
    if (ea_state == EA_IDLE || ea_state == EA_PLAY) ledfx_scene(SCENE_NONE, 0, 0);
    else ledfx_scene(SCENE_TUNER, isnan(c) ? NAN : fmaxf(-60, fminf(60, c)), 0);
}

static void ea_draw(hud_fb_t *fb, int s)
{
    char t[24];
    switch (s) {
    case 0: {
        mk_title(fb, "EAR");
        const char *w = "READY";
        hud_color_t c = C_INK;
        if (ea_state == EA_PLAY) w = "LISTEN";
        else if (ea_state == EA_LISTEN) { w = "SING IT"; c = mk_blink() ? C_INK : C_LEGIBLE; }
        else if (ea_state == EA_DONE) {
            w = isnan(ea_got) ? "NO ANSWER" : ea_ok ? "RIGHT" : "NOT QUITE";
            c = isnan(ea_got) ? C_CAPTION : ea_ok ? C_SUCCESS : C_ORANGE;
        }
        const hud_font_t *f = hud_text_w(w, &font_o16, 0) <= 110 ? &font_o16 : &font_b12;
        hud_text(fb, 64, 62, w, f, c, "mm", 0);
        if (ea_state == EA_IDLE) hud_text(fb, 64, 86, "POWER: A NOTE", &font_b7, C_CAPTION, "mm", 1);
        mk_softkey(fb, "LIGHT");
        break;
    }
    case 1:
        mk_title(fb, "THE NOTE");
        if (ea_target >= 0 && ea_state == EA_DONE) name_of(t, sizeof t, ea_target);
        else snprintf(t, sizeof t, ea_target >= 0 ? "?" : "\xE2\x80\x94");
        hud_text(fb, 64, 64, t, &font_o28, C_INK, "mm", 0);
        if (ea_target >= 0 && ea_state == EA_DONE) {
            snprintf(t, sizeof t, "%.0f HZ", hz_of(ea_target));
            hud_text(fb, 64, 90, t, &font_b8, C_CAPTION, "mm", 1);
        }
        mk_softkey(fb, "REPLAY");
        break;
    case 2: {
        hud_text(fb, 64, 32, "YOU", &font_b8, C_LEGIBLE, "mm", 2);
        float shown = ea_state == EA_DONE ? ea_got : ea_live;
        if (isnan(shown)) snprintf(t, sizeof t, "\xE2\x80\x94");
        else name_of(t, sizeof t, (int)lroundf(shown));
        hud_text(fb, 64, 64, t, &font_o28, isnan(shown) ? C_CAPTION : C_INK, "mm", 0);
        if (!isnan(shown) && ea_target >= 0) {
            float c = cents_off(shown);
            snprintf(t, sizeof t, "%+.0f CENTS", c);
            hud_text(fb, 64, 92, t, &font_b8, fabsf(c) <= TOL[ea_tol] ? C_SUCCESS : C_CAPTION, "mm", 1);
        }
        mk_softkey(fb, "NEXT");
        break;
    }
    case 3: {
        mk_title(fb, "OFF BY");
        /* -60 .. +60 cents, the "close enough" zone lit */
        const int x0 = 14, x1 = 114, y = 60;
        float px = (x1 - x0) / 120.0f;
        int zl = (int)lroundf(64 - TOL[ea_tol] * px), zr = (int)lroundf(64 + TOL[ea_tol] * px);
        hud_rect(fb, x0, y, x1, y + 2, C_OFF);
        hud_rect(fb, zl, y - 1, zr, y + 3, C_P700);
        hud_rect(fb, 63, y - 6, 65, y + 8, C_CAPTION);
        float c = ea_state == EA_DONE ? ea_err : ea_state == EA_LISTEN && !isnan(ea_live) ? cents_off(ea_live) : NAN;
        if (!isnan(c)) {
            float x = 64 + fmaxf(-60, fminf(60, c)) * px;
            hud_color_t mc = fabsf(c) <= TOL[ea_tol] ? C_SUCCESS : C_ORANGE;
            hud_rect(fb, (int)x - 2, y - 9, (int)x + 2, y + 11, mc);
            snprintf(t, sizeof t, "%+.0f", c);
            hud_text(fb, 64, 88, t, &font_o16, mc, "mm", 0);
        }
        hud_text(fb, x0, y + 16, "FLAT", &font_b7, C_CAPTION, "lm", 1);
        hud_text(fb, x1, y + 16, "SHARP", &font_b7, C_CAPTION, "rm", 1);
        snprintf(t, sizeof t, "%s %d", TOL_NAME[ea_tol], TOL[ea_tol]);
        mk_softkey(fb, t);
        break;
    }
    default:
        mk_title(fb, "SCORE");
        snprintf(t, sizeof t, "%d", ea_streak);
        hud_text(fb, 64, 52, t, &font_o28, ea_streak ? C_INK : C_CAPTION, "mm", 0);
        hud_text(fb, 64, 74, "IN A ROW", &font_b7, C_CAPTION, "mm", 1);
        snprintf(t, sizeof t, "BEST %d \xC2\xB7 %d/%d", ea_best, ea_right, ea_total);
        hud_text(fb, 64, 94, t, &font_b8, C_LEGIBLE, "mm", 1);
        mk_softkey(fb, "MODE");
        break;
    }
}

const mode_def_t MODE_DEF_EAR = {
    .name = "EAR", .mic = MK_MIC_PITCH, .enter = ea_enter, .key = ea_key, .update = ea_update, .leds = ea_leds,
    .draw = ea_draw,
};
