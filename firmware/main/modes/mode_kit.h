#pragma once
/*
 * What the mode files share (modes.c runs them; mode_music.c, mode_time.c and mode_fx.c hold them).
 * A mode is a table of callbacks; every one may be NULL.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/settings.h"
#include "drivers/buttons.h"
#include "drivers/buzzer.h"
#include "gfx/assets/fonts.h"
#include "gfx/hud.h"

extern uint32_t g_ms;                 /* this frame's time, esp_timer ms */
extern tg_settings_t g_cfg;           /* this frame's settings */

static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

bool mk_blink(void);                                              /* 400 ms on, 400 ms off */
void mk_beep(buzzer_pattern_t p);                                 /* at the set volume */
void mk_toast(const char *title, const char *value, float bar);   /* over the middle screen */
void mk_save(tg_settings_t *s);                                   /* store and use now */
/* g_cfg was changed in place (brightness, tuner A4, metronome, timer): keep it, store it once the
 * buttons have rested for 2.5 s (not on every press: flash wear) */
void mk_save_later(void);
void mk_title(hud_fb_t *fb, const char *title);                   /* the usual screen header */
void mk_softkey(hud_fb_t *fb, const char *label);                 /* what this screen's button does */
void mk_arrow(hud_fb_t *fb, int x, int y, int dir, int h, hud_color_t c);
void mk_level_bar(hud_fb_t *fb, int x0, int y0, int x1, int y1, float frac, int segs);
void mk_mmss(char *out, size_t n, int64_t us, bool hundredths);

/* Ringing (the timer, the alarm clock): wakes the device, full brightness, the melody every 1.3 s
 * for a minute, the LEDs flashing. Any button stops it (calling on_stop); LIGHT snoozes it instead
 * when on_snooze is given. */
void mk_ring(const char *what, void (*on_stop)(void), void (*on_snooze)(void));
bool mk_ringing(void);

enum { MK_MIC_PITCH = 1, MK_MIC_ONSETS = 2, MK_MIC_CHROMA = 4 };

typedef struct {
    const char *name;
    uint8_t mic;                                  /* MK_MIC_*: what the microphone must compute */
    bool frameless;                               /* no frame round the screens */
    void (*enter)(void);
    void (*leave)(void);
    void (*key)(const btn_event_t *e);            /* the mode's own buttons */
    bool (*escape)(void);                         /* MODE pressed: true if the mode used it (cancel an edit) */
    void (*update)(void);                         /* every frame while shown */
    void (*leds)(void);                           /* set the LED scene (default: none) */
    void (*draw)(hud_fb_t *fb, int screen);
} mode_def_t;

extern const mode_def_t MODE_DEF_TUNER, MODE_DEF_LONGTONE, MODE_DEF_TEMPO, MODE_DEF_CHORDS,
                        MODE_DEF_METRO;                                                       /* mode_music.c */
extern const mode_def_t MODE_DEF_CHRONO, MODE_DEF_TIMER, MODE_DEF_ALARM, MODE_DEF_WORLD;     /* mode_time.c */
extern const mode_def_t MODE_DEF_SOUND, MODE_DEF_STARS;                                      /* mode_fx.c */

void time_tick(void);                 /* mode_time.c: every frame, whatever shows (timer, alarm, chime) */
float music_tempo_bpm(void);          /* mode_music.c: the tempo finder's reading, NAN if none */
