#pragma once
/* Device settings, kept in NVS (namespace "tg") and editable from the web page. */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define TG_SOURCES  2
#define TG_LAYOUT_N 5

typedef struct {
    char src_url[TG_SOURCES][128];   /* stats agents: [0] = PC, [1] = home lab */
    uint16_t poll_ms;                /* how often the agents are polled, 500..10000 */
    char tz[48];                     /* POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3" */
    float lat, lon;                  /* for the weather */
    bool hour24;
    uint8_t brightness;              /* 0..100 % */
    char night_start[6];             /* "HH:MM", empty = no night dimming */
    char night_end[6];
    uint8_t night_brightness;        /* 0..100 % */
    char layout[TG_LAYOUT_N][12];    /* panel per screen, left to right: lab, cpu, nav, gpu, audio (or off) */
    /* v2: buzzer alerts (appended, so a v1 blob is a prefix of this struct) */
    bool buzzer;                     /* alerts on */
    uint8_t buzzer_volume;           /* 0..100 % */
    bool quiet_night;                /* no alerts inside the night window */
    uint8_t alert_cpu_temp;          /* °C, PC CPU and home lab CPU */
    uint8_t alert_gpu_temp;          /* °C, PC GPU */
    uint8_t alert_vram_temp;         /* °C, PC VRAM */
    /* v3: the LED chain */
    char led_mode_v10[8];            /* until v10 the one LED mode (off, glow, status, music); v11 reads it
                                        once into led_zone, then leaves it */
    uint8_t led_brightness;          /* 0..100 %, scaled by night_brightness inside the night window */
    /* v4: the buttons */
    bool key_beep;                   /* a short click on every button press */
    /* v5: the clock's sounds */
    bool chime;                      /* a short chime on the hour (not inside a quiet night) */
    bool alarm_on;
    char alarm_time[6];              /* "HH:MM" */
    uint8_t alarm_days;              /* bit 0 = Monday .. bit 6 = Sunday */
    /* v6: the world clock's two other places */
    char world_name[2][16];          /* shown as the screen's title, e.g. "STUTTGART" */
    char world_tz[2][48];            /* POSIX TZ, e.g. "CET-1CEST,M3.5.0,M10.5.0/3" */
    /* v7: what the modes were left at */
    uint16_t tuner_a4;               /* Hz, 430..450 */
    uint16_t metro_bpm;              /* 30..240 */
    uint16_t timer_min;              /* 1..180 */
    /* v8: low power when nobody seems to be at the desk (power.c) */
    uint8_t idle_min;                /* minutes without a sign of anyone before low power, 0 = never */
    uint8_t idle_brightness;         /* 0..100 %, the screens' level in low power (never above the normal one) */
    /* v9 */
    bool mic_stream;                 /* PCs may stream the mic (stream.c); the device's own uses don't change */
    /* v10 */
    char audio_view_v12[4];          /* until v12 the AUDIO screen's view ("eq" or "vu"); v13 reads it once into
                                        audio_view, then leaves it */
    /* v11 */
    char led_zone[3][8];             /* each LED zone's effect (LED_ZONE_*), one of settings.c's LED_EFFECTS */
    char world_order[4];             /* the World mode's clocks on screens 2, 3, 4: a digit each, 0 = home,
                                        1 and 2 = world_name[0] and [1]; "102" puts home in the middle */
    /* v12 */
    uint32_t led_color[3];           /* each LED zone's colour, 0xRRGGBB: what glow, breathe, wave, comet, sparkle
                                        and pulse show, and the "all is well" colour of the meters */
    /* v13 */
    uint32_t accent;                 /* the screens' (and the web page's) accent, 0xRRGGBB; teal by default */
    char audio_view[12];             /* the AUDIO screen: one of settings.c's AUDIO_VIEWS (eq, vu, or a visual effect) */
    uint8_t viz_palette;             /* the visual effects' colours: VIZ_PAL_* */
    uint8_t viz_effect;              /* the VISUAL mode's effect, as it was left */
    /* v14 */
    char clap[8];                    /* what a double clap does: one of settings.c's CLAP_ACTIONS */
    bool noise_alert;                /* warn when the day's sound dose reaches 100 % (core/listen.c) */
} tg_settings_t;

enum { VIZ_PAL_ACCENT, VIZ_PAL_RAINBOW, VIZ_PAL_FIRE, VIZ_PAL_COUNT };

/* The accent presets (the setup menu steps through them; the web page offers them and any colour) */
typedef struct { const char *name; uint32_t rgb; } tg_accent_t;
extern const tg_accent_t TG_ACCENTS[];
extern const int TG_ACCENT_N;
const char *settings_accent_name(uint32_t rgb);                   /* "TEAL", ..., or "CUSTOM" */

enum { LED_ZONE_LEFT, LED_ZONE_BACK, LED_ZONE_RIGHT };

/* The old single LED modes, now presets over the three zones: "off" (all off), "glow" (all glow),
 * "status" (cpu, lab, gpu), "music" (all music). */
bool settings_led_preset(tg_settings_t *s, const char *preset);   /* false: no such preset */
const char *settings_led_preset_of(const tg_settings_t *s);       /* the preset the zones match, or "custom" */
bool settings_leds_off(const tg_settings_t *s);                   /* every zone off */

esp_err_t settings_init(void);                 /* load from NVS (or defaults) */
void settings_get(tg_settings_t *out);         /* copy of the current settings */
esp_err_t settings_set(const tg_settings_t *s);/* validate, store, notify listeners */

/* JSON in/out for the web API. */
char *settings_to_json(void);                  /* caller frees */
esp_err_t settings_from_json(const char *json, char *err, int err_len);   /* merge + save */

/* Inside the night window right now (needs a valid clock; false without one). */
bool settings_is_night(const tg_settings_t *s, bool time_valid);

/* Bumped on every change, so tasks can notice new settings cheaply. */
uint32_t settings_generation(void);
