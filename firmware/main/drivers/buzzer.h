#pragma once
/* The piezo buzzer on GPIO9: short sound patterns (notes and pitch glides) played by a background task.
 * The piezo is loudest around 2.5..4 kHz, so the sounds live up there. */
#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    BZ_TEST,        /* the settings page's test button */
    BZ_WARN,        /* something got hot */
    BZ_DOWN,        /* a source or service went away */
    BZ_UP,          /* it came back */
    BZ_POWER,       /* the home lab lost mains power */
    BZ_KEY,         /* a button press */
    BZ_TICK,        /* metronome beat */
    BZ_ACCENT,      /* metronome first beat of the bar */
    BZ_ALARM,       /* the timer ran out (one burst; repeat it to keep ringing) */
    BZ_MODE,        /* entering another mode */
    BZ_BATTERY,     /* the home lab's battery is low */
    BZ_BOOT,        /* the device has started */
    BZ_CHIME,       /* on the hour */
} buzzer_pattern_t;

esp_err_t buzzer_start(void);
/* Queue a pattern. volume 0..100 (0 = silent); patterns don't overlap, extras are dropped. */
void buzzer_play(buzzer_pattern_t p, int volume);
/* One plain tone (tests, e.g. the tuner), 50..8000 Hz, up to 5 s. */
void buzzer_tone(int hz, int ms, int volume);
/* Sounding now, or stopped less than 150 ms ago (what the mic hears of it, echoes included). */
bool buzzer_busy(void);
