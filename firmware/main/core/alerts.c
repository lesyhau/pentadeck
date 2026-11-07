#include "core/alerts.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "core/settings.h"
#include "drivers/buzzer.h"
#include "net/netdata.h"

static const char *TAG = "alerts";

#define HYST_C        5            /* a temperature alert re-arms 5 °C below its threshold */
#define REMIND_S      600          /* while still hot, remind every 10 minutes */
#define OFFLINE_S     20           /* the home lab must be gone this long before it counts as down */

typedef struct { bool hot; int64_t last; } temp_alarm_t;

static struct {
    temp_alarm_t pc_cpu, pc_gpu, pc_vram, lab_cpu;
    bool lab_seen, lab_down;
    int64_t lab_bad_since;
    bool svc_known;
    bool svc_up[HOST_MAX_SERVICES];
    bool ac_known, ac, bat_low;
} A;

static int64_t now_s(void)
{
    return (int64_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
}

/* true when this reading should beep */
static bool temp_check(temp_alarm_t *a, float t, int limit, int64_t now)
{
    if (isnan(t)) return false;
    if (!a->hot && t >= limit) {
        a->hot = true;
        a->last = now;
        return true;
    }
    if (a->hot && t < limit - HYST_C) a->hot = false;
    if (a->hot && now - a->last >= REMIND_S) {
        a->last = now;
        return true;
    }
    return false;
}

static void alerts_task(void *arg)
{
    host_stats_t pc, lab;
    tg_settings_t s;
    vTaskDelay(pdMS_TO_TICKS(15000));          /* let the first polls land before judging anything */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        settings_get(&s);
        netdata_get_host(0, &pc);
        netdata_get_host(1, &lab);
        int64_t now = now_s();
        int beep = -1;                         /* the most important pattern this second */
#define RAISE(p) do { if (beep < 0 || (p) == BZ_DOWN || (p) == BZ_POWER || (p) == BZ_BATTERY) beep = (p); } while (0)

        /* temperatures (only from live data) */
        if (pc.ok) {
            if (temp_check(&A.pc_cpu, pc.cpu_temp, s.alert_cpu_temp, now)) RAISE(BZ_WARN);
            if (pc.has_gpu) {
                if (temp_check(&A.pc_gpu, pc.gpu.temp, s.alert_gpu_temp, now)) RAISE(BZ_WARN);
                if (temp_check(&A.pc_vram, pc.gpu.vram_temp, s.alert_vram_temp, now)) RAISE(BZ_WARN);
            }
        }
        if (lab.ok && temp_check(&A.lab_cpu, lab.cpu_temp, s.alert_cpu_temp, now)) RAISE(BZ_WARN);

        /* the home lab going away and coming back (the PC is switched off routinely: no alert) */
        if (lab.ok) {
            if (A.lab_down) {
                A.lab_down = false;
                RAISE(BZ_UP);
                ESP_LOGI(TAG, "home lab back");
            }
            A.lab_seen = true;
            A.lab_bad_since = 0;
        } else if (A.lab_seen && !A.lab_down) {
            if (!A.lab_bad_since) A.lab_bad_since = now;
            if (now - A.lab_bad_since >= OFFLINE_S) {
                A.lab_down = true;
                RAISE(BZ_DOWN);
                ESP_LOGW(TAG, "home lab down");
            }
        }

        if (lab.ok) {
            /* services: a beep when one goes down, a chirp when it's back */
            if (A.svc_known) {
                for (int i = 0; i < lab.n_services && i < HOST_MAX_SERVICES; i++) {
                    if (A.svc_up[i] && !lab.services[i].up) { RAISE(BZ_DOWN); ESP_LOGW(TAG, "%s down", lab.services[i].name); }
                    if (!A.svc_up[i] && lab.services[i].up) RAISE(BZ_UP);
                }
            }
            for (int i = 0; i < HOST_MAX_SERVICES; i++) A.svc_up[i] = i < lab.n_services && lab.services[i].up;
            A.svc_known = true;

            /* mains power and battery */
            if (lab.has_battery) {
                if (A.ac_known && A.ac && !lab.ac) { RAISE(BZ_POWER); ESP_LOGW(TAG, "home lab on battery"); }
                if (A.ac_known && !A.ac && lab.ac) RAISE(BZ_UP);
                A.ac = lab.ac;
                A.ac_known = true;
                bool low = !lab.ac && lab.bat_pct <= 20;
                if (low && !A.bat_low) RAISE(BZ_BATTERY);
                A.bat_low = low ? true : (lab.bat_pct > 25 ? false : A.bat_low);
            }
        }
#undef RAISE
        if (beep >= 0 && s.buzzer && !(s.quiet_night && settings_is_night(&s, netdata_time_valid()))) {
            buzzer_play((buzzer_pattern_t)beep, s.buzzer_volume);
        }
    }
}

void alerts_start(void)
{
    memset(&A, 0, sizeof A);
    xTaskCreate(alerts_task, "alerts", 4096, NULL, 3, NULL);
}
