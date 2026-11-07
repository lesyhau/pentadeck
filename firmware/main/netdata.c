#include "netdata.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"

#include "settings.h"

static const char *TAG = "netdata";

static SemaphoreHandle_t s_lock;
static host_stats_t s_host[TG_SOURCES];
static int64_t s_host_at[TG_SOURCES];     /* esp_timer time of the last good poll, us */
static weather_t s_weather;
static int64_t s_weather_at;

/* ------------------------------------------------------------------- HTTP */

static int http_get(const char *url, char *buf, int cap, int timeout_ms)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = timeout_ms,
        .crt_bundle_attach = strncmp(url, "https://", 8) == 0 ? esp_crt_bundle_attach : NULL,
        .buffer_size = 1024,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return -1;
    int n = -1;
    if (esp_http_client_open(h, 0) == ESP_OK) {
        esp_http_client_fetch_headers(h);
        if (esp_http_client_get_status_code(h) == 200) {
            int total = 0, r;
            while (total < cap - 1 && (r = esp_http_client_read(h, buf + total, cap - 1 - total)) > 0) {
                total += r;
            }
            buf[total] = 0;
            n = total;
        }
    }
    esp_http_client_cleanup(h);
    return n;
}

/* ------------------------------------------------------------------ agents */

static float num(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : NAN;
}

static uint64_t u64(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) && v->valuedouble > 0 ? (uint64_t)v->valuedouble : 0;
}

static void str(char *dst, size_t n, const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItem(o, k);
    strlcpy(dst, cJSON_IsString(v) && v->valuestring ? v->valuestring : "", n);
}

static bool parse_host(const char *json, host_stats_t *h)
{
    cJSON *o = cJSON_Parse(json);
    if (!o) return false;
    memset(h, 0, sizeof *h);
    str(h->name, sizeof h->name, o, "name");
    str(h->host, sizeof h->host, o, "host");
    str(h->os, sizeof h->os, o, "os");
    h->uptime_s = (uint32_t)u64(o, "uptime_s");
    const cJSON *cpu = cJSON_GetObjectItem(o, "cpu");
    h->cpu_load = num(cpu, "load");
    h->cpu_temp = num(cpu, "temp");
    h->cpu_hot = num(cpu, "hotspot");
    h->threads = (int)num(cpu, "threads");
    str(h->cpu_model, sizeof h->cpu_model, cpu, "model");
    str(h->temp_src, sizeof h->temp_src, cpu, "temp_src");
    const cJSON *ram = cJSON_GetObjectItem(o, "ram");
    h->ram_used = u64(ram, "used");
    h->ram_total = u64(ram, "total");
    h->ram_temp = num(ram, "temp");
    const cJSON *d;
    cJSON_ArrayForEach(d, cJSON_GetObjectItem(o, "disks")) {
        if (h->n_disks >= HOST_MAX_DISKS) break;
        str(h->disks[h->n_disks].name, sizeof h->disks[0].name, d, "name");
        h->disks[h->n_disks].used = u64(d, "used");
        h->disks[h->n_disks].total = u64(d, "total");
        h->n_disks++;
    }
    const cJSON *g = cJSON_GetObjectItem(o, "gpu");
    if (cJSON_IsObject(g)) {
        h->has_gpu = true;
        str(h->gpu.name, sizeof h->gpu.name, g, "name");
        h->gpu.load = num(g, "load");
        h->gpu.temp = num(g, "temp");
        h->gpu.hotspot = num(g, "hotspot");
        h->gpu.vram_temp = num(g, "vram_temp");
        h->gpu.vram_used = u64(g, "vram_used");
        h->gpu.vram_total = u64(g, "vram_total");
    }
    const cJSON *b = cJSON_GetObjectItem(o, "battery");
    if (cJSON_IsObject(b)) {
        h->has_battery = true;
        h->bat_pct = (int)num(b, "percent");
        h->charging = cJSON_IsTrue(cJSON_GetObjectItem(b, "charging"));
        h->ac = cJSON_IsTrue(cJSON_GetObjectItem(b, "ac"));
    }
    const cJSON *s;
    cJSON_ArrayForEach(s, cJSON_GetObjectItem(o, "services")) {
        if (h->n_services >= HOST_MAX_SERVICES) break;
        str(h->services[h->n_services].name, sizeof h->services[0].name, s, "name");
        h->services[h->n_services].up = cJSON_IsTrue(cJSON_GetObjectItem(s, "up"));
        h->services[h->n_services].ms = num(s, "ms");
        h->n_services++;
    }
    cJSON_Delete(o);
    h->ever = true;
    return true;
}

static void agents_task(void *arg)
{
    static char buf[3072];
    for (;;) {
        tg_settings_t cfg;
        settings_get(&cfg);
        int64_t t0 = esp_timer_get_time();
        for (int i = 0; i < TG_SOURCES; i++) {
            if (!cfg.src_url[i][0]) continue;
            host_stats_t h;
            if (http_get(cfg.src_url[i], buf, sizeof buf, 1500) > 0 && parse_host(buf, &h)) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_host[i] = h;
                s_host_at[i] = esp_timer_get_time();
                xSemaphoreGive(s_lock);
            }
        }
        int64_t spent_ms = (esp_timer_get_time() - t0) / 1000;
        int wait = cfg.poll_ms - (int)spent_ms;
        vTaskDelay(pdMS_TO_TICKS(wait > 50 ? wait : 50));
    }
}

/* ----------------------------------------------------------------- weather */

static void weather_task(void *arg)
{
    static char buf[2048];
    /* HTTPS wants a real clock (certificate dates): give SNTP up to 30 s first. */
    for (int i = 0; i < 30 && !netdata_time_valid(); i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    for (;;) {
        tg_settings_t cfg;
        settings_get(&cfg);
        if (cfg.lat == 0 && cfg.lon == 0) {          /* no location set yet */
            vTaskDelay(pdMS_TO_TICKS(30 * 1000));
            continue;
        }
        char url[200];
        snprintf(url, sizeof url,
                 "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
                 "&current=temperature_2m,relative_humidity_2m,weather_code,is_day&timezone=auto",
                 cfg.lat, cfg.lon);
        bool ok = false;
        if (http_get(url, buf, sizeof buf, 8000) > 0) {
            cJSON *o = cJSON_Parse(buf);
            const cJSON *cur = o ? cJSON_GetObjectItem(o, "current") : NULL;
            if (cJSON_IsObject(cur)) {
                weather_t w = { .ok = true };
                w.temp_c = num(cur, "temperature_2m");
                w.humidity = num(cur, "relative_humidity_2m");
                w.code = (int)num(cur, "weather_code");
                w.is_day = num(cur, "is_day") > 0.5f;
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_weather = w;
                s_weather_at = esp_timer_get_time();
                xSemaphoreGive(s_lock);
                ok = true;
            }
            cJSON_Delete(o);
        }
        if (!ok) ESP_LOGW(TAG, "weather fetch failed");
        vTaskDelay(pdMS_TO_TICKS(ok ? 10 * 60 * 1000 : 30 * 1000));
    }
}

/* -------------------------------------------------------------------- time */

void netdata_apply_tz(void)
{
    tg_settings_t cfg;
    settings_get(&cfg);
    setenv("TZ", cfg.tz, 1);
    tzset();
}

bool netdata_time_valid(void)
{
    return time(NULL) > 1735689600;   /* after 2025-01-01: SNTP has run */
}

/* ------------------------------------------------------------------ public */

void netdata_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    netdata_apply_tz();
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp);
    xTaskCreate(agents_task, "agents", 6144, NULL, 4, NULL);
    xTaskCreate(weather_task, "weather", 8192, NULL, 3, NULL);
}

void netdata_get_host(int i, host_stats_t *out)
{
    if (i < 0 || i >= TG_SOURCES || !s_lock) {
        memset(out, 0, sizeof *out);
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_host[i];
    int64_t at = s_host_at[i];
    xSemaphoreGive(s_lock);
    out->age_ms = at ? (esp_timer_get_time() - at) / 1000 : -1;
    out->ok = out->ever && out->age_ms >= 0 && out->age_ms < 5000;
}

void netdata_get_weather(weather_t *out)
{
    if (!s_lock) {
        memset(out, 0, sizeof *out);
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_weather;
    int64_t at = s_weather_at;
    xSemaphoreGive(s_lock);
    out->age_ms = at ? (esp_timer_get_time() - at) / 1000 : -1;
}
