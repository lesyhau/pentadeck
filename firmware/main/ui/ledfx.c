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
static float s_level, s_peak, s_db = -90.0f;

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

/* Where each side LED sits, in chain order (ledfx.h): its row, 0 = bottom .. 9 = top. Both side
 * boards are alike; rows from a straight-on photo of the board, 2026-11-08:
 *   LED2..11   the short column, on the back edge, bottom to top
 *   LED12..22  the long column, on the front edge, top to bottom
 *   LED23      the bottom, in the middle */
static const uint8_t ROW_OF[LED_STRIP] = {
    0, 2, 4, 5, 6, 7, 7, 8,                 /* LED2, 4, 6..11 */
    9, 8, 7, 6, 5, 4, 4, 3, 2, 1, 0,        /* LED12..22 */
    1,                                      /* LED23 */
};

/* Row `row` (0 = bottom .. LED_ROWS - 1) of a side strip: every LED in it (one, two or three, on either
 * edge). The callers set all rows each frame, so every LED gets a colour. */
static void side_set(int right, int row, uint32_t c)
{
    for (int k = 0; k < LED_STRIP; k++) {
        if (ROW_OF[k] == row) s_px[(right ? LED_RIGHT0 : LED_LEFT0) + k] = c;
    }
}

/* A zone (settings.h LED_ZONE_*: the left strip, the back light, the right strip), drawn in steps:
 * the side strips by row from the bottom up, the back light from the middle LED out to both ends. */
static int zone_steps(int z)
{
    return z == LED_ZONE_BACK ? LED_BACK_N / 2 + 1 : LED_ROWS;
}

static void zone_set(int z, int step, uint32_t c)
{
    if (z == LED_ZONE_BACK) {
        const int mid = LED_BACK_N / 2;
        s_px[LED_BACK0 + mid - step] = c;
        s_px[LED_BACK0 + mid + step] = c;
    } else {
        side_set(z == LED_ZONE_RIGHT, step, c);
    }
}

static void zone_fill(int z, uint32_t c)
{
    for (int i = 0; i < zone_steps(z); i++) zone_set(z, i, c);
}

/* Same thresholds and hysteresis as the screens: 60 % orange, 80 % red. Calling it twice a frame
 * with the same load (two zones on the CPU) gives the same answer. */
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

/* How the screens colour a temperature (hud.c temp_color): orange from 75 °C, red from 88 °C. */
static int heat_level(float c)
{
    return isnan(c) ? 0 : c >= 88.0f ? 2 : c >= 75.0f ? 1 : 0;
}

/* The screens' blink: orange 1.2 s period, red 0.6 s. */
static bool blink_on(int lv, uint32_t ms)
{
    int ph = (int)((ms / 150) % 8);
    return lv == 2 ? (ph % 4) < 2 : ph < 4;
}

static uint32_t meter_color(float pos)            /* teal, orange from 60 %, red from 80 % */
{
    return pos >= 0.8f ? RED : pos >= 0.6f ? ORANGE : TEAL;
}

/* A PC load meter, lit up to the load. Teal while all is well; orange from 60 % load or 75 °C, red
 * from 80 % or 88 °C, the screens' thresholds, so a hot part shows even at a low load (the unlit part
 * takes a glow of the colour too). A load alert blinks like the screen's frame; heat alone is steady. */
static void fx_load(int z, float load, float temp, int alert, uint32_t ms)
{
    if (isnan(load)) { zone_fill(z, 0); return; }
    int heat = heat_level(temp), worst = alert > heat ? alert : heat, n = zone_steps(z);
    uint32_t c = worst == 2 ? RED : worst == 1 ? ORANGE : TEAL;
    uint32_t unlit = worst ? scale(c, 0.12f) : TEAL_LO;
    if (alert && !blink_on(alert, ms)) c = scale(c, 0.2f);
    int lit = (int)lroundf(clamp01(load / 100.0f) * n);
    if (lit < 1) lit = 1;
    for (int i = 0; i < n; i++) zone_set(z, i, i < lit ? c : unlit);
}

/* The home lab's health: teal breathing when well, amber on battery, a red pulse when it or a service is down. */
static void fx_lab(int z, const host_stats_t *lab, uint32_t ms)
{
    bool svc_down = false;
    for (int i = 0; i < lab->n_services; i++) svc_down |= !lab->services[i].up;
    uint32_t c;
    float k = 1.0f;
    if (!lab->ever) c = TEAL_LO;
    else if (!lab->ok || svc_down) { c = RED; k = 0.35f + 0.65f * (0.5f + 0.5f * sinf(ms * 0.0042f)); }   /* ~1.5 s pulse */
    else if (lab->has_battery && !lab->ac) c = AMBER;
    else { c = TEAL; k = 0.75f + 0.25f * sinf(ms * 0.0013f); }                                           /* slow breath */
    zone_fill(z, scale(c, k));
}

static void fx_breathe(int z, uint32_t ms)        /* teal, 15 % .. full and back every 4 s */
{
    float k = 0.15f + 0.85f * (0.5f - 0.5f * cosf(ms * (6.2831853f / 4000.0f)));
    zone_fill(z, scale(TEAL, k * k));             /* squared: the eye's curve, so it doesn't sit at full */
}

static uint32_t hue(float h)                      /* h in turns (0..1), full saturation and value */
{
    h -= floorf(h);
    float r = clamp01(fabsf(h * 6 - 3) - 1), g = clamp01(2 - fabsf(h * 6 - 2)), b = clamp01(2 - fabsf(h * 6 - 4));
    return (uint32_t)(r * 255) << 16 | (uint32_t)(g * 255) << 8 | (uint32_t)(b * 255);
}

static void fx_rainbow(int z, uint32_t ms)        /* the colour wheel rolling up the strip, once every 6 s */
{
    int n = zone_steps(z);
    for (int i = 0; i < n; i++) zone_set(z, i, hue(ms / 6000.0f - (float)i / (n * 2)));
}

/* One step of a loudness meter: `steps` LEDs, lit up to `level` (0..1), the peak dot above. Each LED
 * takes the colour of the level it lights at, so every zone turns orange and red at the same loudness. */
static uint32_t meter_led(int k, int steps, float level, float peak)
{
    int lit = (int)lroundf(level * steps), pk = (int)lroundf(peak * steps) - 1;
    if (k < lit) return meter_color((k + 0.5f) / steps);
    return k == pk ? 0x808080u : 0;
}

/* Loudness, low-passed like the audio panel, with a falling peak dot; once a frame, for every music zone. */
static void music_update(const mic_snapshot_t *m)
{
    if (!isnan(m->dbfs)) s_db += (m->dbfs - s_db) * 0.3f;
    float lf = clamp01((s_db + 75.0f) / 60.0f);
    s_level = lf > s_level ? s_level + (lf - s_level) * 0.6f : fmaxf(lf, s_level - 0.03f);
    s_peak = s_level > s_peak ? s_level : fmaxf(0, s_peak - 0.01f);
}

static void fx_music(int z)
{
    int n = zone_steps(z);
    for (int i = 0; i < n; i++) zone_set(z, i, meter_led(i, n, s_level, s_peak));
}

static void zone_effect(int z, const char *fx, const host_stats_t *pc, const host_stats_t *lab, uint32_t ms)
{
    if (strcmp(fx, "glow") == 0) {
        zone_fill(z, TEAL);
    } else if (strcmp(fx, "breathe") == 0) {
        fx_breathe(z, ms);
    } else if (strcmp(fx, "rainbow") == 0) {
        fx_rainbow(z, ms);
    } else if (strcmp(fx, "cpu") == 0) {
        float load = pc->ok ? pc->cpu_load : NAN;
        fx_load(z, load, pc->ok ? pc->cpu_temp : NAN, alert_level(0, load), ms);
    } else if (strcmp(fx, "gpu") == 0) {
        bool ok = pc->ok && pc->has_gpu;
        float load = ok ? pc->gpu.load : NAN;
        fx_load(z, load, ok ? pc->gpu.temp : NAN, alert_level(1, load), ms);
    } else if (strcmp(fx, "lab") == 0) {
        fx_lab(z, lab, ms);
    } else if (strcmp(fx, "music") == 0) {
        fx_music(z);
    }                                             /* "off": left dark */
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
    int lit = (int)lroundf(clamp01(fabsf(cents) / 50.0f) * LED_ROWS);
    if (lit < 1) lit = 1;
    int side = cents > 0;                                /* flat: left strip, sharp: right */
    for (int i = 0; i < LED_ROWS; i++) side_set(side, i, i < lit ? (fabsf(cents) > 25 ? ORANGE : TEAL) : TEAL_LO);
    for (int i = 0; i < LED_ROWS; i++) side_set(!side, i, TEAL_LO);
    for (int i = 0; i < LED_BACK_N; i++) s_px[LED_BACK0 + i] = scale(TEAL, 0.12f);
}

static void scene_progress(float left, bool running, uint32_t ms)
{
    int lit = (int)ceilf(clamp01(left) * LED_ROWS);
    float k = running ? 1.0f : 0.45f + 0.35f * (0.5f + 0.5f * sinf(ms * 0.004f));   /* paused: breathing */
    for (int side = 0; side < 2; side++) {
        for (int i = 0; i < LED_ROWS; i++) side_set(side, i, i < lit ? scale(TEAL, k) : TEAL_LO);
    }
    for (int i = 0; i < LED_BACK_N; i++) s_px[LED_BACK0 + i] = scale(TEAL, 0.15f);
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
        bool off = settings_leds_off(&s) || bright == 0 || s_sleep || power_idle();   /* low power: dark */
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
        } else {                                          /* each zone its own effect */
            bool music = false;
            for (int z = 0; z < 3; z++) music |= strcmp(s.led_zone[z], "music") == 0;
            if (music) {
                mic_get(&m);
                music_update(&m);
            }
            for (int z = 0; z < 3; z++) zone_effect(z, s.led_zone[z], &pc, &lab, ms);
        }
        leds_show(s_px, LED_COUNT, (uint8_t)(bright * 255 / 100));
    }
}

void ledfx_start(void)
{
    xTaskCreatePinnedToCore(ledfx_task, "ledfx", 4096, NULL, 4, NULL, 0);
}
