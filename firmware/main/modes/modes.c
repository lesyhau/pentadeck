/* The mode machine: which mode shows, the buttons every mode shares, sleep and the LIGHT boost, the
 * toast, ringing, and the SETUP mode. The other modes live in mode_music.c, mode_time.c, mode_fx.c. */
#include "modes/modes.h"

#include <time.h>

#include "freertos/FreeRTOS.h"

#include "esp_random.h"

#include "app.h"
#include "core/listen.h"
#include "core/power.h"
#include "drivers/mic.h"
#include "modes/mode_kit.h"
#include "net/netdata.h"
#include "ui/ledfx.h"

uint32_t g_ms;
tg_settings_t g_cfg;

static ui_mode_t s_mode = MODE_DASH;
static bool s_sleep;
static btn_key_t s_swallow = KEY_NONE;    /* the button whose press woke the device from low power */
static uint32_t s_boost_until;        /* LIGHT: full brightness until then */
static char s_prev_led[3][8] = { "cpu", "lab", "gpu" };   /* the zones' effects a long LIGHT turns back on */
static uint32_t s_save_at;            /* a pending save of g_cfg's in-place changes, 0 = none */

static struct {
    char title[20], value[24];
    float bar;                        /* 0..1, NAN = none */
    uint32_t until, gen;
} s_toast;

/* a notification: what the web posts (any task), and what shows */
typedef struct { char title[24], text[96]; int seconds, sound; uint32_t color; } note_t;
static portMUX_TYPE s_note_mux = portMUX_INITIALIZER_UNLOCKED;
static note_t s_note_in;
static volatile uint32_t s_note_in_gen;
static uint32_t s_note_seen;
static struct { bool on; note_t n; uint32_t until, gen; } s_note;
static uint32_t s_heard_seen, s_dose_seen, s_claps_seen;
static int s_learn_was = -1;
static uint32_t s_learn_left;

static struct {
    bool on;
    char what[12];
    uint32_t since, beep;
    void (*on_stop)(void), (*on_snooze)(void);
} s_ring;

/* ------------------------------------------------------------------ kit */

bool mk_blink(void) { return (g_ms / 400) % 2 == 0; }

void mk_beep(buzzer_pattern_t p)
{
    if (g_cfg.buzzer_volume) buzzer_play(p, g_cfg.buzzer_volume);
}

void mk_toast(const char *title, const char *value, float bar)
{
    strlcpy(s_toast.title, title, sizeof s_toast.title);
    strlcpy(s_toast.value, value, sizeof s_toast.value);
    s_toast.bar = bar;
    s_toast.until = g_ms + 1600;
    s_toast.gen++;
}

void mk_save_later(void)
{
    s_save_at = g_ms + 2500;
}

void mk_save(tg_settings_t *s)
{
    settings_set(s);
    g_cfg = *s;
}

void mk_title(hud_fb_t *fb, const char *title)
{
    hud_header(fb, &font_b9, title, 12, 2, 0, 0, C_LEGIBLE, C_HUD_DIM);
}

void mk_softkey(hud_fb_t *fb, const char *label)
{
    if (label && *label) hud_text(fb, 64, 117, label, &font_b7, C_CAPTION, "mm", 1);
}

/* a solid triangle pointing left (-1) or right (+1), row by row on whole pixels; x is its flat side */
void mk_arrow(hud_fb_t *fb, int x, int y, int dir, int h, hud_color_t c)
{
    for (int r = 0; r < h; r++) {
        int len = r < h / 2 ? r + 1 : h - r;
        if (dir > 0) hud_rect(fb, x, y + r, x + len, y + r + 1, c);
        else hud_rect(fb, x - len + 1, y + r, x + 1, y + r + 1, c);
    }
}

/* a level bar in one colour (settings are levels, not loads: no amber / red end) */
void mk_level_bar(hud_fb_t *fb, int x0, int y0, int x1, int y1, float frac, int segs)
{
    float w = (float)(x1 - x0 - (segs - 1)) / segs;
    int lit = (int)lroundf(clamp01(frac) * segs);
    for (int i = 0; i < segs; i++) {
        int sx = (int)lroundf(x0 + i * (w + 1)), ex = (int)lroundf(x0 + i * (w + 1) + w);
        hud_rect(fb, sx, y0, ex, y1, i < lit ? C_ACCENT : C_OFF);
    }
}

void mk_mmss(char *out, size_t n, int64_t us, bool hundredths)
{
    if (us < 0) us = 0;
    int64_t cs = us / 10000;
    int m = (int)(cs / 6000), s = (int)(cs / 100 % 60), c = (int)(cs % 100);
    if (hundredths) snprintf(out, n, "%02d:%02d.%02d", m, s, c);
    else snprintf(out, n, "%02d:%02d", m, s);
}

void mk_ring(const char *what, void (*on_stop)(void), void (*on_snooze)(void))
{
    strlcpy(s_ring.what, what, sizeof s_ring.what);
    s_ring.on = true;
    s_ring.since = g_ms;
    s_ring.beep = g_ms - 2000;
    s_ring.on_stop = on_stop;
    s_ring.on_snooze = on_snooze;
    s_sleep = false;
}

bool mk_ringing(void) { return s_ring.on; }

/* ---------------------------------------------------------------- pairing */

static struct {
    volatile uint32_t id;             /* 0 = none */
    volatile uint16_t code;
    volatile int state;               /* 0 waiting, 1 allowed, 2 refused */
    volatile uint32_t since;
    volatile bool ask;                /* show the prompt */
} s_pair;
static uint32_t s_pair_last;

uint32_t modes_pair_start(uint16_t *code)
{
    uint32_t now = g_ms;
    if (s_pair.ask || (s_pair_last && now - s_pair_last < 5000)) return 0;
    uint32_t id;
    do id = esp_random(); while (!id);
    s_pair.code = (uint16_t)(1000 + esp_random() % 9000);
    s_pair.state = 0;
    s_pair.since = now;
    s_pair.id = id;
    s_pair.ask = true;
    s_pair_last = now;
    *code = s_pair.code;
    return id;
}

int modes_pair_state(uint32_t id)
{
    if (!id || id != s_pair.id) return 2;
    return s_pair.state;
}

void modes_pair_done(uint32_t id)
{
    if (id && id == s_pair.id) s_pair.id = 0;
}

static void pair_answer(bool allow)
{
    s_pair.state = allow ? 1 : 2;
    s_pair.ask = false;
    s_pair.since = g_ms;                 /* the browser has 30 s to collect the answer */
    if (allow) mk_beep(BZ_UP);
}

void modes_notify(const char *title, const char *text, int seconds, int sound, uint32_t color)
{
    portENTER_CRITICAL(&s_note_mux);
    strlcpy(s_note_in.title, title ? title : "", sizeof s_note_in.title);
    strlcpy(s_note_in.text, text ? text : "", sizeof s_note_in.text);
    s_note_in.seconds = seconds < 0 ? 0 : seconds > 3600 ? 3600 : seconds;
    s_note_in.sound = sound;
    s_note_in.color = color & 0xFFFFFFu;
    s_note_in_gen++;
    portEXIT_CRITICAL(&s_note_mux);
}

static void note_show(const note_t *n)
{
    s_note.n = *n;
    s_note.on = true;
    s_note.until = g_ms + (uint32_t)n->seconds * 1000;
    s_note.gen++;
    s_sleep = false;
    power_wake("notification");
    bool quiet = g_cfg.quiet_night && settings_is_night(&g_cfg, netdata_time_valid());
    if (n->sound >= 0 && !quiet) mk_beep((buzzer_pattern_t)n->sound);
}

static void note_local(const char *title, const char *text, int seconds, int sound, uint32_t color)
{
    note_t n = { .seconds = seconds, .sound = sound, .color = color };
    strlcpy(n.title, title, sizeof n.title);
    strlcpy(n.text, text, sizeof n.text);
    note_show(&n);
}

static void ring_end(bool snooze)
{
    s_ring.on = false;
    if (snooze && s_ring.on_snooze) s_ring.on_snooze();
    else if (s_ring.on_stop) s_ring.on_stop();
}

/* ------------------------------------------------------------------ setup */

#define SETUP_ITEMS 14
static int s_sel;
static bool s_edit;
static tg_settings_t s_draft;
static const char *LED_MODES[] = { "off", "glow", "status", "music" };    /* settings.c's presets */
/* settings.c's AUDIO_VIEWS, and what the menu calls them */
static const char *AUDIO_VIEWS[][2] = { { "eq", "EQ" }, { "vu", "VU METER" }, { "mirror", "MIRROR" }, { "scope", "SCOPE" },
                                        { "waterfall", "WATERFALL" }, { "ripple", "RIPPLE" }, { "radial", "RADIAL" } };
#define AUDIO_VIEW_N (int)(sizeof AUDIO_VIEWS / sizeof AUDIO_VIEWS[0])

static int audio_view_index(const char *key)
{
    for (int j = 0; j < AUDIO_VIEW_N; j++) if (strcmp(key, AUDIO_VIEWS[j][0]) == 0) return j;
    return 0;
}

static const char *item_name(int i)
{
    static const char *n[SETUP_ITEMS] = { "BRIGHTNESS", "NIGHT LEVEL", "LED MODE", "LED LEVEL", "VOLUME",
                                          "ALERTS", "KEY BEEP", "QUIET NIGHT", "LOW POWER", "IDLE LEVEL",
                                          "CLOCK", "AUDIO VIEW", "ACCENT", "DEVICE" };
    return n[i];
}

static void item_value(const tg_settings_t *s, int i, char *out, size_t n, float *bar)
{
    *bar = NAN;
    switch (i) {
    case 0: snprintf(out, n, "%d%%", s->brightness); *bar = s->brightness / 100.0f; break;
    case 1: snprintf(out, n, "%d%%", s->night_brightness); *bar = s->night_brightness / 100.0f; break;
    case 2: snprintf(out, n, "%s", settings_led_preset_of(s)); for (char *c = out; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32; break;
    case 3: snprintf(out, n, "%d%%", s->led_brightness); *bar = s->led_brightness / 100.0f; break;
    case 4: snprintf(out, n, "%d%%", s->buzzer_volume); *bar = s->buzzer_volume / 100.0f; break;
    case 5: snprintf(out, n, "%s", s->buzzer ? "ON" : "OFF"); break;
    case 6: snprintf(out, n, "%s", s->key_beep ? "ON" : "OFF"); break;
    case 7: snprintf(out, n, "%s", s->quiet_night ? "ON" : "OFF"); break;
    case 8: if (s->idle_min) snprintf(out, n, "%d MIN", s->idle_min); else snprintf(out, n, "OFF"); break;
    case 9: snprintf(out, n, "%d%%", s->idle_brightness); *bar = s->idle_brightness / 100.0f; break;
    case 10: snprintf(out, n, "%s", s->hour24 ? "24H" : "12H"); break;
    case 11: snprintf(out, n, "%s", AUDIO_VIEWS[audio_view_index(s->audio_view)][1]); break;
    case 12: snprintf(out, n, "%s", settings_accent_name(s->accent)); break;
    default: snprintf(out, n, "%s", app_fw_version()); break;
    }
}

static void item_step(tg_settings_t *s, int i, int d)
{
    switch (i) {
    case 0: s->brightness = (uint8_t)clampi(s->brightness + d * 5, 5, 100); break;
    case 1: s->night_brightness = (uint8_t)clampi(s->night_brightness + d * 5, 0, 100); break;
    case 2: {
        int k = d > 0 ? -1 : 0;                       /* custom zones (set on the web page): to the first or last preset */
        for (int j = 0; j < 4; j++) if (strcmp(settings_led_preset_of(s), LED_MODES[j]) == 0) k = j;
        settings_led_preset(s, LED_MODES[(k + d + 4) % 4]);
        break;
    }
    case 3: s->led_brightness = (uint8_t)clampi(s->led_brightness + d * 5, 0, 100); break;
    case 4:
        s->buzzer_volume = (uint8_t)clampi(s->buzzer_volume + d * 5, 0, 100);
        if (s->buzzer_volume) buzzer_play(BZ_KEY, s->buzzer_volume);       /* hear the new level */
        break;
    case 5: s->buzzer = !s->buzzer; break;
    case 6: s->key_beep = !s->key_beep; break;
    case 7: s->quiet_night = !s->quiet_night; break;
    case 8: {                                         /* off, 2 .. 120 minutes */
        static const uint8_t m[] = { 0, 2, 5, 10, 15, 20, 30, 45, 60, 90, 120 };
        int k = 0, c = (int)(sizeof m / sizeof m[0]);
        for (int j = 0; j < c; j++) if (m[j] <= s->idle_min) k = j;
        s->idle_min = m[clampi(k + d, 0, c - 1)];
        break;
    }
    case 9: s->idle_brightness = (uint8_t)clampi(s->idle_brightness + d * 5, 0, 100); break;
    case 10: s->hour24 = !s->hour24; break;
    case 11:
        strlcpy(s->audio_view, AUDIO_VIEWS[(audio_view_index(s->audio_view) + d + AUDIO_VIEW_N) % AUDIO_VIEW_N][0],
                sizeof s->audio_view);
        break;
    case 12: {
        int k = d > 0 ? -1 : 0;                       /* a custom colour (from the web page): to the first or last preset */
        for (int j = 0; j < TG_ACCENT_N; j++) if (TG_ACCENTS[j].rgb == s->accent) k = j;
        s->accent = TG_ACCENTS[(k + d + TG_ACCENT_N) % TG_ACCENT_N].rgb;
        break;
    }
    default: break;
    }
}

static void setup_key(const btn_event_t *e)
{
    if (e->kind == BTN_DOWN && (e->key == KEY_LEFT || e->key == KEY_RIGHT)) {
        int d = e->key == KEY_RIGHT ? 1 : -1;
        if (s_edit) item_step(&s_draft, s_sel, d);
        else if (!e->repeat) s_sel = (s_sel + d + SETUP_ITEMS) % SETUP_ITEMS;
    } else if (e->kind == BTN_SHORT && e->key == KEY_POWER) {
        if (s_sel == SETUP_ITEMS - 1) return;             /* DEVICE is read-only */
        if (!s_edit) {
            s_draft = g_cfg;
            s_edit = true;
        } else {
            mk_save(&s_draft);
            s_edit = false;
            mk_beep(BZ_UP);
        }
    }
}

static bool setup_escape(void)
{
    if (!s_edit) return false;
    s_edit = false;
    return true;
}

static void setup_leave(void) { s_edit = false; }

static void setup_draw(hud_fb_t *fb, int s)
{
    int page = s_sel / 5, i = page * 5 + s;
    if (i >= SETUP_ITEMS) {                       /* the last page isn't full */
        static const char *k[5] = { "LIGHT", "PREV", "", "NEXT", "MODE" };
        if (k[s][0]) mk_softkey(fb, k[s]);
        return;
    }
    bool sel = i == s_sel;
    const tg_settings_t *v = sel && s_edit ? &s_draft : &g_cfg;
    hud_header(fb, &font_b8, item_name(i), 12, 1, 0, 0, sel ? C_INK : C_LEGIBLE, sel ? C_HUD : C_HUD_DIM);
    char t[24];
    float bar;
    if (i == SETUP_ITEMS - 1) {
        hud_text(fb, 64, 40, "FIRMWARE", &font_b7, C_CAPTION, "mm", 1);
        hud_text(fb, 64, 54, app_fw_version(), &font_o16, C_INK, "mm", 0);
        hud_text(fb, 64, 74, "ADDRESS", &font_b7, C_CAPTION, "mm", 1);
        hud_text(fb, 64, 88, app_ip(), &font_b9, C_INK, "mm", 0);
    } else {
        item_value(v, i, t, sizeof t, &bar);
        bool show = !(sel && s_edit) || mk_blink();
        const hud_font_t *f = hud_text_w(t, &font_o22, 0) <= 100 ? &font_o22 : &font_o16;
        if (show) hud_text(fb, 64, 62, t, f, sel && s_edit ? C_INK : C_INK2, "ms", 0);
        if (sel && s_edit) {
            mk_arrow(fb, 10, 48, -1, 13, C_ACCENT);
            mk_arrow(fb, 117, 48, 1, 13, C_ACCENT);
        }
        if (!isnan(bar)) mk_level_bar(fb, 20, 78, 108, 84, bar, 12);
        if (i == 12) hud_rect(fb, 20, 78, 108, 84, v->accent);   /* the colour itself (it applies once saved) */
    }
    snprintf(t, sizeof t, "%02d / %02d", i + 1, SETUP_ITEMS);
    hud_text(fb, 64, 100, t, &font_b7, sel ? C_LEGIBLE : C_HUD_DIM, "mm", 1);
    if (sel) hud_rect(fb, 8, 106, 120, 107, C_ACCENT);
    const char *k[5] = { "LIGHT", s_edit ? "-" : "PREV", s_edit ? "SAVE" : "EDIT", s_edit ? "+" : "NEXT", s_edit ? "CANCEL" : "MODE" };
    mk_softkey(fb, k[s]);
}

static const mode_def_t MODE_DEF_SETUP = {
    .name = "SETUP", .leave = setup_leave, .key = setup_key, .escape = setup_escape, .draw = setup_draw,
};

/* ---------------------------------------------------------- dispatching */

static const mode_def_t *def_of(ui_mode_t m)
{
    switch (m) {
    case MODE_TUNER:    return &MODE_DEF_TUNER;
    case MODE_LONGTONE: return &MODE_DEF_LONGTONE;
    case MODE_TEMPO:    return &MODE_DEF_TEMPO;
    case MODE_CHORDS:   return &MODE_DEF_CHORDS;
    case MODE_EAR:      return &MODE_DEF_EAR;
    case MODE_METRO:    return &MODE_DEF_METRO;
    case MODE_CHRONO:   return &MODE_DEF_CHRONO;
    case MODE_TIMER:    return &MODE_DEF_TIMER;
    case MODE_ALARM:    return &MODE_DEF_ALARM;
    case MODE_WORLD:    return &MODE_DEF_WORLD;
    case MODE_SOUND:    return &MODE_DEF_SOUND;
    case MODE_STARS:    return &MODE_DEF_STARS;
    case MODE_VISUAL:   return &MODE_DEF_VISUAL;
    case MODE_SETUP:    return &MODE_DEF_SETUP;
    default:            return NULL;
    }
}

static void enter(ui_mode_t m)
{
    const mode_def_t *old = def_of(s_mode), *now = def_of(m);
    if (old && old->leave) old->leave();
    s_mode = m;
    if (now && now->enter) now->enter();
}

static void dash_key(const btn_event_t *e)
{
    if (e->kind == BTN_DOWN && (e->key == KEY_LEFT || e->key == KEY_RIGHT)) {
        bool night = settings_is_night(&g_cfg, netdata_time_valid());
        uint8_t *lv = night ? &g_cfg.night_brightness : &g_cfg.brightness;
        *lv = (uint8_t)clampi(*lv + (e->key == KEY_RIGHT ? 5 : -5), night ? 0 : 5, 100);
        char v[8];
        snprintf(v, sizeof v, "%d%%", *lv);
        mk_toast(night ? "NIGHT LEVEL" : "BRIGHTNESS", v, *lv / 100.0f);
        mk_save_later();
    } else if (e->kind == BTN_SHORT && e->key == KEY_POWER) {
        mk_toast("ADDRESS", app_ip(), NAN);
    }
}

/* the LEDs off, or back to what they were (a long LIGHT, a clap) */
static void leds_toggle(void)
{
    tg_settings_t c = g_cfg;
    bool off = settings_leds_off(&c);
    if (!off) memcpy(s_prev_led, c.led_zone, sizeof s_prev_led);
    if (off) memcpy(c.led_zone, s_prev_led, sizeof c.led_zone);
    else settings_led_preset(&c, "off");
    mk_save(&c);
    char v[16];
    snprintf(v, sizeof v, "%s", off ? settings_led_preset_of(&c) : "OFF");
    for (char *p = v; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    mk_toast("LEDS", v, NAN);
}

static void clap(void)
{
    if (s_sleep) { s_sleep = false; return; }        /* asleep: a clap only wakes it */
    if (s_ring.on) { ring_end(true); return; }        /* snooze (or stop, with nothing to snooze) */
    if (s_note.on) { s_note.on = false; return; }
    const char *a = g_cfg.clap;
    if (strcmp(a, "leds") == 0) {
        leds_toggle();
    } else if (strcmp(a, "light") == 0) {
        s_boost_until = g_ms + 10000;
        mk_toast("CLAP", "FULL BRIGHTNESS", NAN);
    } else if (strcmp(a, "mode") == 0) {
        enter((ui_mode_t)((s_mode + 1) % MODE_COUNT));
        mk_beep(BZ_MODE);
    } else if (strcmp(a, "dash") == 0 && s_mode != MODE_DASH) {
        enter(MODE_DASH);
        mk_beep(BZ_MODE);
    }
}

static void key(const btn_event_t *e)
{
    if (s_ring.on) {                                  /* LIGHT snoozes; anything else stops */
        if (e->kind == BTN_DOWN) ring_end(e->key == KEY_LIGHT);
        return;
    }
    if (s_note.on) {                                  /* any button closes it, and does nothing else */
        if (e->kind == BTN_DOWN && !e->repeat) {
            s_note.on = false;
            s_swallow = e->key;
        }
        return;
    }
    if (s_pair.ask) {                                 /* power allows; anything else refuses */
        if (e->kind == BTN_SHORT && e->key == KEY_POWER) pair_answer(true);
        else if (e->kind == BTN_SHORT || e->kind == BTN_LONG ||
                 (e->kind == BTN_DOWN && (e->key == KEY_LEFT || e->key == KEY_RIGHT))) {
            if (e->key != KEY_POWER) pair_answer(false);   /* on the release, so MODE doesn't also switch */
        }
        return;
    }
    const mode_def_t *d = def_of(s_mode);
    if (e->key == KEY_MODE) {
        if (e->kind == BTN_SHORT) {
            if (d && d->escape && d->escape()) return;
            enter((ui_mode_t)((s_mode + 1) % MODE_COUNT));
            mk_beep(BZ_MODE);
        } else if (e->kind == BTN_LONG && s_mode != MODE_DASH) {
            enter(MODE_DASH);
            mk_beep(BZ_MODE);
        }
        return;
    }
    if (e->key == KEY_LIGHT) {
        if (e->kind == BTN_SHORT) {
            s_boost_until = g_ms + 10000;
        } else if (e->kind == BTN_LONG) {
            leds_toggle();
        }
        return;
    }
    if (e->key == KEY_POWER && e->kind == BTN_LONG) {
        s_sleep = true;
        return;
    }
    if (s_mode == MODE_DASH) dash_key(e);
    else if (d && d->key) d->key(e);
}

void modes_init(void)
{
    settings_get(&g_cfg);
}

void modes_update(uint32_t ms)
{
    g_ms = ms;
    tg_settings_t fresh;
    settings_get(&fresh);
    if (!s_save_at) g_cfg = fresh;          /* while a dashboard brightness change waits, keep ours */

    btn_event_t e;
    while (buttons_get(&e)) {
        if (s_sleep) {                       /* the press that wakes the device does nothing else */
            if (e.kind == BTN_DOWN && !e.repeat) s_sleep = false;
            power_wake("button");
            continue;
        }
        if (power_idle()) {                  /* same from low power: the whole press is swallowed */
            power_wake("button");
            if (e.kind == BTN_DOWN && !e.repeat) s_swallow = e.key;
            continue;
        }
        power_wake("button");
        if (e.key == s_swallow) {            /* the rest of the press that woke us (its release, repeats) */
            if (e.kind == BTN_DOWN && !e.repeat) {
                s_swallow = KEY_NONE;        /* a new press: act on it */
            } else {
                if (e.kind != BTN_DOWN) s_swallow = KEY_NONE;
                continue;
            }
        }
        if (e.kind == BTN_DOWN && !e.repeat && g_cfg.key_beep && e.key != KEY_MODE && !s_ring.on) mk_beep(BZ_KEY);
        key(&e);
    }
    if (s_save_at && (int32_t)(ms - s_save_at) >= 0) {
        s_save_at = 0;
        tg_settings_t c = fresh;                     /* what the web may have changed meanwhile, plus ours */
        c.brightness = g_cfg.brightness;
        c.night_brightness = g_cfg.night_brightness;
        c.tuner_a4 = g_cfg.tuner_a4;
        c.metro_bpm = g_cfg.metro_bpm;
        c.timer_min = g_cfg.timer_min;
        c.viz_effect = g_cfg.viz_effect;
        c.viz_palette = g_cfg.viz_palette;
        mk_save(&c);
    }

    time_tick();

    /* what came from elsewhere: a notification, a learned sound, the day's dose, a clap, learning */
    if (s_note_in_gen != s_note_seen) {
        note_t n;
        portENTER_CRITICAL(&s_note_mux);
        n = s_note_in;
        s_note_seen = s_note_in_gen;
        portEXIT_CRITICAL(&s_note_mux);
        note_show(&n);
    }
    char heard[LISTEN_NAME];
    uint32_t hg = listen_heard(NULL, heard);
    if (hg != s_heard_seen) {
        s_heard_seen = hg;
        note_local("HEARD", heard, 15, -1, 0xFF7A00u);
    }
    uint32_t dg = listen_dose_alerts();
    if (dg != s_dose_seen) {
        s_dose_seen = dg;
        if (g_cfg.noise_alert) note_local("SOUND DOSE", "100% for today. Time for some quiet.", 20, BZ_CHIME, 0xFFB000u);
    }
    unsigned cg = mic_double_claps();
    if (cg != s_claps_seen) {
        s_claps_seen = cg;
        if (strcmp(g_cfg.clap, "off") != 0) clap();
    }
    uint32_t left;
    int learning = listen_learning(&left);
    if (learning >= 0 && (learning != s_learn_was || (left + 999) / 1000 != s_learn_left)) {
        s_learn_left = (left + 999) / 1000;
        char v[20];
        snprintf(v, sizeof v, "MAKE IT  %lus", (unsigned long)s_learn_left);
        mk_toast("LISTENING", v, NAN);
        s_sleep = false;
        power_wake("learning");
    } else if (learning < 0 && s_learn_was >= 0) {
        mk_toast("LEARNING", listen_learn_result() == 1 ? "DONE" : "HEARD NOTHING", NAN);
    }
    s_learn_was = learning;
    if (s_note.on && s_note.n.seconds && (int32_t)(ms - s_note.until) >= 0) s_note.on = false;

    if (s_pair.ask && g_ms - s_pair.since > 60000) pair_answer(false);          /* nobody answered */
    if (!s_pair.ask && s_pair.id && g_ms - s_pair.since > 30000) s_pair.id = 0;  /* never collected */
    if (s_pair.ask && s_sleep) s_sleep = false;
    if (s_pair.ask || s_ring.on) power_wake(s_ring.on ? "alarm" : "pairing");
    if (s_ring.on) {
        if (ms - s_ring.beep > 1300) {
            s_ring.beep = ms;
            mk_beep(BZ_ALARM);
        }
        if (ms - s_ring.since > 60000) ring_end(false);
    }

    const mode_def_t *d = def_of(s_mode);
    if (d && d->update) d->update();
    uint8_t mic = d && !s_sleep ? d->mic : 0;
    mic_tuner(mic & MK_MIC_PITCH);
    mic_tempo(mic & MK_MIC_ONSETS);
    mic_chroma_on(mic & MK_MIC_CHROMA);

    if (s_ring.on) ledfx_scene(SCENE_ALARM, 0, 0);
    else if (s_note.on) ledfx_scene(SCENE_NOTIFY, 0, (float)s_note.n.color);
    else if (d && d->leds) d->leds();
    else ledfx_scene(SCENE_NONE, 0, 0);
    ledfx_sleep(s_sleep);
}

ui_mode_t modes_current(void) { return s_mode; }

const char *modes_name(void)
{
    const mode_def_t *d = def_of(s_mode);
    return d ? d->name : "DASH";
}

float modes_tempo_bpm(void) { return music_tempo_bpm(); }
bool modes_sleeping(void) { return s_sleep; }

int modes_backlight(int base_pct)
{
    if (s_sleep) return 0;
    if ((int32_t)(s_boost_until - g_ms) > 0 || s_ring.on) return 100;
    if (power_idle()) return base_pct < g_cfg.idle_brightness ? base_pct : g_cfg.idle_brightness;
    if (s_save_at) {                         /* show a dashboard brightness change straight away */
        bool night = settings_is_night(&g_cfg, netdata_time_valid());
        return night ? g_cfg.night_brightness : g_cfg.brightness;
    }
    return base_pct;
}

bool modes_toast_active(void)
{
    return s_ring.on || s_pair.ask || s_note.on || (int32_t)(s_toast.until - g_ms) > 0;
}

uint32_t modes_toast_gen(void)
{
    return s_toast.gen * 4 + (s_ring.on ? 1 + mk_blink() : 0) + (s_pair.ask ? 3 + s_pair.code * 8u : 0) +
           (s_note.on ? s_note.gen * 7919u : 0);
}

/* `text` in lines of at most max_w px, word by word; the last line cut with "…" when it doesn't fit */
static int wrap(const char *text, const hud_font_t *f, int max_w, char lines[][48], int max_lines)
{
    int n = 0;
    char cur[48] = "";
    const char *p = text;
    while (*p && n < max_lines) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *e = p;
        while (*e && *e != ' ') e++;
        char word[48];
        snprintf(word, sizeof word, "%.*s", (int)(e - p), p);
        char next[100];
        snprintf(next, sizeof next, "%s%s%s", cur, cur[0] ? " " : "", word);
        if (!cur[0] || hud_text_w(next, f, 0) <= max_w) {
            strlcpy(cur, next, sizeof cur);
            p = e;
            continue;
        }
        if (n == max_lines - 1) break;                /* the rest goes on the last line, cut */
        strlcpy(lines[n++], cur, sizeof lines[0]);
        cur[0] = 0;
    }
    if (n < max_lines && cur[0]) {
        char last[160];
        snprintf(last, sizeof last, "%s%s%s", cur, *p ? " " : "", p);
        hud_ellipsize(lines[n++], sizeof lines[0], last, f, max_w, 0);
    }
    return n;
}

static void draw_note(hud_fb_t *fb)
{
    hud_color_t edge = s_note.n.color ? s_note.n.color : C_ACCENT;
    hud_rect(fb, 6, 26, 122, 114, C_CANVAS);
    hud_rect(fb, 6, 26, 122, 27, edge);
    hud_rect(fb, 6, 113, 122, 114, edge);
    hud_rect(fb, 6, 26, 7, 114, edge);
    hud_rect(fb, 121, 26, 122, 114, edge);
    char title[24];
    hud_ellipsize(title, sizeof title, s_note.n.title[0] ? s_note.n.title : "NOTE", &font_b8, 104, 1);
    hud_text(fb, 64, 37, title, &font_b8, edge, "mm", 1);
    char lines[5][48];
    const hud_font_t *f = &font_b12;
    int n = wrap(s_note.n.text, f, 106, lines, 3), step = 16;
    if (n == 3 && hud_text_w(s_note.n.text, f, 0) > 3 * 100) {   /* long: smaller, five lines */
        f = &font_b9;
        n = wrap(s_note.n.text, f, 106, lines, 5);
        step = 11;
    }
    int y = 72 - (n - 1) * step / 2;
    for (int i = 0; i < n; i++) hud_text(fb, 64, y + i * step, lines[i], f, C_INK, "mm", 0);
    hud_text(fb, 64, 105, "ANY BUTTON: CLOSE", &font_b7, C_CAPTION, "mm", 1);
}

void modes_draw_toast(hud_fb_t *fb)
{
    if (s_pair.ask && !s_ring.on) {
        char code[8];
        snprintf(code, sizeof code, "%u", s_pair.code);
        hud_rect(fb, 6, 30, 122, 104, C_CANVAS);
        hud_rect(fb, 6, 30, 122, 31, C_ACCENT);
        hud_rect(fb, 6, 103, 122, 104, C_ACCENT);
        hud_rect(fb, 6, 30, 7, 104, C_ACCENT);
        hud_rect(fb, 121, 30, 122, 104, C_ACCENT);
        hud_text(fb, 64, 41, "PAIR THIS BROWSER?", &font_b7, C_LEGIBLE, "mm", 1);
        hud_text(fb, 64, 72, code, &font_o28, C_INK, "ms", 0);
        hud_text(fb, 64, 84, "SAME CODE ON THE PAGE?", &font_b7, C_CAPTION, "mm", 0);
        hud_text(fb, 64, 96, "POWER: ALLOW \xC2\xB7 OTHER: NO", &font_b7, C_ACCENT, "mm", 0);
        return;
    }
    if (s_note.on && !s_ring.on) {
        draw_note(fb);
        return;
    }
    const char *title = s_ring.on ? s_ring.what : s_toast.title;
    const char *value = s_ring.on ? (s_ring.on_snooze ? "LIGHT: SNOOZE" : "TIME UP") : s_toast.value;
    float bar = s_ring.on ? NAN : s_toast.bar;
    hud_color_t edge = s_ring.on ? (mk_blink() ? C_ORANGE : C_OFF) : C_ACCENT;
    hud_rect(fb, 12, 36, 116, 100, C_CANVAS);
    hud_rect(fb, 12, 36, 116, 37, edge);
    hud_rect(fb, 12, 99, 116, 100, edge);
    hud_rect(fb, 12, 36, 13, 100, edge);
    hud_rect(fb, 115, 36, 116, 100, edge);
    hud_text(fb, 64, 48, title, &font_b8, C_LEGIBLE, "mm", 2);
    const hud_font_t *f = hud_text_w(value, &font_o22, 0) <= 96 ? &font_o22 : &font_b12;
    hud_text(fb, 64, isnan(bar) ? 78 : 76, value, f, s_ring.on ? C_ORANGE : C_INK, "ms", 0);
    if (!isnan(bar)) mk_level_bar(fb, 22, 85, 106, 91, bar, 12);
}

void modes_draw(hud_fb_t *fb, int screen)
{
    const mode_def_t *d = def_of(s_mode);
    if (!d) return;
    if (!d->frameless) {
        hud_color_t frame = s_ring.on ? (mk_blink() ? C_ORANGE : C_HUD_DIM) : C_HUD;
        if (s_mode == MODE_SETUP) frame = s_sel % 5 == screen ? C_ACCENT : C_HUD_DIM;
        hud_frame(fb, frame, C_P900, (int)(g_ms / 500));
    }
    if (d->draw) d->draw(fb, screen);
    /* the clock heads the middle screen in every mode (not SETUP: its headers name the settings), on its
     * own in the dashboard clock's Orbitron, no rule */
    if (screen == 2 && s_mode != MODE_SETUP && netdata_time_valid()) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        char t[8];
        strftime(t, sizeof t, g_cfg.hour24 ? "%H:%M" : "%I:%M", &tm);
        hud_text(fb, 64, 12, t, &font_o14, C_LEGIBLE, "mm", 0);
    }
}
