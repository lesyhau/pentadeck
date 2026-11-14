#include "core/settings.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "core/tzlite.h"

static const char *TAG = "settings";
#define NS      "tg"
#define KEY     "cfg"
#define VERSION 14                  /* 2 the buzzer, 3 the LEDs, 4 the key beep, 5 the alarm and chime, 6 the world clock,
                                       7 the modes' values, 8 low power, 9 the mic stream switch, 10 the AUDIO view,
                                       11 an effect per LED zone and the world clock's order, 12 a colour per LED zone,
                                       13 the accent, the visual effects, 14 the clap and the sound dose */

typedef struct {
    uint32_t version;
    tg_settings_t s;
} stored_t;

static tg_settings_t s_cur;
static SemaphoreHandle_t s_lock;
static volatile uint32_t s_gen;

static const char *PANELS[] = { "lab", "cpu", "nav", "gpu", "audio", "off" };
static const char *LED_MODES[] = { "off", "glow", "status", "music" };          /* presets over the zones */
static const char *LED_EFFECTS[] = { "off", "glow", "breathe", "wave", "comet", "sparkle", "pulse", "fire", "rainbow",
                                     "cpu", "gpu", "lab", "music", "spectrum" };
/* a double clap: nothing, the LEDs off / on, full brightness for 10 s, the next mode, the dashboard */
static const char *CLAP_ACTIONS[] = { "off", "leds", "light", "mode", "dash" };
static const char *LED_ZONE_KEYS[3] = { "led_left", "led_back", "led_right" };
static const char *LED_COLOR_KEYS[3] = { "led_left_color", "led_back_color", "led_right_color" };
#define LED_TEAL 0x2A8CB2u                                                     /* the HUD's teal */
/* each preset's effect for the left strip, the back light and the right strip */
static const char *LED_PRESET_ZONES[][3] = {
    { "off", "off", "off" }, { "glow", "glow", "glow" }, { "cpu", "lab", "gpu" }, { "music", "music", "music" },
};

#define COUNT(a) (sizeof (a) / sizeof (a)[0])

static bool in_list(const char *v, const char *const *list, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (strcmp(v, list[i]) == 0) return true;
    }
    return false;
}

bool settings_led_preset(tg_settings_t *s, const char *preset)
{
    for (size_t i = 0; i < COUNT(LED_MODES); i++) {
        if (strcmp(preset, LED_MODES[i]) != 0) continue;
        for (int z = 0; z < 3; z++) strlcpy(s->led_zone[z], LED_PRESET_ZONES[i][z], sizeof s->led_zone[z]);
        return true;
    }
    return false;
}

const char *settings_led_preset_of(const tg_settings_t *s)
{
    for (size_t i = 0; i < COUNT(LED_MODES); i++) {
        bool same = true;
        for (int z = 0; z < 3; z++) same &= strcmp(s->led_zone[z], LED_PRESET_ZONES[i][z]) == 0;
        if (same) return LED_MODES[i];
    }
    return "custom";
}

bool settings_leds_off(const tg_settings_t *s)
{
    return strcmp(settings_led_preset_of(s), "off") == 0;
}

static bool valid_world_order(const char *o)
{
    return strlen(o) == 3 && strchr(o, '0') && strchr(o, '1') && strchr(o, '2');
}
static const char *AUDIO_VIEWS[] = { "eq", "vu", "mirror", "scope", "waterfall", "ripple", "radial" };
static const char *VIZ_PALETTES[VIZ_PAL_COUNT] = { "accent", "rainbow", "fire" };

/* the teal ramp's saturation and lightness (its 600), in other hues */
const tg_accent_t TG_ACCENTS[] = {
    { "TEAL", 0x2A8CB2u }, { "BLUE", 0x2A57B2u }, { "VIOLET", 0x622AB2u }, { "PINK", 0xB22A84u }, { "RED", 0xB22A2Au },
    { "ORANGE", 0xB2622Au }, { "AMBER", 0xB2892Au }, { "GREEN", 0x2AB262u }, { "MONO", 0x8C8C8Cu },
};
const int TG_ACCENT_N = COUNT(TG_ACCENTS);

const char *settings_accent_name(uint32_t rgb)
{
    for (int i = 0; i < TG_ACCENT_N; i++) if (TG_ACCENTS[i].rgb == rgb) return TG_ACCENTS[i].name;
    return "CUSTOM";
}

static uint32_t parse_hex(const cJSON *v, bool *given)      /* "#RRGGBB" (or "RRGGBB"); bad: fails validation */
{
    *given = cJSON_IsString(v) && v->valuestring;
    if (!*given) return 0;
    const char *h = v->valuestring + (v->valuestring[0] == '#');
    char *end;
    unsigned long c = strtoul(h, &end, 16);
    return strlen(h) == 6 && *end == 0 ? (uint32_t)c : 0xFFFFFFFFu;
}

static void defaults(tg_settings_t *s)
{
    memset(s, 0, sizeof *s);
    /* no stats agents yet (set them on the settings page), UTC, and no location: the weather
     * waits until one is set */
    s->poll_ms = 1000;
    strcpy(s->tz, "UTC0");
    s->lat = 0;
    s->lon = 0;
    s->hour24 = true;
    s->brightness = 100;
    s->night_brightness = 40;
    const char *layout[TG_LAYOUT_N] = { "lab", "cpu", "nav", "gpu", "audio" };
    for (int i = 0; i < TG_LAYOUT_N; i++) {
        strcpy(s->layout[i], layout[i]);
    }
    s->buzzer = true;
    s->buzzer_volume = 40;
    s->quiet_night = true;
    s->alert_cpu_temp = 90;
    s->alert_gpu_temp = 85;
    s->alert_vram_temp = 95;
    strcpy(s->led_mode_v10, "status");
    settings_led_preset(s, "status");
    s->led_brightness = 40;
    s->key_beep = true;
    s->chime = false;
    s->alarm_on = false;
    strcpy(s->alarm_time, "07:00");
    s->alarm_days = 0x1F;              /* Monday to Friday */
    strcpy(s->world_name[0], "LONDON");
    strcpy(s->world_tz[0], "GMT0BST,M3.5.0/1,M10.5.0");
    strcpy(s->world_name[1], "NEW YORK");
    strcpy(s->world_tz[1], "EST5EDT,M3.2.0,M11.1.0");
    s->tuner_a4 = 440;
    s->metro_bpm = 100;
    s->timer_min = 5;
    s->idle_min = 10;
    s->idle_brightness = 10;
    s->mic_stream = true;
    strcpy(s->audio_view_v12, "eq");
    strcpy(s->world_order, "102");     /* home in the middle */
    for (int z = 0; z < 3; z++) s->led_color[z] = LED_TEAL;
    s->accent = LED_TEAL;
    strcpy(s->audio_view, "eq");
    s->viz_palette = VIZ_PAL_ACCENT;
    s->viz_effect = 0;
    strcpy(s->clap, "off");
    s->noise_alert = true;
}

static bool valid_panel(const char *p)
{
    for (size_t i = 0; i < sizeof PANELS / sizeof PANELS[0]; i++) {
        if (strcmp(p, PANELS[i]) == 0) return true;
    }
    return false;
}

static bool valid_hhmm(const char *t)
{
    if (t[0] == 0) return true;
    int h, m;
    return strlen(t) == 5 && sscanf(t, "%2d:%2d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60;
}

static esp_err_t validate(const tg_settings_t *s, char *err, int err_len)
{
#define FAIL(...) do { if (err) snprintf(err, err_len, __VA_ARGS__); return ESP_ERR_INVALID_ARG; } while (0)
    for (int i = 0; i < TG_SOURCES; i++) {
        if (s->src_url[i][0] && strncmp(s->src_url[i], "http://", 7) != 0) FAIL("source %d must start with http://", i + 1);
    }
    if (s->poll_ms < 500 || s->poll_ms > 10000) FAIL("poll_ms must be 500..10000");
    if (!s->tz[0]) FAIL("tz is empty");
    if (s->lat < -90 || s->lat > 90 || s->lon < -180 || s->lon > 180) FAIL("lat/lon out of range");
    if (s->brightness > 100 || s->night_brightness > 100) FAIL("brightness is 0..100");
    if (!valid_hhmm(s->night_start) || !valid_hhmm(s->night_end)) FAIL("night times are HH:MM");
    for (int i = 0; i < TG_LAYOUT_N; i++) {
        if (!valid_panel(s->layout[i])) FAIL("unknown panel '%s'", s->layout[i]);
    }
    if (s->buzzer_volume > 100) FAIL("buzzer volume is 0..100");
    for (int z = 0; z < 3; z++) {
        if (!in_list(s->led_zone[z], LED_EFFECTS, COUNT(LED_EFFECTS))) FAIL("%s: unknown LED effect '%s'", LED_ZONE_KEYS[z], s->led_zone[z]);
    }
    for (int z = 0; z < 3; z++) {
        if (s->led_color[z] > 0xFFFFFF) FAIL("%s must be #RRGGBB", LED_COLOR_KEYS[z]);
    }
    if (!valid_world_order(s->world_order)) FAIL("world_order must hold 0, 1 and 2 once each");
    if (s->led_brightness > 100) FAIL("LED brightness is 0..100");
    if (!in_list(s->audio_view, AUDIO_VIEWS, COUNT(AUDIO_VIEWS))) FAIL("unknown audio view '%s'", s->audio_view);
    if (s->accent > 0xFFFFFF) FAIL("accent must be #RRGGBB");
    if (s->viz_palette >= VIZ_PAL_COUNT) FAIL("viz_palette is accent, rainbow or fire");
    if (!in_list(s->clap, CLAP_ACTIONS, COUNT(CLAP_ACTIONS))) FAIL("clap is off, leds, light, mode or dash");
    if (!valid_hhmm(s->alarm_time) || !s->alarm_time[0]) FAIL("alarm time must be HH:MM");
    if (s->tuner_a4 < 430 || s->tuner_a4 > 450) FAIL("tuner A4 is 430..450 Hz");
    if (s->metro_bpm < 30 || s->metro_bpm > 240) FAIL("metronome is 30..240 BPM");
    if (s->timer_min < 1 || s->timer_min > 180) FAIL("timer is 1..180 min");
    if (s->idle_min > 120) FAIL("low power after 0..120 min (0 = never)");
    if (s->idle_brightness > 100) FAIL("low-power brightness is 0..100");
    for (int i = 0; i < 2; i++) {
        tz_t z;
        if (!s->world_name[i][0]) FAIL("world clock %d needs a name", i + 1);
        if (!tz_parse(s->world_tz[i], &z)) FAIL("world clock %d: '%s' is not a POSIX time zone", i + 1, s->world_tz[i]);
    }
    if (s->alert_cpu_temp < 50 || s->alert_gpu_temp < 50 || s->alert_vram_temp < 50 ||
        s->alert_cpu_temp > 120 || s->alert_gpu_temp > 120 || s->alert_vram_temp > 120) FAIL("alert temperatures are 50..120");
    return ESP_OK;
#undef FAIL
}

esp_err_t settings_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs a reformat (%s)", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    defaults(&s_cur);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS unavailable (%s): using defaults", esp_err_to_name(err));
        return err;
    }
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        /* static: two copies of the settings are too much for the main task's stack (start-up only) */
        static stored_t st;
        size_t len = sizeof st;
        if (nvs_get_blob(h, KEY, &st, &len) == ESP_OK) {
            static tg_settings_t s;
            defaults(&s);
            if (st.version == VERSION && len == sizeof st) {
                s = st.s;
            } else if (st.version == 13 && len >= sizeof(uint32_t) + offsetof(tg_settings_t, clap)) {
                memcpy(&s, &st.s, offsetof(tg_settings_t, clap));
            } else if (st.version == 12 && len >= sizeof(uint32_t) + offsetof(tg_settings_t, accent)) {
                memcpy(&s, &st.s, offsetof(tg_settings_t, accent));
            } else if (st.version == 11 && len >= sizeof(uint32_t) + offsetof(tg_settings_t, led_color)) {
                memcpy(&s, &st.s, offsetof(tg_settings_t, led_color));
            } else if (st.version == 10 && len >= sizeof(uint32_t) + offsetof(tg_settings_t, led_zone)) {
                memcpy(&s, &st.s, offsetof(tg_settings_t, led_zone));
            } else if (st.version == 9 && len >= sizeof(uint32_t) + offsetof(tg_settings_t, audio_view_v12)) {
                memcpy(&s, &st.s, offsetof(tg_settings_t, audio_view_v12));
            } else if (st.version == 8 && len >= sizeof(uint32_t) + offsetof(tg_settings_t, mic_stream)) {
                memcpy(&s, &st.s, offsetof(tg_settings_t, mic_stream));
            } else if (st.version == 7 && len >= sizeof(uint32_t) + offsetof(tg_settings_t, idle_min)) {
                memcpy(&s, &st.s, offsetof(tg_settings_t, idle_min));
            } else if (st.version == 6 && len >= sizeof(uint32_t) + offsetof(tg_settings_t, tuner_a4)) {
                memcpy(&s, &st.s, offsetof(tg_settings_t, tuner_a4));
            } else if (st.version == 5 && len >= sizeof(uint32_t) + offsetof(tg_settings_t, world_name)) {
                memcpy(&s, &st.s, offsetof(tg_settings_t, world_name));
            } else if (st.version == 4 && len >= sizeof(uint32_t) + offsetof(tg_settings_t, chime)) {
                memcpy(&s, &st.s, offsetof(tg_settings_t, chime));
            } else if (st.version == 3 && len >= sizeof(uint32_t) + offsetof(tg_settings_t, key_beep)) {
                memcpy(&s, &st.s, offsetof(tg_settings_t, key_beep));
            } else if (st.version == 2 && len >= sizeof(uint32_t) + offsetof(tg_settings_t, led_mode_v10)) {
                memcpy(&s, &st.s, offsetof(tg_settings_t, led_mode_v10));   /* v2 fields; the new ones keep defaults */
            } else if (st.version == 1 && len >= sizeof(uint32_t) + offsetof(tg_settings_t, buzzer)) {
                memcpy(&s, &st.s, offsetof(tg_settings_t, buzzer));
            }
            /* before v11 one LED mode ran all three zones (v1 and v2 keep the default, "status") */
            if (st.version < 11 && !settings_led_preset(&s, s.led_mode_v10)) settings_led_preset(&s, "status");
            /* before v13 the AUDIO view had room for "eq" or "vu" only */
            if (st.version < 13 && in_list(s.audio_view_v12, AUDIO_VIEWS, COUNT(AUDIO_VIEWS))) {
                strlcpy(s.audio_view, s.audio_view_v12, sizeof s.audio_view);
            }
            if (validate(&s, NULL, 0) == ESP_OK) {
                s_cur = s;
                ESP_LOGI(TAG, "loaded (stored version %lu)", (unsigned long)st.version);
            }
        }
        nvs_close(h);
    }
    s_gen = 1;
    return ESP_OK;
}

void settings_get(tg_settings_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_cur;
    xSemaphoreGive(s_lock);
}

bool settings_is_night(const tg_settings_t *s, bool time_valid)
{
    int h, m, a = -1, b = -1;
    if (sscanf(s->night_start, "%d:%d", &h, &m) == 2) a = h * 60 + m;
    if (sscanf(s->night_end, "%d:%d", &h, &m) == 2) b = h * 60 + m;
    if (a < 0 || b < 0 || a == b || !time_valid) return false;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    int t = tm.tm_hour * 60 + tm.tm_min;
    return a < b ? (t >= a && t < b) : (t >= a || t < b);
}

uint32_t settings_generation(void)
{
    return s_gen;
}

esp_err_t settings_set(const tg_settings_t *s)
{
    esp_err_t err = validate(s, NULL, 0);
    if (err != ESP_OK) return err;
    stored_t st = { .version = VERSION, .s = *s };
    nvs_handle_t h;
    err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, KEY, &st, sizeof st);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_cur = *s;     /* apply even if saving failed */
    xSemaphoreGive(s_lock);
    s_gen++;
    return err;
}

char *settings_to_json(void)
{
    tg_settings_t s;
    settings_get(&s);
    cJSON *o = cJSON_CreateObject();
    cJSON *src = cJSON_AddArrayToObject(o, "sources");
    for (int i = 0; i < TG_SOURCES; i++) cJSON_AddItemToArray(src, cJSON_CreateString(s.src_url[i]));
    cJSON_AddNumberToObject(o, "poll_ms", s.poll_ms);
    cJSON_AddStringToObject(o, "tz", s.tz);
    cJSON_AddNumberToObject(o, "lat", round(s.lat * 10000.0) / 10000.0);   /* float -> tidy decimal */
    cJSON_AddNumberToObject(o, "lon", round(s.lon * 10000.0) / 10000.0);
    cJSON_AddBoolToObject(o, "hour24", s.hour24);
    cJSON_AddNumberToObject(o, "brightness", s.brightness);
    cJSON_AddStringToObject(o, "night_start", s.night_start);
    cJSON_AddStringToObject(o, "night_end", s.night_end);
    cJSON_AddNumberToObject(o, "night_brightness", s.night_brightness);
    cJSON *lay = cJSON_AddArrayToObject(o, "layout");
    for (int i = 0; i < TG_LAYOUT_N; i++) cJSON_AddItemToArray(lay, cJSON_CreateString(s.layout[i]));
    cJSON_AddBoolToObject(o, "buzzer", s.buzzer);
    cJSON_AddNumberToObject(o, "buzzer_volume", s.buzzer_volume);
    cJSON_AddBoolToObject(o, "quiet_night", s.quiet_night);
    cJSON_AddNumberToObject(o, "alert_cpu_temp", s.alert_cpu_temp);
    cJSON_AddNumberToObject(o, "alert_gpu_temp", s.alert_gpu_temp);
    cJSON_AddNumberToObject(o, "alert_vram_temp", s.alert_vram_temp);
    cJSON_AddStringToObject(o, "led_mode", settings_led_preset_of(&s));   /* a preset name, or "custom" */
    for (int z = 0; z < 3; z++) {
        char hex[8];
        snprintf(hex, sizeof hex, "#%06lx", (unsigned long)s.led_color[z]);
        cJSON_AddStringToObject(o, LED_ZONE_KEYS[z], s.led_zone[z]);
        cJSON_AddStringToObject(o, LED_COLOR_KEYS[z], hex);
    }
    cJSON_AddNumberToObject(o, "led_brightness", s.led_brightness);
    cJSON_AddBoolToObject(o, "key_beep", s.key_beep);
    cJSON_AddBoolToObject(o, "chime", s.chime);
    cJSON_AddBoolToObject(o, "alarm_on", s.alarm_on);
    cJSON_AddStringToObject(o, "alarm_time", s.alarm_time);
    cJSON_AddNumberToObject(o, "alarm_days", s.alarm_days);
    cJSON_AddNumberToObject(o, "tuner_a4", s.tuner_a4);
    cJSON_AddNumberToObject(o, "metro_bpm", s.metro_bpm);
    cJSON_AddNumberToObject(o, "timer_min", s.timer_min);
    cJSON_AddNumberToObject(o, "idle_min", s.idle_min);
    cJSON_AddNumberToObject(o, "idle_brightness", s.idle_brightness);
    cJSON_AddBoolToObject(o, "mic_stream", s.mic_stream);
    cJSON_AddStringToObject(o, "audio_view", s.audio_view);
    char accent[8];
    snprintf(accent, sizeof accent, "#%06lx", (unsigned long)s.accent);
    cJSON_AddStringToObject(o, "accent", accent);
    cJSON_AddStringToObject(o, "viz_palette", VIZ_PALETTES[s.viz_palette < VIZ_PAL_COUNT ? s.viz_palette : 0]);
    cJSON_AddStringToObject(o, "clap", s.clap);
    cJSON_AddBoolToObject(o, "noise_alert", s.noise_alert);
    cJSON *order = cJSON_AddArrayToObject(o, "world_order");
    for (int i = 0; i < 3; i++) cJSON_AddItemToArray(order, cJSON_CreateNumber(s.world_order[i] - '0'));
    cJSON *wn = cJSON_AddArrayToObject(o, "world_names"), *wt = cJSON_AddArrayToObject(o, "world_tz");
    for (int i = 0; i < 2; i++) {
        cJSON_AddItemToArray(wn, cJSON_CreateString(s.world_name[i]));
        cJSON_AddItemToArray(wt, cJSON_CreateString(s.world_tz[i]));
    }
    cJSON *modes = cJSON_AddArrayToObject(o, "led_modes");
    for (size_t i = 0; i < COUNT(LED_MODES); i++) cJSON_AddItemToArray(modes, cJSON_CreateString(LED_MODES[i]));
    cJSON *fx = cJSON_AddArrayToObject(o, "led_effects");
    for (size_t i = 0; i < COUNT(LED_EFFECTS); i++) cJSON_AddItemToArray(fx, cJSON_CreateString(LED_EFFECTS[i]));
    cJSON *views = cJSON_AddArrayToObject(o, "audio_views");
    for (size_t i = 0; i < COUNT(AUDIO_VIEWS); i++) cJSON_AddItemToArray(views, cJSON_CreateString(AUDIO_VIEWS[i]));
    cJSON *claps = cJSON_AddArrayToObject(o, "clap_actions");
    for (size_t i = 0; i < COUNT(CLAP_ACTIONS); i++) cJSON_AddItemToArray(claps, cJSON_CreateString(CLAP_ACTIONS[i]));
    cJSON *pals = cJSON_AddArrayToObject(o, "viz_palettes");
    for (size_t i = 0; i < COUNT(VIZ_PALETTES); i++) cJSON_AddItemToArray(pals, cJSON_CreateString(VIZ_PALETTES[i]));
    cJSON *panels = cJSON_AddArrayToObject(o, "panels");
    for (size_t i = 0; i < COUNT(PANELS); i++) cJSON_AddItemToArray(panels, cJSON_CreateString(PANELS[i]));
    char *out = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return out;
}

static void copy_str(char *dst, size_t n, const cJSON *v)
{
    if (cJSON_IsString(v) && v->valuestring) {
        strlcpy(dst, v->valuestring, n);
    }
}

esp_err_t settings_from_json(const char *json, char *err, int err_len)
{
    cJSON *o = cJSON_Parse(json);
    if (!o) {
        snprintf(err, err_len, "invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }
    tg_settings_t s;
    settings_get(&s);
    const cJSON *v;
    if (cJSON_IsArray(v = cJSON_GetObjectItem(o, "sources"))) {
        for (int i = 0; i < TG_SOURCES; i++) copy_str(s.src_url[i], sizeof s.src_url[i], cJSON_GetArrayItem(v, i));
    }
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "poll_ms"))) s.poll_ms = (uint16_t)v->valuedouble;
    copy_str(s.tz, sizeof s.tz, cJSON_GetObjectItem(o, "tz"));
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "lat"))) s.lat = (float)v->valuedouble;
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "lon"))) s.lon = (float)v->valuedouble;
    if (cJSON_IsBool(v = cJSON_GetObjectItem(o, "hour24"))) s.hour24 = cJSON_IsTrue(v);
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "brightness"))) s.brightness = (uint8_t)v->valuedouble;
    copy_str(s.night_start, sizeof s.night_start, cJSON_GetObjectItem(o, "night_start"));
    copy_str(s.night_end, sizeof s.night_end, cJSON_GetObjectItem(o, "night_end"));
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "night_brightness"))) s.night_brightness = (uint8_t)v->valuedouble;
    if (cJSON_IsArray(v = cJSON_GetObjectItem(o, "layout"))) {
        for (int i = 0; i < TG_LAYOUT_N; i++) copy_str(s.layout[i], sizeof s.layout[i], cJSON_GetArrayItem(v, i));
    }
    if (cJSON_IsBool(v = cJSON_GetObjectItem(o, "buzzer"))) s.buzzer = cJSON_IsTrue(v);
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "buzzer_volume"))) s.buzzer_volume = (uint8_t)v->valuedouble;
    if (cJSON_IsBool(v = cJSON_GetObjectItem(o, "quiet_night"))) s.quiet_night = cJSON_IsTrue(v);
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "alert_cpu_temp"))) s.alert_cpu_temp = (uint8_t)v->valuedouble;
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "alert_gpu_temp"))) s.alert_gpu_temp = (uint8_t)v->valuedouble;
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "alert_vram_temp"))) s.alert_vram_temp = (uint8_t)v->valuedouble;
    /* led_mode, the old single mode, sets all three zones as a preset; zones sent beside it win */
    v = cJSON_GetObjectItem(o, "led_mode");
    if (cJSON_IsString(v) && v->valuestring && strcmp(v->valuestring, "custom") != 0 && !settings_led_preset(&s, v->valuestring)) {
        snprintf(err, err_len, "unknown LED mode '%s' (off, glow, status, music)", v->valuestring);
        cJSON_Delete(o);
        return ESP_ERR_INVALID_ARG;
    }
    for (int z = 0; z < 3; z++) {
        copy_str(s.led_zone[z], sizeof s.led_zone[z], cJSON_GetObjectItem(o, LED_ZONE_KEYS[z]));
        bool given;
        uint32_t c = parse_hex(cJSON_GetObjectItem(o, LED_COLOR_KEYS[z]), &given);
        if (given) s.led_color[z] = c;
    }
    bool given;
    uint32_t accent = parse_hex(cJSON_GetObjectItem(o, "accent"), &given);
    if (given) s.accent = accent;
    copy_str(s.clap, sizeof s.clap, cJSON_GetObjectItem(o, "clap"));
    if (cJSON_IsBool(v = cJSON_GetObjectItem(o, "noise_alert"))) s.noise_alert = cJSON_IsTrue(v);
    v = cJSON_GetObjectItem(o, "viz_palette");
    if (cJSON_IsString(v) && v->valuestring) {
        s.viz_palette = 0xFF;                           /* unknown: fails validation */
        for (int i = 0; i < VIZ_PAL_COUNT; i++) if (strcmp(v->valuestring, VIZ_PALETTES[i]) == 0) s.viz_palette = (uint8_t)i;
    }
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "led_brightness"))) s.led_brightness = (uint8_t)v->valuedouble;
    if (cJSON_IsBool(v = cJSON_GetObjectItem(o, "key_beep"))) s.key_beep = cJSON_IsTrue(v);
    if (cJSON_IsBool(v = cJSON_GetObjectItem(o, "chime"))) s.chime = cJSON_IsTrue(v);
    if (cJSON_IsBool(v = cJSON_GetObjectItem(o, "alarm_on"))) s.alarm_on = cJSON_IsTrue(v);
    copy_str(s.alarm_time, sizeof s.alarm_time, cJSON_GetObjectItem(o, "alarm_time"));
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "alarm_days"))) s.alarm_days = (uint8_t)v->valuedouble & 0x7F;
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "tuner_a4"))) s.tuner_a4 = (uint16_t)v->valuedouble;
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "metro_bpm"))) s.metro_bpm = (uint16_t)v->valuedouble;
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "timer_min"))) s.timer_min = (uint16_t)v->valuedouble;
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "idle_min"))) s.idle_min = (uint8_t)(v->valuedouble > 255 ? 255 : v->valuedouble < 0 ? 0 : v->valuedouble);
    if (cJSON_IsBool(v = cJSON_GetObjectItem(o, "mic_stream"))) s.mic_stream = cJSON_IsTrue(v);
    copy_str(s.audio_view, sizeof s.audio_view, cJSON_GetObjectItem(o, "audio_view"));
    if (cJSON_IsArray(v = cJSON_GetObjectItem(o, "world_order"))) {
        for (int i = 0; i < 3; i++) {             /* anything but 0, 1, 2 once each fails validation */
            const cJSON *p = cJSON_GetArrayItem(v, i);
            int k = cJSON_IsNumber(p) ? (int)p->valuedouble : -1;
            s.world_order[i] = (char)(k >= 0 && k <= 2 ? '0' + k : '?');
        }
        s.world_order[3] = 0;
        if (cJSON_GetArraySize(v) != 3) s.world_order[0] = '?';
    }
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "idle_brightness"))) s.idle_brightness = (uint8_t)(v->valuedouble > 255 ? 255 : v->valuedouble < 0 ? 0 : v->valuedouble);
    if (cJSON_IsArray(v = cJSON_GetObjectItem(o, "world_names"))) {
        for (int i = 0; i < 2; i++) copy_str(s.world_name[i], sizeof s.world_name[i], cJSON_GetArrayItem(v, i));
    }
    if (cJSON_IsArray(v = cJSON_GetObjectItem(o, "world_tz"))) {
        for (int i = 0; i < 2; i++) copy_str(s.world_tz[i], sizeof s.world_tz[i], cJSON_GetArrayItem(v, i));
    }
    for (int i = 0; i < 2; i++) {                     /* the screens use capitals */
        for (char *c = s.world_name[i]; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    }
    cJSON_Delete(o);
    esp_err_t e = validate(&s, err, err_len);
    if (e != ESP_OK) return e;
    e = settings_set(&s);
    if (e != ESP_OK) snprintf(err, err_len, "applied, but saving failed: %s", esp_err_to_name(e));
    return e;
}
