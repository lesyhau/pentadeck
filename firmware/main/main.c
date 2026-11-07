/*
 * Pentadeck: open cockpit firmware for the Divoom Times Gate (ESP32-S3, five 128x128 LCDs).
 *
 *  - Joins Wi-Fi (credentials in secrets.h). If Wi-Fi isn't up within WIFI_FALLBACK_S seconds it
 *    boots the other app slot, so a bad network setup can't strand the device.
 *  - Draws the cockpit on the five screens (ui.c), drives the LED chain (ledfx.c), the buzzer
 *    (alerts.c) and listens to the microphone (mic.c).
 *  - Pulls stats JSON from the agents set in the settings (netdata.c), and the time and weather.
 *  - Serves the settings page and its API (web.c) plus a maintenance API on port 80; every
 *    POST needs the X-Token header (OTA_TOKEN in secrets.h):
 *      GET  /status           version, slot, uptime, RSSI, IP, heap
 *      POST /ota              body = app image (.bin) -> written to the other slot, which then boots
 *      POST /switch           boot the other slot
 *      POST /reboot
 *      POST /frame?s=N        body = 128*128 big-endian RGB565 -> screen N (0..4), for tests
 *      POST /rect, /fill, /backlight, /cmd, /test; GET /audio   diagnostics
 *  - Wi-Fi credentials stay in RAM (never written to NVS); settings live in NVS namespace "tg".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "app.h"
#include "core/alerts.h"
#include "core/power.h"
#include "core/settings.h"
#include "drivers/buttons.h"
#include "drivers/buzzer.h"
#include "drivers/display.h"
#include "drivers/leds.h"
#include "drivers/mic.h"
#include "net/netdata.h"
#include "net/stream.h"
#include "net/web.h"
#include "secrets.h"
#include "ui/ledfx.h"
#include "ui/ui.h"

#define FW_VERSION      "0.17.1"
#define WIFI_FALLBACK_S 180   /* generous: the device sits at about -80 dBm */
#define EV_GOT_IP       BIT0

static const char *TAG = "pentadeck";
static EventGroupHandle_t s_events;
static char s_ip[16] = "0.0.0.0";

const char *app_fw_version(void)
{
    return FW_VERSION;
}

const char *app_ip(void)
{
    return s_ip;
}

int app_rssi(void)
{
    wifi_ap_record_t ap = { 0 };
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}

/* ------------------------------------------------------------------ slots */

static const esp_partition_t *other_slot(void)
{
    return esp_ota_get_next_update_partition(esp_ota_get_running_partition());
}

static bool slot_has_app(const esp_partition_t *p)
{
    esp_app_desc_t desc;
    return p != NULL && esp_ota_get_partition_description(p, &desc) == ESP_OK;
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1000));   /* let the HTTP reply go out first */
    esp_restart();
}

static void restart_soon(void)
{
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
}

/* ------------------------------------------------------------------ Wi-Fi */

static void on_net_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %d, rssi %d), reconnecting", d->reason, d->rssi);
        xEventGroupClearBits(s_events, EV_GOT_IP);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "got IP %s", s_ip);
        xEventGroupSetBits(s_events, EV_GOT_IP);
    }
}

static void wifi_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(sta, "pentadeck");

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));   /* never write NVS */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_net_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_net_event, NULL));

    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, WIFI_SSID, sizeof cfg.sta.ssid);
    strlcpy((char *)cfg.sta.password, WIFI_PASS, sizeof cfg.sta.password);
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);   /* mains powered: favour responsiveness */
}

/* If the network never comes up, go back to the other slot rather than sit unreachable. */
static void fallback_task(void *arg)
{
    EventBits_t bits = xEventGroupWaitBits(s_events, EV_GOT_IP, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(WIFI_FALLBACK_S * 1000));
    if (!(bits & EV_GOT_IP)) {
        const esp_partition_t *other = other_slot();
        if (slot_has_app(other) && esp_ota_set_boot_partition(other) == ESP_OK) {
            ESP_LOGE(TAG, "no Wi-Fi after %d s: booting %s instead", WIFI_FALLBACK_S, other->label);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
        ESP_LOGE(TAG, "no Wi-Fi after %d s and no valid app in the other slot; staying here",
                 WIFI_FALLBACK_S);
    }
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------- HTTP */

bool http_authorized(httpd_req_t *req)
{
    char token[80];
    if (httpd_req_get_hdr_value_str(req, "X-Token", token, sizeof token) == ESP_OK &&
        strcmp(token, OTA_TOKEN) == 0) {
        return true;
    }
    httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "missing or wrong X-Token");
    return false;
}

const char *app_token(void)
{
    return OTA_TOKEN;
}

/* Why the chip last restarted, for /status */
static const char *reset_name(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:  return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    default:               return "other";
    }
}

static esp_err_t status_get(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *other = other_slot();
    esp_app_desc_t other_desc = { 0 };
    bool other_ok = other && esp_ota_get_partition_description(other, &other_desc) == ESP_OK;
    wifi_ap_record_t ap = { 0 };
    int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;

    char buf[700];
    int n = snprintf(buf, sizeof buf,
        "{\"fw\":\"%s\",\"idf\":\"%s\",\"built\":\"%s %s\",\"running\":\"%s\","
        "\"other_slot\":\"%s\",\"other_app\":\"%s\",\"uptime_s\":%lld,\"rssi\":%d,"
        "\"ip\":\"%s\",\"free_heap\":%lu,\"display\":%s,\"backlight\":%u,\"reset\":\"%s\"}\n",
        FW_VERSION, app->idf_ver, app->date, app->time, run->label,
        other ? other->label : "-", other_ok ? other_desc.project_name : "",
        esp_timer_get_time() / 1000000, rssi, s_ip, (unsigned long)esp_get_free_heap_size(),
        tg_display_ok() ? "true" : "false", tg_backlight_get(), reset_name());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t ota_post(httpd_req_t *req)
{
    if (!http_authorized(req)) {
        return ESP_OK;
    }
    const esp_partition_t *dst = other_slot();
    if (dst == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA slot");
        return ESP_OK;
    }
    if (req->content_len <= 0 || req->content_len > dst->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body must be an app image that fits the slot");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "OTA: receiving %d bytes into %s", req->content_len, dst->label);

    esp_ota_handle_t ota;
    esp_err_t err = esp_ota_begin(dst, req->content_len, &ota);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_OK;
    }

    static char chunk[4096];   /* httpd runs one handler at a time */
    int left = req->content_len;
    while (left > 0) {
        int got = httpd_req_recv(req, chunk, MIN(left, (int)sizeof chunk));
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (got <= 0) {
            ESP_LOGE(TAG, "OTA: connection lost with %d bytes left", left);
            esp_ota_abort(ota);
            return ESP_FAIL;
        }
        err = esp_ota_write(ota, chunk, got);
        if (err != ESP_OK) {
            esp_ota_abort(ota);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
            return ESP_OK;
        }
        left -= got;
    }

    err = esp_ota_end(ota);   /* verifies the image (format, checksum, SHA-256) */
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(dst);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA: rejected: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
        return ESP_OK;
    }

    char msg[96];
    snprintf(msg, sizeof msg, "ok: %d bytes written to %s, rebooting\n", req->content_len, dst->label);
    ESP_LOGI(TAG, "%s", msg);
    httpd_resp_sendstr(req, msg);
    restart_soon();
    return ESP_OK;
}

static esp_err_t switch_post(httpd_req_t *req)
{
    if (!http_authorized(req)) {
        return ESP_OK;
    }
    const esp_partition_t *other = other_slot();
    if (!slot_has_app(other) || esp_ota_set_boot_partition(other) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "the other slot has no valid app");
        return ESP_OK;
    }
    httpd_resp_sendstr(req, "ok: booting the other slot\n");
    restart_soon();
    return ESP_OK;
}

static esp_err_t reboot_post(httpd_req_t *req)
{
    if (!http_authorized(req)) {
        return ESP_OK;
    }
    httpd_resp_sendstr(req, "ok: rebooting\n");
    restart_soon();
    return ESP_OK;
}

static int query_int(httpd_req_t *req, const char *key, int base, int fallback)
{
    char query[96], value[24];
    if (httpd_req_get_url_query_str(req, query, sizeof query) != ESP_OK ||
        httpd_query_key_value(query, key, value, sizeof value) != ESP_OK) {
        return fallback;
    }
    return (int)strtol(value, NULL, base);
}

static esp_err_t frame_post(httpd_req_t *req)
{
    if (!http_authorized(req)) {
        return ESP_OK;
    }
    int screen = query_int(req, "s", 10, -1);
    if (screen < 0 || screen >= TG_SCREENS || req->content_len != TG_FRAME_BYTES) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "need ?s=0..4 and a 32768-byte big-endian RGB565 body");
        return ESP_OK;
    }
    uint16_t *fb = tg_display_lock();
    if (fb == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "display not ready");
        return ESP_OK;
    }
    char *dst = (char *)fb;
    int left = TG_FRAME_BYTES;
    while (left > 0) {
        int got = httpd_req_recv(req, dst, left);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (got <= 0) {
            tg_display_unlock();
            return ESP_FAIL;
        }
        dst += got;
        left -= got;
    }
    esp_err_t err = tg_display_show(screen);
    tg_display_unlock();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_OK;
    }
    return httpd_resp_sendstr(req, "ok\n");
}

/* POST /rect?s=N&x=X&y=Y&w=W&h=H, body = W*H big-endian RGB565 pixels, row by row. */
static esp_err_t rect_post(httpd_req_t *req)
{
    if (!http_authorized(req)) {
        return ESP_OK;
    }
    int screen = query_int(req, "s", 10, -1);
    int x = query_int(req, "x", 10, -1), y = query_int(req, "y", 10, -1);
    int w = query_int(req, "w", 10, -1), h = query_int(req, "h", 10, -1);
    if (screen < 0 || screen >= TG_SCREENS || x < 0 || y < 0 || w <= 0 || h <= 0 ||
        x + w > TG_W || y + h > TG_H || req->content_len != w * h * 2) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "need ?s=0..4&x&y&w&h inside 128x128 and a w*h*2-byte body");
        return ESP_OK;
    }
    uint16_t *fb = tg_display_lock();
    if (fb == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "display not ready");
        return ESP_OK;
    }
    char *dst = (char *)fb;
    int left = req->content_len;
    while (left > 0) {
        int got = httpd_req_recv(req, dst, left);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (got <= 0) {
            tg_display_unlock();
            return ESP_FAIL;
        }
        dst += got;
        left -= got;
    }
    esp_err_t err = tg_display_show_rect(screen, x, y, w, h);
    tg_display_unlock();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_OK;
    }
    return httpd_resp_sendstr(req, "ok\n");
}

static esp_err_t fill_post(httpd_req_t *req)
{
    if (!http_authorized(req)) {
        return ESP_OK;
    }
    int screen = query_int(req, "s", 10, -1);
    int rgb = query_int(req, "c", 16, -1);
    if (screen < 0 || screen >= TG_SCREENS || rgb < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "need ?s=0..4&c=RRGGBB");
        return ESP_OK;
    }
    esp_err_t err = tg_display_fill(screen, TG_RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_OK;
    }
    return httpd_resp_sendstr(req, "ok\n");
}

static esp_err_t backlight_post(httpd_req_t *req)
{
    if (!http_authorized(req)) {
        return ESP_OK;
    }
    int duty = query_int(req, "d", 10, -1);
    if (duty < 0 || duty > 255) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "need ?d=0..255");
        return ESP_OK;
    }
    tg_backlight_set((uint8_t)duty);
    return httpd_resp_sendstr(req, "ok\n");
}

/* GET /audio: latest mic analysis as JSON. */
static esp_err_t audio_get(httpd_req_t *req)
{
    mic_snapshot_t m;
    mic_get(&m);
    char buf[512];
    int n = snprintf(buf, sizeof buf, "{\"ok\":%s,\"mode\":\"%s\",\"blocks\":%u,\"dbfs\":%.1f,\"peak_dbfs\":%.1f,\"bands\":[",
                     mic_ok() ? "true" : "false", m.mode ? m.mode : "", m.blocks, m.dbfs, m.peak_dbfs);
    for (int b = 0; b < MIC_BANDS; b++) {
        n += snprintf(buf + n, sizeof buf - n, "%s%.1f", b ? "," : "", m.bands[b]);
    }
    int rerr;
    unsigned rfails, rgot;
    mic_read_stats(&rerr, &rfails, &rgot);
    n += snprintf(buf + n, sizeof buf - n, "],\"probe_dbfs\":[%.1f,%.1f,%.1f,%.1f],\"rate\":%d,"
                  "\"read_err\":\"%s\",\"read_fails\":%u,\"fail_wait_ms\":%u}\n",
                  m.probe_dbfs[0], m.probe_dbfs[1], m.probe_dbfs[2], m.probe_dbfs[3], mic_rate(),
                  esp_err_to_name(rerr), rfails, rgot);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t audio_probe_post(httpd_req_t *req)
{
    if (!http_authorized(req)) {
        return ESP_OK;
    }
    mic_reprobe();
    return httpd_resp_sendstr(req, "ok: probing the mic wirings (~2 s), then GET /audio\n");
}

/* Debug: POST /cmd?s=N&c=HEX, optional body = raw parameter bytes (max 32). */
static esp_err_t cmd_post(httpd_req_t *req)
{
    if (!http_authorized(req)) {
        return ESP_OK;
    }
    int screen = query_int(req, "s", 10, -1);
    int c = query_int(req, "c", 16, -1);
    uint8_t params[32];
    if (screen < 0 || screen >= TG_SCREENS || c < 0 || c > 0xFF || req->content_len > (int)sizeof params) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "need ?s=0..4&c=HEX and at most 32 parameter bytes");
        return ESP_OK;
    }
    int got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, (char *)params + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            return ESP_FAIL;
        }
        got += n;
    }
    esp_err_t err = tg_display_cmd(screen, (uint8_t)c, params, got);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_OK;
    }
    return httpd_resp_sendstr(req, "ok\n");
}

static esp_err_t test_post(httpd_req_t *req)
{
    if (!http_authorized(req)) {
        return ESP_OK;
    }
    esp_err_t err = tg_display_test_pattern();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_OK;
    }
    return httpd_resp_sendstr(req, "ok\n");
}

static void http_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 32;
    cfg.stack_size = 8192;
    cfg.recv_wait_timeout = 30;
    cfg.send_wait_timeout = 30;
    cfg.lru_purge_enable = true;

    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));
    const httpd_uri_t uris[] = {
        { .uri = "/status", .method = HTTP_GET,  .handler = status_get },   /* GET / is the web page now */
        { .uri = "/ota",    .method = HTTP_POST, .handler = ota_post },
        { .uri = "/switch", .method = HTTP_POST, .handler = switch_post },
        { .uri = "/reboot", .method = HTTP_POST, .handler = reboot_post },
        { .uri = "/frame",     .method = HTTP_POST, .handler = frame_post },
        { .uri = "/rect",      .method = HTTP_POST, .handler = rect_post },
        { .uri = "/fill",      .method = HTTP_POST, .handler = fill_post },
        { .uri = "/backlight", .method = HTTP_POST, .handler = backlight_post },
        { .uri = "/cmd",       .method = HTTP_POST, .handler = cmd_post },
        { .uri = "/test",      .method = HTTP_POST, .handler = test_post },
        { .uri = "/audio",     .method = HTTP_GET,  .handler = audio_get },
        { .uri = "/audio/probe", .method = HTTP_POST, .handler = audio_probe_post },
    };
    for (size_t i = 0; i < sizeof uris / sizeof uris[0]; i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    web_register(server);
}

/* -------------------------------------------------------------- brightness */

/* Brightness from the settings, dimmed inside the night window once the clock is set. */
static void apply_brightness(void)
{
    tg_settings_t s;
    settings_get(&s);
    int pct = settings_is_night(&s, netdata_time_valid()) ? s.night_brightness : s.brightness;
    uint8_t duty = (uint8_t)(pct * 255 / 100);
    if (tg_display_ok() && duty != tg_backlight_get()) {
        tg_backlight_set(duty);
    }
}

/* ------------------------------------------------------------------- main */

void app_main(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Pentadeck %s (ESP-IDF %s), running from %s @ 0x%lx",
             FW_VERSION, esp_app_get_description()->idf_ver, run->label, (unsigned long)run->address);

    /* Only matters if the bootloader has rollback enabled; harmless otherwise. */
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(run, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
    }

    settings_init();
    s_events = xEventGroupCreate();
    wifi_start();
    xTaskCreate(fallback_task, "fallback", 4096, NULL, 5, NULL);
    http_start();
    netdata_start();

    /* Mic first (the audio panel reads it), screens last: whatever happens there,
     * Wi-Fi and the update endpoint are already up. */
    if (buzzer_start() != ESP_OK) ESP_LOGE(TAG, "buzzer start failed");
    {
        tg_settings_t cfg;
        settings_get(&cfg);
        if (cfg.buzzer_volume) buzzer_play(BZ_BOOT, cfg.buzzer_volume);     /* systems online */
    }
    if (leds_start() != ESP_OK) ESP_LOGE(TAG, "LED chain start failed");
    alerts_start();
    buttons_start();
    esp_err_t mic_err = mic_start();
    if (mic_err != ESP_OK) {
        ESP_LOGE(TAG, "mic start failed: %s", esp_err_to_name(mic_err));
    }
    if (stream_start() != ESP_OK) ESP_LOGE(TAG, "mic stream start failed");
    power_start();
    ledfx_start();
    if (tg_display_init() == ESP_OK) {
        esp_err_t ui_err = ui_start();
        if (ui_err != ESP_OK) {
            ESP_LOGE(TAG, "cockpit failed (%s): showing the test pattern", esp_err_to_name(ui_err));
            tg_display_test_pattern();
        }
        apply_brightness();
    } else {
        ESP_LOGE(TAG, "display init failed; updates over Wi-Fi still work");
    }

    uint32_t gen = settings_generation();
    for (int tick = 0;; tick++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (settings_generation() != gen) {
            gen = settings_generation();
            netdata_apply_tz();
        }
        if (!tg_display_ok()) apply_brightness();       /* otherwise the UI owns the backlight */
        if (tick % 30 == 0) {
            ESP_LOGI(TAG, "alive: up %llds, ip %s, rssi %d, heap %lu", esp_timer_get_time() / 1000000,
                     s_ip, app_rssi(), (unsigned long)esp_get_free_heap_size());
        }
    }
}
