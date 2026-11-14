#pragma once
/*
 * The device's modes, Casio-style. MODE steps through them; a long MODE goes back to the dashboard.
 *
 *   DASH      the cockpit (ui.c). Left / right: screen brightness. Power: show the IP address.
 *   TUNER     chromatic tuner from the microphone (40 Hz .. 2.3 kHz). Left / right: A4 reference.
 *             Power: hold the reading.
 *   LONGTONE  long-tone practice: a ten-second trace of the pitch (cents) and loudness across all five
 *             screens, the held note's average and spread, how long it lasted. Power: clear.
 *   TEMPO     finds the beat of the music it hears (and flashes it). Power: tap the tempo (taps are
 *             held). Left: hold what was heard / release a hold. Right: octave (1x, 2x, 1/2x).
 *             The metronome starts from the tempo held or found here.
 *   CHORDS    names the chord being played and the song's key the way a dizi is marked ("1 = Bb":
 *             the major scale sharing the song's notes), with the dizi's notes in that key and the
 *             pitch classes heard. Power: new song. Left: hold.
 *   EAR       ear training: the buzzer plays a note, sing or play it back; scored by the tuner, whatever
 *             the octave. Power: a new note. Left: again. Right: how close counts (50, 25, 10 cents).
 *   METRO     metronome. Left / right: tempo. Power: start / stop.
 *   CHRONO    stopwatch. Power: start / stop. Left: lap while running, reset while stopped.
 *   TIMER     countdown. Left / right: minutes. Power: start / pause. Left while paused: reset.
 *   ALARM     daily alarm and hourly chime. Left: alarm on / off. Right: chime on / off. Power: set
 *             (hour, minute, days in turn). A ringing alarm: LIGHT snoozes 5 min, any other button stops.
 *   WORLD     home time in the middle, two other places (settings) either side, UTC, and who is awake
 *             when. Left / right: shift every clock by 30 min (planning a call). Power: back to now.
 *   SOUND     sound level meter (dBA / dBZ), max, min, Leq, and a spectrum. Power: reset.
 *   STARS     a starfield across the five screens; it speeds up with the music.
 *   VISUAL    visual effects from the mic as one picture across the five screens (ui/viz.h); the LEDs
 *             flash on the beat. Left / right: the effect. Power: the colours (accent, rainbow, fire).
 *   SETUP     the device's settings, five to a page, one per screen. Left / right: choose.
 *             Power: edit (the value blinks), left / right change it, power saves, MODE cancels.
 *
 * A double clap (settings "clap") can turn the LEDs off / on, give full brightness, step to the next
 * mode or go back to the dashboard; it also snoozes a ringing alarm and closes a notification.
 * Notifications (POST /api/notify), sounds the device has learned and the day's sound dose show as a card
 * over the middle screen, the LEDs pulsing; any button closes it (that press does nothing else).
 *
 * Everywhere: LIGHT = full brightness for 10 s; long LIGHT = LEDs off / on; long power = sleep
 * (screens and LEDs dark; any button wakes the device, and that press does nothing else).
 * Low power (power.c) comes by itself when nobody seems to be about: the screens dim and slow down;
 * a sound or any button wakes it (the press that wakes it does nothing else).
 */
#include <stdbool.h>
#include <stdint.h>

#include "gfx/hud.h"

typedef enum {
    MODE_DASH, MODE_TUNER, MODE_LONGTONE, MODE_TEMPO, MODE_CHORDS, MODE_EAR, MODE_METRO, MODE_CHRONO, MODE_TIMER, MODE_ALARM,
    MODE_WORLD, MODE_SOUND, MODE_STARS, MODE_VISUAL, MODE_SETUP, MODE_COUNT
} ui_mode_t;

void modes_init(void);
void modes_update(uint32_t ms);           /* once per frame: buttons, clocks, LEDs, mic */
ui_mode_t modes_current(void);
bool modes_sleeping(void);
int modes_backlight(int base_pct);        /* the backlight to use now, given the settings' level */

/* A short message over the middle screen (brightness changed, LEDs off, the timer ringing, ...) */
bool modes_toast_active(void);
uint32_t modes_toast_gen(void);           /* changes whenever the toast changes */
void modes_draw_toast(hud_fb_t *fb);

/* One screen (0..4, left to right) of the current mode; not for the dashboard. */
void modes_draw(hud_fb_t *fb, int screen);

/* Pairing a browser: the device asks on its screens ("PAIR THIS BROWSER? <code>"); power allows, any
 * other button refuses, 60 s to answer. One request at a time, at most one every 5 s. */
uint32_t modes_pair_start(uint16_t *code);   /* the request's id (0: busy or too soon) and its code */
int modes_pair_state(uint32_t id);           /* 0 waiting, 1 allowed, 2 refused / expired / unknown */
void modes_pair_done(uint32_t id);           /* the browser has the token: forget the request */

/* A notification card over the middle screen: `seconds` (0 = until a button), a buzzer pattern (-1 =
 * none; not in a quiet night), the LEDs pulsing in `color` (0xRRGGBB). Any task may call it. */
void modes_notify(const char *title, const char *text, int seconds, int sound, uint32_t color);

const char *modes_name(void);             /* the current mode, e.g. "TUNER" ("DASH" for the dashboard) */
float modes_tempo_bpm(void);              /* the tempo finder's reading, NAN if none */
