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
#define VERSION 10                  /* 2 the buzzer, 3 the LEDs, 4 the key beep, 5 the alarm and chime, 6 the world clock,
                                       7 the modes' values, 8 low power, 9 the mic stream switch, 10 the AUDIO view */

typedef struct {
    uint32_t version;
    tg_settings_t s;
} stored_t;

static tg_settings_t s_cur;
static SemaphoreHandle_t s_lock;
static volatile uint32_t s_gen;

static const char *PANELS[] = { "lab", "cpu", "nav", "gpu", "audio", "off" };
static const char *LED_MODES[] = { "off", "glow", "status", "music" };
static const char *AUDIO_VIEWS[] = { "eq", "vu" };

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
    strcpy(s->led_mode, "status");
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
    strcpy(s->audio_view, "eq");
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
    bool mode_ok = false;
    for (size_t i = 0; i < sizeof LED_MODES / sizeof LED_MODES[0]; i++) mode_ok |= strcmp(s->led_mode, LED_MODES[i]) == 0;
    if (!mode_ok) FAIL("unknown LED mode '%s'", s->led_mode);
    if (s->led_brightness > 100) FAIL("LED brightness is 0..100");
    bool view_ok = false;
    for (size_t i = 0; i < sizeof AUDIO_VIEWS / sizeof AUDIO_VIEWS[0]; i++) view_ok |= strcmp(s->audio_view, AUDIO_VIEWS[i]) == 0;
    if (!view_ok) FAIL("unknown audio view '%s' (eq or vu)", s->audio_view);
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
        stored_t st;
        size_t len = sizeof st;
        if (nvs_get_blob(h, KEY, &st, &len) == ESP_OK) {
            tg_settings_t s;
            defaults(&s);
            if (st.version == VERSION && len == sizeof st) {
                s = st.s;
            } else if (st.version == 9 && len >= sizeof(uint32_t) + offsetof(tg_settings_t, audio_view)) {
                memcpy(&s, &st.s, offsetof(tg_settings_t, audio_view));
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
            } else if (st.version == 2 && len >= sizeof(uint32_t) + offsetof(tg_settings_t, led_mode)) {
                memcpy(&s, &st.s, offsetof(tg_settings_t, led_mode));   /* v2 fields; the new ones keep defaults */
            } else if (st.version == 1 && len >= sizeof(uint32_t) + offsetof(tg_settings_t, buzzer)) {
                memcpy(&s, &st.s, offsetof(tg_settings_t, buzzer));
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
    cJSON_AddStringToObject(o, "led_mode", s.led_mode);
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
    cJSON *wn = cJSON_AddArrayToObject(o, "world_names"), *wt = cJSON_AddArrayToObject(o, "world_tz");
    for (int i = 0; i < 2; i++) {
        cJSON_AddItemToArray(wn, cJSON_CreateString(s.world_name[i]));
        cJSON_AddItemToArray(wt, cJSON_CreateString(s.world_tz[i]));
    }
    cJSON *modes = cJSON_AddArrayToObject(o, "led_modes");
    for (size_t i = 0; i < sizeof LED_MODES / sizeof LED_MODES[0]; i++) cJSON_AddItemToArray(modes, cJSON_CreateString(LED_MODES[i]));
    cJSON *views = cJSON_AddArrayToObject(o, "audio_views");
    for (size_t i = 0; i < sizeof AUDIO_VIEWS / sizeof AUDIO_VIEWS[0]; i++) cJSON_AddItemToArray(views, cJSON_CreateString(AUDIO_VIEWS[i]));
    cJSON *panels = cJSON_AddArrayToObject(o, "panels");
    for (size_t i = 0; i < sizeof PANELS / sizeof PANELS[0]; i++) cJSON_AddItemToArray(panels, cJSON_CreateString(PANELS[i]));
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
    copy_str(s.led_mode, sizeof s.led_mode, cJSON_GetObjectItem(o, "led_mode"));
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
