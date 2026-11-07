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
    char led_mode[8];                /* off, glow, status, music */
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
} tg_settings_t;

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
