#include "web.h"

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
#include "buttons.h"
#include "buzzer.h"
#include "display.h"
#include "hud.h"
#include "ledfx.h"
#include "leds.h"
#include "mic.h"
#include "modes.h"
#include "netdata.h"
#include "power.h"
#include "settings.h"
#include "stream.h"
#include "ui.h"

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
    };
    for (size_t i = 0; i < sizeof uris / sizeof uris[0]; i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
}
