#include "ui/ledfx.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_timer.h"

#include "core/power.h"
#include "core/settings.h"
#include "drivers/leds.h"
#include "drivers/mic.h"
#include "net/netdata.h"

#define TEAL    0x2A8CB2u          /* brand teal 600, the HUD colour */
#define TEAL_LO 0x0A2833u          /* the unlit part of a meter */
#define ORANGE  0xFB6A10u          /* the screens' orange, pushed a little for the LEDs */
#define RED     0xFF1010u
#define AMBER   0xFBB000u
#define GREEN   0x20D080u

static volatile int64_t s_hold_until;
static volatile ledfx_scene_t s_scene;
static volatile float s_sa, s_sb;
static volatile bool s_sleep;
static volatile uint32_t s_beat_at;
static volatile bool s_beat_accent;

void ledfx_beat(uint32_t at_ms, bool accent)
{
    s_beat_accent = accent;
    s_beat_at = at_ms;
}

void ledfx_scene(ledfx_scene_t scene, float a, float b)
{
    s_sa = a;
    s_sb = b;
    s_scene = scene;
}

void ledfx_sleep(bool sleep)
{
    s_sleep = sleep;
}
static uint32_t s_px[LED_COUNT];
static int s_alert[2];             /* load alert level (0, 1 orange, 2 red) for CPU and GPU, with hysteresis */
static float s_level, s_peak, s_db = -90.0f, s_band[LED_STRIP];

void ledfx_hold(uint32_t ms)
{
    s_hold_until = esp_timer_get_time() + (int64_t)ms * 1000;
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static uint32_t scale(uint32_t c, float k)
{
    k = clamp01(k);
    return (uint32_t)((c >> 16 & 0xFF) * k) << 16 | (uint32_t)((c >> 8 & 0xFF) * k) << 8 | (uint32_t)((c & 0xFF) * k);
}

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    t = clamp01(t);
    int r = (int)((a >> 16 & 0xFF) + ((int)(b >> 16 & 0xFF) - (int)(a >> 16 & 0xFF)) * t);
    int g = (int)((a >> 8 & 0xFF) + ((int)(b >> 8 & 0xFF) - (int)(a >> 8 & 0xFF)) * t);
    int bl = (int)((a & 0xFF) + ((int)(b & 0xFF) - (int)(a & 0xFF)) * t);
    return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)bl;
}

/* i-th LED of a side strip counted from the bottom */
static void side_set(int right, int i, uint32_t c)
{
    if (i < 0 || i >= LED_STRIP) return;
    s_px[right ? LED_RIGHT0 + LED_STRIP - 1 - i : LED_LEFT0 + i] = c;
}

static void side_fill(int right, uint32_t c)
{
    for (int i = 0; i < LED_STRIP; i++) side_set(right, i, c);
}

/* Same thresholds and hysteresis as the screens: 60 % orange, 80 % red. */
static int alert_level(int which, float load)
{
    int lv = s_alert[which];
    if (isnan(load)) lv = 0;
    else if (load >= 80) lv = 2;
    else if (load >= 60) lv = (lv == 2 && load >= 75) ? 2 : 1;
    else if (load < 55) lv = 0;
    else lv = lv ? 1 : 0;
    return s_alert[which] = lv;
}

/* The screens' blink: orange 1.2 s period, red 0.6 s. */
static bool blink_on(int lv, uint32_t ms)
{
    int ph = (int)((ms / 150) % 8);
    return lv == 2 ? (ph % 4) < 2 : ph < 4;
}

static uint32_t meter_color(float pos)            /* teal, amber from 60 %, red from 80 % */
{
    return pos >= 0.8f ? RED : pos >= 0.6f ? ORANGE : TEAL;
}

static void load_meter(int right, float load, int lv, uint32_t ms)
{
    if (isnan(load)) { side_fill(right, 0); return; }
    if (lv && !blink_on(lv, ms)) { side_fill(right, scale(lv == 2 ? RED : ORANGE, 0.15f)); return; }
    int lit = (int)lroundf(clamp01(load / 100.0f) * LED_STRIP);
    if (lit < 1) lit = 1;
    for (int i = 0; i < LED_STRIP; i++) {
        side_set(right, i, i < lit ? (lv ? (lv == 2 ? RED : ORANGE) : meter_color((float)i / (LED_STRIP - 1))) : TEAL_LO);
    }
}

static void alert_overlay(const host_stats_t *pc, uint32_t ms)
{
    if (!pc->ok) return;
    int lv = alert_level(0, pc->cpu_load);
    if (lv) side_fill(0, blink_on(lv, ms) ? (lv == 2 ? RED : ORANGE) : 0);
    lv = alert_level(1, pc->has_gpu ? pc->gpu.load : NAN);
    if (lv) side_fill(1, blink_on(lv, ms) ? (lv == 2 ? RED : ORANGE) : 0);
}

static void back_health(const host_stats_t *lab, uint32_t ms)
{
    bool svc_down = false;
    for (int i = 0; i < lab->n_services; i++) svc_down |= !lab->services[i].up;
    uint32_t c;
    float k = 1.0f;
    if (!lab->ever) c = TEAL_LO;
    else if (!lab->ok || svc_down) { c = RED; k = 0.35f + 0.65f * (0.5f + 0.5f * sinf(ms * 0.0042f)); }   /* ~1.5 s pulse */
    else if (lab->has_battery && !lab->ac) c = AMBER;
    else { c = TEAL; k = 0.75f + 0.25f * sinf(ms * 0.0013f); }                                           /* slow breath */
    for (int i = 0; i < LED_STRIP; i++) s_px[LED_BACK0 + i] = scale(c, k);
}

static void music(const mic_snapshot_t *m)
{
    /* loudness, low-passed like the audio panel, with a falling peak dot */
    if (!isnan(m->dbfs)) s_db += (m->dbfs - s_db) * 0.3f;
    float lf = clamp01((s_db + 75.0f) / 60.0f);
    s_level = lf > s_level ? s_level + (lf - s_level) * 0.6f : fmaxf(lf, s_level - 0.03f);
    s_peak = s_level > s_peak ? s_level : fmaxf(0, s_peak - 0.01f);
    int lit = (int)lroundf(s_level * LED_STRIP), pk = (int)lroundf(s_peak * LED_STRIP) - 1;
    for (int side = 0; side < 2; side++) {
        for (int i = 0; i < LED_STRIP; i++) {
            uint32_t c = i < lit ? meter_color((float)i / (LED_STRIP - 1)) : 0;
            if (i == pk && pk >= lit) c = 0x808080;
            side_set(side, i, c);
        }
    }
    /* back: 16 bands folded onto 15 LEDs, brightness = band level, colour teal -> violet across */
    for (int i = 0; i < LED_STRIP; i++) {
        float pos = i * (MIC_BANDS - 1) / (float)(LED_STRIP - 1);
        int b0 = (int)pos;
        int b1 = b0 + 1 < MIC_BANDS ? b0 + 1 : b0;
        float db = m->bands[b0] + (m->bands[b1] - m->bands[b0]) * (pos - b0);
        float f = clamp01((db + 90.0f) / 65.0f);
        s_band[i] = f > s_band[i] ? f : fmaxf(0, s_band[i] - 0.05f);
        s_px[LED_BACK0 + i] = scale(mix(TEAL, 0x8040FF, (float)i / (LED_STRIP - 1)), s_band[i] * s_band[i]);
    }
}

static void scene_tuner(float cents)
{
    if (isnan(cents)) {                                  /* listening: a dim teal line all round */
        for (int i = 0; i < LED_COUNT; i++) s_px[i] = scale(TEAL, 0.12f);
        return;
    }
    if (fabsf(cents) <= 5.0f) {
        for (int i = 0; i < LED_COUNT; i++) s_px[i] = GREEN;
        return;
    }
    int lit = (int)lroundf(clamp01(fabsf(cents) / 50.0f) * LED_STRIP);
    if (lit < 1) lit = 1;
    int side = cents > 0;                                /* flat: left strip, sharp: right */
    for (int i = 0; i < LED_STRIP; i++) side_set(side, i, i < lit ? (fabsf(cents) > 25 ? ORANGE : TEAL) : TEAL_LO);
    for (int i = 0; i < LED_STRIP; i++) side_set(!side, i, TEAL_LO);
    for (int i = 0; i < LED_STRIP; i++) s_px[LED_BACK0 + i] = scale(TEAL, 0.12f);
}

static void scene_progress(float left, bool running, uint32_t ms)
{
    int lit = (int)ceilf(clamp01(left) * LED_STRIP);
    float k = running ? 1.0f : 0.45f + 0.35f * (0.5f + 0.5f * sinf(ms * 0.004f));   /* paused: breathing */
    for (int side = 0; side < 2; side++) {
        for (int i = 0; i < LED_STRIP; i++) side_set(side, i, i < lit ? scale(TEAL, k) : TEAL_LO);
    }
    for (int i = 0; i < LED_STRIP; i++) s_px[LED_BACK0 + i] = scale(TEAL, 0.15f);
}

static void scene_beat(uint32_t at_ms, bool accent, uint32_t ms)
{
    int32_t age = (int32_t)(ms - at_ms);
    float k = age < 0 ? 0 : expf(-age / 90.0f);          /* ~0.1 s fade */
    uint32_t c = accent ? ORANGE : 0xC8F0FFu;
    for (int i = 0; i < LED_COUNT; i++) s_px[i] = mix(scale(TEAL, 0.08f), c, k);
}

static void ledfx_task(void *arg)
{
    host_stats_t pc, lab;
    mic_snapshot_t m;
    tg_settings_t s;
    bool was_off = false;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(power_idle() ? 250 : 33));
        int64_t now = esp_timer_get_time();
        if (now < s_hold_until) continue;
        uint32_t ms = (uint32_t)(now / 1000);
        settings_get(&s);
        int bright = s.led_brightness;
        if (settings_is_night(&s, netdata_time_valid())) bright = bright * s.night_brightness / 100;
        bool off = strcmp(s.led_mode, "off") == 0 || bright == 0 || s_sleep || power_idle();   /* low power: dark */
        leds_keypad(off ? 0 : (uint8_t)bright);         /* buttons board: steady, same brightness */
        if (off) {
            if (!was_off) leds_off();
            was_off = true;
            continue;
        }
        was_off = false;
        netdata_get_host(0, &pc);
        netdata_get_host(1, &lab);
        memset(s_px, 0, sizeof s_px);
        ledfx_scene_t scene = s_scene;
        if (scene == SCENE_TUNER) {
            scene_tuner(s_sa);
        } else if (scene == SCENE_PROGRESS) {
            scene_progress(s_sa, s_sb > 0.5f, ms);
        } else if (scene == SCENE_BEAT) {
            scene_beat(s_beat_at, s_beat_accent, ms);
        } else if (scene == SCENE_ALARM) {
            uint32_t c = (ms / 250) % 2 ? ORANGE : 0;
            for (int i = 0; i < LED_COUNT; i++) s_px[i] = c;
        } else if (strcmp(s.led_mode, "status") == 0) {
            load_meter(0, pc.ok ? pc.cpu_load : NAN, alert_level(0, pc.ok ? pc.cpu_load : NAN), ms);
            float g = pc.ok && pc.has_gpu ? pc.gpu.load : NAN;
            load_meter(1, g, alert_level(1, g), ms);
            back_health(&lab, ms);
        } else if (strcmp(s.led_mode, "music") == 0) {
            mic_get(&m);
            music(&m);
            alert_overlay(&pc, ms);
        } else {                                                      /* glow */
            for (int i = 0; i < LED_COUNT; i++) s_px[i] = TEAL;
            alert_overlay(&pc, ms);
        }
        leds_show(s_px, LED_COUNT, (uint8_t)(bright * 255 / 100));
    }
}

void ledfx_start(void)
{
    xTaskCreatePinnedToCore(ledfx_task, "ledfx", 4096, NULL, 4, NULL, 0);
}
