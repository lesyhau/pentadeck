#include "net/web.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"

#include "app.h"
#include "core/history.h"
#include "core/listen.h"
#include "core/power.h"
#include "core/settings.h"
#include "drivers/buttons.h"
#include "drivers/buzzer.h"
#include "drivers/display.h"
#include "drivers/leds.h"
#include "drivers/mic.h"
#include "gfx/hud.h"
#include "modes/modes.h"
#include "net/netdata.h"
#include "net/stream.h"
#include "ui/ledfx.h"
#include "ui/ui.h"

/* The settings page: one self-contained HTML file built from webui/ by
 * webui/build.mjs, stored gzip-compressed and sent as is (every browser accepts gzip). */
extern const uint8_t index_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_gz_end[] asm("_binary_index_html_gz_end");

static esp_err_t page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)index_gz_start, index_gz_end - index_gz_start);
}

static void add_num(cJSON *o, const char *k, float v)
{
    if (isnan(v)) cJSON_AddNullToObject(o, k);
    else cJSON_AddNumberToObject(o, k, round((double)v * 10.0) / 10.0);   /* double: no 1.79999 noise */
}

static esp_err_t status_get(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "fw", app_fw_version());
    cJSON_AddStringToObject(o, "running", esp_ota_get_running_partition()->label);
    cJSON_AddNumberToObject(o, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddStringToObject(o, "ip", app_ip());
    cJSON_AddNumberToObject(o, "rssi", app_rssi());
    cJSON_AddNumberToObject(o, "free_heap", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    cJSON_AddNumberToObject(o, "free_psram", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddBoolToObject(o, "display", tg_display_ok());
    cJSON_AddNumberToObject(o, "backlight", tg_backlight_get());

    char when[32] = "";
    if (netdata_time_valid()) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);
    }
    cJSON_AddStringToObject(o, "time", when);

    weather_t w;
    netdata_get_weather(&w);
    cJSON *wo = cJSON_AddObjectToObject(o, "weather");
    cJSON_AddBoolToObject(wo, "ok", w.ok);
    add_num(wo, "temp_c", w.ok ? w.temp_c : NAN);
    add_num(wo, "humidity", w.ok ? w.humidity : NAN);
    cJSON_AddNumberToObject(wo, "code", w.code);
    cJSON_AddNumberToObject(wo, "age_s", w.age_ms >= 0 ? (double)(w.age_ms / 1000) : -1);

    cJSON *srcs = cJSON_AddArrayToObject(o, "sources");
    for (int i = 0; i < TG_SOURCES; i++) {
        host_stats_t h;
        netdata_get_host(i, &h);
        cJSON *s = cJSON_CreateObject();
        cJSON_AddBoolToObject(s, "ok", h.ok);
        cJSON_AddStringToObject(s, "name", h.name);
        cJSON_AddStringToObject(s, "host", h.host);
        cJSON_AddNumberToObject(s, "age_ms", (double)h.age_ms);
        add_num(s, "cpu_load", h.ever ? h.cpu_load : NAN);
        add_num(s, "cpu_temp", h.ever ? h.cpu_temp : NAN);
        add_num(s, "ram_pct", h.ram_total ? 100.0f * h.ram_used / h.ram_total : NAN);
        if (h.has_gpu) {
            add_num(s, "gpu_load", h.gpu.load);
            add_num(s, "gpu_temp", h.gpu.temp);
            add_num(s, "vram_temp", h.gpu.vram_temp);
        }
        if (h.has_battery) cJSON_AddNumberToObject(s, "battery", h.bat_pct);
        int up = 0;
        for (int k = 0; k < h.n_services; k++) up += h.services[k].up;
        if (h.n_services) {
            char sv[16];
            snprintf(sv, sizeof sv, "%d/%d", up, h.n_services);
            cJSON_AddStringToObject(s, "services_up", sv);
        }
        cJSON_AddItemToArray(srcs, s);
    }

    char uis[256];
    ui_stats_json(uis, sizeof uis);
    cJSON_AddRawToObject(o, "ui", uis);

    mic_snapshot_t m;
    mic_get(&m);
    cJSON *mo = cJSON_AddObjectToObject(o, "mic");
    cJSON_AddStringToObject(mo, "mode", m.mode ? m.mode : "");
    add_num(mo, "dbfs", m.dbfs);
    add_num(mo, "dba", m.dba);
    add_num(mo, "pitch_hz", m.pitch_hz);
    add_num(mo, "pitch_clarity", m.pitch_clarity * 100.0f);
    cJSON_AddNumberToObject(mo, "listeners", stream_listeners());   /* PCs streaming the mic now */
    cJSON_AddNumberToObject(mo, "rate", mic_rate());
    add_num(mo, "background_dbfs", mic_floor_db());

    float dose, leq;
    listen_dose(&dose, &leq);
    cJSON *ho = cJSON_AddObjectToObject(o, "hearing");      /* the day's sound dose (core/listen.c) */
    add_num(ho, "dose_pct", dose);
    add_num(ho, "leq_dba", leq);

    cJSON *po = cJSON_AddObjectToObject(o, "power");
    cJSON_AddStringToObject(po, "state", power_idle() ? "idle" : "full");
    cJSON_AddStringToObject(po, "last_sign", power_last_reason());   /* sound, button, pc input, ... */
    cJSON_AddNumberToObject(po, "quiet_s", power_quiet_s());
    cJSON_AddStringToObject(o, "mode", modes_name());
    add_num(o, "tempo_bpm", modes_tempo_bpm());

    char *out = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, out);
    free(out);
    return err;
}

static esp_err_t settings_get_h(httpd_req_t *req)
{
    char *out = settings_to_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, out);
    free(out);
    return err;
}

static esp_err_t settings_post_h(httpd_req_t *req)
{
    if (!http_authorized(req)) {
        return ESP_OK;
    }
    if (req->content_len <= 0 || req->content_len > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body must be JSON, up to 4 KB");
        return ESP_OK;
    }
    char *body = malloc(req->content_len + 1);
    int got = 0;
    while (body && got < req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            free(body);
            return ESP_FAIL;
        }
        got += n;
    }
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_OK;
    }
    body[got] = 0;
    char err[96] = "";
    esp_err_t e = settings_from_json(body, err, sizeof err);
    free(body);
    if (e == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, err);
        return ESP_OK;
    }
    netdata_apply_tz();
    return settings_get_h(req);   /* echo what is now in effect */
}

static int query_int(httpd_req_t *req, const char *key, int def)
{
    char q[64], v[12];
    if (httpd_req_get_url_query_str(req, q, sizeof q) != ESP_OK) return def;
    if (httpd_query_key_value(q, key, v, sizeof v) != ESP_OK) return def;
    return atoi(v);
}

/* POST /api/button?k=light|left|power|right|mode[&e=short|long|down]: press a button from afar
 * (default: a short press; left / right send "down", which is what they act on) */
static esp_err_t button_post(httpd_req_t *req)
{
    if (!http_authorized(req)) return ESP_OK;
    char q[64] = "", k[12] = "", e[8] = "";
    httpd_req_get_url_query_str(req, q, sizeof q);
    httpd_query_key_value(q, "k", k, sizeof k);
    httpd_query_key_value(q, "e", e, sizeof e);
    btn_key_t key = buttons_key_from_name(k);
    if (key == KEY_NONE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "k must be light, left, power, right or mode");
        return ESP_OK;
    }
    bool arrow = key == KEY_LEFT || key == KEY_RIGHT;
    btn_kind_t kind = strcmp(e, "long") == 0 ? BTN_LONG : strcmp(e, "down") == 0 || (!e[0] && arrow) ? BTN_DOWN : BTN_SHORT;
    if (kind != BTN_DOWN) buttons_inject(key, BTN_DOWN);      /* as a real press: down first */
    buttons_inject(key, kind);
    return httpd_resp_sendstr(req, "ok\n");
}

/* GET /api/pcm: the tuner's last 2048 samples (16 kHz, float32 little-endian), while the tuner runs */
static esp_err_t pcm_get(httpd_req_t *req)
{
    float *buf = heap_caps_malloc(2048 * sizeof(float), MALLOC_CAP_SPIRAM);
    if (!buf) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    int n = mic_tuner_samples(buf, 2048);
    httpd_resp_set_type(req, "application/octet-stream");
    esp_err_t err = httpd_resp_send(req, (const char *)buf, n * sizeof(float));
    free(buf);
    return err;
}

/* POST /api/power?state=idle|wake: low power now (until the next sign of someone), or full power */
static esp_err_t power_post(httpd_req_t *req)
{
    if (!http_authorized(req)) return ESP_OK;
    char q[32] = "", st[8] = "";
    httpd_req_get_url_query_str(req, q, sizeof q);
    httpd_query_key_value(q, "state", st, sizeof st);
    if (strcmp(st, "idle") == 0) power_sleep_now();
    else if (strcmp(st, "wake") == 0) power_wake("web");
    else return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "state must be idle or wake");
    return httpd_resp_sendstr(req, "ok\n");
}

/* Pairing a browser without typing the token: POST /api/pair asks on the device's screens and returns
 * {"id", "code"}; GET /api/pair?id=N returns {"state": "waiting" | "allowed" | "refused"}, with the
 * token once, when the owner pressed power on the device. */
static esp_err_t pair_post(httpd_req_t *req)
{
    uint16_t code;
    uint32_t id = modes_pair_start(&code);
    if (!id) {
        httpd_resp_set_status(req, "429 Too Many Requests");
        return httpd_resp_sendstr(req, "{\"error\":\"the device is already asking, or was just asked\"}");
    }
    char buf[64];
    snprintf(buf, sizeof buf, "{\"id\":%lu,\"code\":\"%u\"}", (unsigned long)id, code);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

static esp_err_t pair_get(httpd_req_t *req)
{
    char q[48] = "", v[16] = "";
    httpd_req_get_url_query_str(req, q, sizeof q);
    httpd_query_key_value(q, "id", v, sizeof v);
    uint32_t id = (uint32_t)strtoul(v, NULL, 10);
    int st = modes_pair_state(id);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (st == 1) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "state", "allowed");
        cJSON_AddStringToObject(o, "token", app_token());
        char *out = cJSON_PrintUnformatted(o);
        cJSON_Delete(o);
        modes_pair_done(id);                          /* the token goes out once */
        esp_err_t err = httpd_resp_sendstr(req, out);
        free(out);
        return err;
    }
    return httpd_resp_sendstr(req, st == 0 ? "{\"state\":\"waiting\"}" : "{\"state\":\"refused\"}");
}

/* GET /api/screen?s=0..4: what that screen shows, as 128x128 big-endian RGB565 (32768 bytes) */
static esp_err_t screen_get(httpd_req_t *req)
{
    int s = query_int(req, "s", 0);
    uint16_t *buf = heap_caps_malloc(HUD_W * HUD_H * 2, MALLOC_CAP_SPIRAM);
    if (!buf) buf = malloc(HUD_W * HUD_H * 2);
    if (!buf || ui_capture(s, buf) != ESP_OK) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "s must be 0..4");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, (const char *)buf, HUD_W * HUD_H * 2);
    free(buf);
    return err;
}

/* POST /api/beep[?p=0..4&v=0..100]: play an alert pattern (default: the test chime at the set volume) */
static esp_err_t beep_post(httpd_req_t *req)
{
    if (!http_authorized(req)) return ESP_OK;
    tg_settings_t s;
    settings_get(&s);
    int hz = query_int(req, "hz", 0);
    if (hz) buzzer_tone(hz, query_int(req, "ms", 1000), query_int(req, "v", s.buzzer_volume));
    else buzzer_play((buzzer_pattern_t)query_int(req, "p", BZ_TEST), query_int(req, "v", s.buzzer_volume));
    return httpd_resp_sendstr(req, "ok\n");
}

/* POST /api/leds?mode=auto|groups|off|fill|one|range[&c=RRGGBB&n=COUNT&i=INDEX&from=&to=&b=0..255]:
 * LED chain tests while it's mapped */
static esp_err_t leds_post(httpd_req_t *req)
{
    if (!http_authorized(req)) return ESP_OK;
    char q[96] = "", mode[12] = "groups", c[12] = "FFFFFF";
    httpd_req_get_url_query_str(req, q, sizeof q);
    httpd_query_key_value(q, "mode", mode, sizeof mode);
    httpd_query_key_value(q, "c", c, sizeof c);
    int b = query_int(req, "b", 40), n = query_int(req, "n", LEDS_MAX), at = query_int(req, "i", -1);
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    esp_err_t err;
    static uint32_t px[LEDS_MAX];
    if (strcmp(mode, "auto") == 0) {          /* back to the effects */
        ledfx_hold(0);
        return httpd_resp_sendstr(req, "ok\n");
    }
    ledfx_hold(120000);                       /* a test holds the LEDs for two minutes */
    if (strcmp(mode, "keys") == 0) {          /* buttons board LEDs, b = 0..100 % */
        leds_keypad((uint8_t)(b > 100 ? 100 : b));
        return httpd_resp_sendstr(req, "ok\n");
    }
    uint32_t col = (uint32_t)strtoul(c, NULL, 16);
    if (strcmp(mode, "off") == 0) {
        memset(px, 0, sizeof px);
        err = leds_off();
    } else if (strcmp(mode, "range") == 0) {
        /* paint [from, to) on top of what is already lit, so blocks can be composed */
        int from = query_int(req, "from", 0), to = query_int(req, "to", LEDS_MAX);
        for (int i = from < 0 ? 0 : from; i < to && i < LEDS_MAX; i++) px[i] = col;
        err = leds_show(px, LEDS_MAX, (uint8_t)b);
    } else if (strcmp(mode, "fill") == 0 || strcmp(mode, "one") == 0) {
        if (n > LEDS_MAX) n = LEDS_MAX;
        for (int i = 0; i < LEDS_MAX; i++) px[i] = (mode[0] == 'f' ? i < n : i == at) ? col : 0;
        err = leds_show(px, LEDS_MAX, (uint8_t)b);
    } else {
        err = leds_test_groups((uint8_t)b);
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_OK;
    }
    return httpd_resp_sendstr(req, "ok\n");
}

/* A body of up to `max` bytes, NUL-terminated; NULL (and the error sent) when it isn't that. */
static char *read_body(httpd_req_t *req, int max)
{
    if (req->content_len <= 0 || req->content_len > max) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "wrong body size");
        return NULL;
    }
    char *body = malloc(req->content_len + 1);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return NULL;
    }
    int got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            free(body);
            return NULL;
        }
        got += n;
    }
    body[got] = 0;
    return body;
}

/* %XX and + in a query value, in place */
static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '+') *o++ = ' ';
        else if (*s == '%' && s[1] && s[2]) {
            char h[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(h, NULL, 16);
            s += 2;
        } else *o++ = *s;
    }
    *o = 0;
}

static const struct { const char *name; int pattern; } SOUNDS[] = {
    { "none", -1 }, { "chime", BZ_CHIME }, { "test", BZ_TEST }, { "up", BZ_UP }, { "down", BZ_DOWN }, { "warn", BZ_WARN },
    { "power", BZ_POWER }, { "battery", BZ_BATTERY }, { "alarm", BZ_ALARM }, { "boot", BZ_BOOT }, { "key", BZ_KEY },
};

/* POST /api/notify {"title", "text", "seconds" (10; 0 = until a button), "sound" (chime, none, up, ...),
 * "color" ("#rrggbb", the accent by default)}: a card over the middle screen, the LEDs pulsing */
static esp_err_t notify_post(httpd_req_t *req)
{
    if (!http_authorized(req)) return ESP_OK;
    char *body = read_body(req, 2048);
    if (!body) return ESP_OK;
    cJSON *o = cJSON_Parse(body);
    free(body);
    if (!o) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body must be JSON");
    const cJSON *title = cJSON_GetObjectItem(o, "title"), *text = cJSON_GetObjectItem(o, "text");
    const cJSON *secs = cJSON_GetObjectItem(o, "seconds"), *snd = cJSON_GetObjectItem(o, "sound");
    const cJSON *col = cJSON_GetObjectItem(o, "color");
    if (!cJSON_IsString(title) && !cJSON_IsString(text)) {
        cJSON_Delete(o);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "give a title or a text");
    }
    int sound = BZ_CHIME;
    if (cJSON_IsString(snd)) {
        sound = -2;
        for (size_t i = 0; i < sizeof SOUNDS / sizeof SOUNDS[0]; i++) if (strcmp(snd->valuestring, SOUNDS[i].name) == 0) sound = SOUNDS[i].pattern;
        if (sound == -2) {
            cJSON_Delete(o);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "sound: none, chime, test, up, down, warn, power, battery, alarm, boot or key");
        }
    }
    uint32_t color = hud_ramp.p400;
    if (cJSON_IsString(col)) {
        const char *h = col->valuestring + (col->valuestring[0] == '#');
        char *end;
        unsigned long c = strtoul(h, &end, 16);
        if (strlen(h) != 6 || *end) {
            cJSON_Delete(o);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "color must be #rrggbb");
        }
        color = (uint32_t)c;
    }
    modes_notify(cJSON_IsString(title) ? title->valuestring : "", cJSON_IsString(text) ? text->valuestring : "",
                 cJSON_IsNumber(secs) ? (int)secs->valuedouble : 10, sound, color);
    cJSON_Delete(o);
    return httpd_resp_sendstr(req, "ok\n");
}

/* GET /api/spectrum: the 16 bands now, 0..100 (the screens' scale), and the level */
static esp_err_t spectrum_get(httpd_req_t *req)
{
    mic_snapshot_t m;
    mic_get(&m);
    char buf[256];
    int k = snprintf(buf, sizeof buf, "{\"bands\":[");
    for (int b = 0; b < MIC_BANDS; b++) {
        float f = (m.bands[b] - MIC_BAND_FLOOR_DB) / (MIC_BAND_TOP_DB - MIC_BAND_FLOOR_DB);
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        k += snprintf(buf + k, sizeof buf - k, "%s%d", b ? "," : "", (int)lroundf(f * 100));
    }
    if (isnan(m.dba)) k += snprintf(buf + k, sizeof buf - k, "],\"dba\":null}");
    else k += snprintf(buf + k, sizeof buf - k, "],\"dba\":%.1f}", m.dba);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, k);
}

/* GET /api/history: a value a minute for the last 24 hours, oldest first (core/history.h) */
static esp_err_t history_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    int n = history_count();
    char buf[512];
    int k = snprintf(buf, sizeof buf, "{\"step_s\":%d,\"count\":%d,\"last\":%lld,\"series\":{", HIST_STEP_S, n,
                     (long long)history_last());
    for (int s = 0; s < HIST_SERIES; s++) {
        k += snprintf(buf + k, sizeof buf - k, "%s\"%s\":[", s ? "," : "", history_name((hist_series_t)s));
        for (int i = 0; i < n; i++) {
            if (k > (int)sizeof buf - 16) {
                if (httpd_resp_send_chunk(req, buf, k) != ESP_OK) return ESP_FAIL;
                k = 0;
            }
            float v = history_at((hist_series_t)s, i);
            if (isnan(v)) k += snprintf(buf + k, sizeof buf - k, "%snull", i ? "," : "");
            else k += snprintf(buf + k, sizeof buf - k, "%s%.1f", i ? "," : "", v);
        }
        k += snprintf(buf + k, sizeof buf - k, "]");
    }
    k += snprintf(buf + k, sizeof buf - k, "}}");
    if (httpd_resp_send_chunk(req, buf, k) != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* GET /api/sounds: the learned sounds and any learning going on */
static esp_err_t sounds_get(httpd_req_t *req)
{
    listen_slot_t sl[LISTEN_SLOTS];
    listen_slots(sl);
    uint32_t left;
    int learning = listen_learning(&left);
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(o, "slots");
    for (int i = 0; i < LISTEN_SLOTS; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddBoolToObject(s, "used", sl[i].used);
        cJSON_AddBoolToObject(s, "on", sl[i].on);
        cJSON_AddStringToObject(s, "name", sl[i].name);
        cJSON_AddItemToArray(arr, s);
    }
    cJSON_AddNumberToObject(o, "learning", learning);
    cJSON_AddNumberToObject(o, "left_ms", left);
    static const char *const RES[] = { "none", "learned", "nothing" };
    int r = listen_learn_result();
    cJSON_AddStringToObject(o, "result", RES[r >= 0 && r <= 2 ? r : 0]);
    char *out = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, out);
    free(out);
    return err;
}

/* POST /api/sounds?slot=0..2 and one of &learn=NAME (listen 4 s for it), &on=0|1, &forget=1 */
static esp_err_t sounds_post(httpd_req_t *req)
{
    if (!http_authorized(req)) return ESP_OK;
    char q[128] = "", v[48] = "";
    httpd_req_get_url_query_str(req, q, sizeof q);
    int slot = httpd_query_key_value(q, "slot", v, sizeof v) == ESP_OK ? atoi(v) : -1;
    if (slot < 0 || slot >= LISTEN_SLOTS) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "slot must be 0..2");
    bool ok;
    if (httpd_query_key_value(q, "learn", v, sizeof v) == ESP_OK) {
        url_decode(v);
        if (!v[0]) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "learn needs a name");
        ok = listen_learn(slot, v);
        if (!ok) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "already learning a sound");
    } else if (httpd_query_key_value(q, "on", v, sizeof v) == ESP_OK) {
        ok = listen_enable(slot, atoi(v) != 0);
        if (!ok) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "nothing learned in that slot");
    } else if (httpd_query_key_value(q, "forget", v, sizeof v) == ESP_OK) {
        listen_forget(slot);
    } else {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "learn=NAME, on=0|1 or forget=1");
    }
    return sounds_get(req);
}

/* GET /api/emblem: your clock image (36 x 36 big-endian RGB565), 404 while the logo shows.
 * POST /api/emblem with that as the body sets it; POST /api/emblem?clear=1 brings the logo back. */
static esp_err_t emblem_get(httpd_req_t *req)
{
    static uint8_t px[UI_EMBLEM * UI_EMBLEM * 2];
    if (!ui_emblem_get(px)) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "the logo shows");
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)px, sizeof px);
}

static esp_err_t emblem_post(httpd_req_t *req)
{
    if (!http_authorized(req)) return ESP_OK;
    if (query_int(req, "clear", 0)) {
        ui_emblem_set(NULL, 0);
        return httpd_resp_sendstr(req, "ok\n");
    }
    const int len = UI_EMBLEM * UI_EMBLEM * 2;
    if (req->content_len != len) {
        char e[64];
        snprintf(e, sizeof e, "the body must be %d bytes (36 x 36 RGB565)", len);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, e);
    }
    char *body = read_body(req, len);
    if (!body) return ESP_OK;
    esp_err_t err = ui_emblem_set((const uint8_t *)body, len);
    free(body);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    return httpd_resp_sendstr(req, "ok\n");
}

void web_register(httpd_handle_t server)
{
    const httpd_uri_t uris[] = {
        { .uri = "/",             .method = HTTP_GET,  .handler = page_get },
        { .uri = "/api/status",   .method = HTTP_GET,  .handler = status_get },
        { .uri = "/api/settings", .method = HTTP_GET,  .handler = settings_get_h },
        { .uri = "/api/settings", .method = HTTP_POST, .handler = settings_post_h },
        { .uri = "/api/button",   .method = HTTP_POST, .handler = button_post },
        { .uri = "/api/pcm",      .method = HTTP_GET,  .handler = pcm_get },
        { .uri = "/api/pair",     .method = HTTP_POST, .handler = pair_post },
        { .uri = "/api/pair",     .method = HTTP_GET,  .handler = pair_get },
        { .uri = "/api/screen",   .method = HTTP_GET,  .handler = screen_get },
        { .uri = "/api/beep",     .method = HTTP_POST, .handler = beep_post },
        { .uri = "/api/leds",     .method = HTTP_POST, .handler = leds_post },
        { .uri = "/api/power",    .method = HTTP_POST, .handler = power_post },
        { .uri = "/api/notify",   .method = HTTP_POST, .handler = notify_post },
        { .uri = "/api/spectrum", .method = HTTP_GET,  .handler = spectrum_get },
        { .uri = "/api/history",  .method = HTTP_GET,  .handler = history_get },
        { .uri = "/api/sounds",   .method = HTTP_GET,  .handler = sounds_get },
        { .uri = "/api/sounds",   .method = HTTP_POST, .handler = sounds_post },
        { .uri = "/api/emblem",   .method = HTTP_GET,  .handler = emblem_get },
        { .uri = "/api/emblem",   .method = HTTP_POST, .handler = emblem_post },
    };
    for (size_t i = 0; i < sizeof uris / sizeof uris[0]; i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
}
