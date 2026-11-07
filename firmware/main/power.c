#include "power.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "mic.h"
#include "netdata.h"
#include "settings.h"
#include "stream.h"

static const char *TAG = "power";

static volatile bool s_idle;
static uint32_t s_last_ms;              /* esp_timer ms of the last sign of someone */
static const char *s_reason = "boot";
static bool s_force;                    /* power_sleep_now(): idle until a newer sign */
static uint32_t s_force_at;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_cpu_lock;   /* held at full power: the CPU stays at 240 MHz */
#endif

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* A sign of someone at esp_timer ms `at` (only newer signs count). */
static void note(uint32_t at, const char *why)
{
    portENTER_CRITICAL(&s_mux);
    if ((int32_t)(at - s_last_ms) > 0) {
        s_last_ms = at;
        s_reason = why;
        if (s_force && (int32_t)(at - s_force_at) > 2000) s_force = false;   /* not what was in flight */
    }
    portEXIT_CRITICAL(&s_mux);
}

void power_wake(const char *why)
{
    note(now_ms(), why);
    portENTER_CRITICAL(&s_mux);
    s_force = false;                  /* a button or the web always wakes */
    portEXIT_CRITICAL(&s_mux);
    if (s_task) xTaskNotifyGive(s_task);
}

void power_sleep_now(void)
{
    portENTER_CRITICAL(&s_mux);
    s_force = true;
    s_force_at = now_ms();
    portEXIT_CRITICAL(&s_mux);
    if (s_task) xTaskNotifyGive(s_task);
}

bool power_idle(void)
{
    return s_idle;
}

const char *power_last_reason(void)
{
    return s_reason;
}

uint32_t power_quiet_s(void)
{
    return (now_ms() - s_last_ms) / 1000;
}

static void apply(bool idle)
{
    s_idle = idle;
    mic_low_power(idle);
#if CONFIG_PM_ENABLE
    static bool held = true;
    bool hold = !idle;
    if (hold != held) {
        if (hold) esp_pm_lock_acquire(s_cpu_lock);
        else esp_pm_lock_release(s_cpu_lock);
        held = hold;
    }
#endif
    ESP_LOGI(TAG, "%s (last sign: %s, %lu s ago)", idle ? "low power" : "full power", s_reason,
             (unsigned long)power_quiet_s());
}

static void power_task(void *arg)
{
    int ps = -1;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
        uint32_t now = now_ms();

        uint32_t heard = mic_presence_ms();
        if (heard) note(heard, "sound");
        for (int i = 0; i < TG_SOURCES; i++) {          /* keyboard or mouse on a PC with the agent */
            host_stats_t h;
            netdata_get_host(i, &h);
            if (!h.ok || h.input_idle_s < 0) continue;
            int64_t at = (int64_t)now - h.age_ms - (int64_t)h.input_idle_s * 1000;
            if (at > 0) note((uint32_t)at, "pc input");
        }

        tg_settings_t s;
        settings_get(&s);
        portENTER_CRITICAL(&s_mux);
        uint32_t quiet = now - s_last_ms;
        bool idle = s_force || (s.idle_min && quiet >= (uint32_t)s.idle_min * 60000u);
        portEXIT_CRITICAL(&s_mux);
        if (idle != s_idle) apply(idle);

        /* modem sleep only while idle, and not while a PC streams the mic (it would stutter) */
        int want = idle && !stream_listeners() ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE;
        if (want != ps && esp_wifi_set_ps((wifi_ps_type_t)want) == ESP_OK) ps = want;
    }
}

void power_start(void)
{
    s_last_ms = now_ms();
#if CONFIG_PM_ENABLE
    /* the CPU may slow to 80 MHz (APB stays 80 MHz, so the LEDC, I2S and LCD clocks don't move);
     * the full-power lock keeps it at 240 MHz except while idle */
    esp_pm_config_t pm = { .max_freq_mhz = 240, .min_freq_mhz = 80, .light_sleep_enable = false };
    esp_err_t err = esp_pm_configure(&pm);
    if (err != ESP_OK) ESP_LOGW(TAG, "frequency scaling unavailable: %s", esp_err_to_name(err));
    esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "full", &s_cpu_lock);
    esp_pm_lock_acquire(s_cpu_lock);
#endif
    xTaskCreate(power_task, "power", 3072, NULL, 5, &s_task);
}
