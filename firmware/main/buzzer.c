#include "buzzer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "buzzer";

#define BZ_GPIO    9
#define BZ_TIMER   LEDC_TIMER_2
#define BZ_CHANNEL LEDC_CHANNEL_2
#define BZ_RES     LEDC_TIMER_10_BIT        /* duty 0..1023; 512 = square wave = loudest */

/* A note: hz for ms; with to != 0 the pitch glides from hz to to. hz 0 = rest; ms 0 ends a pattern. */
typedef struct { uint16_t hz, to, ms; } note_t;

/* settings page test: a bright rising arpeggio */
static const note_t P_TEST[] = { {2093, 0, 60}, {0, 0, 20}, {2637, 0, 60}, {0, 0, 20}, {3136, 0, 60}, {0, 0, 20},
                                 {4186, 0, 150}, {0, 0, 0} };
/* something is hot: a two-tone klaxon, three swoops */
static const note_t P_WARN[] = { {2200, 3400, 140}, {3400, 2200, 140}, {2200, 3400, 140}, {3400, 2200, 140},
                                 {2200, 3400, 140}, {3400, 2200, 160}, {0, 0, 0} };
/* link lost: a power-down fall, then two low blips */
static const note_t P_DOWN[] = { {3400, 900, 380}, {0, 0, 70}, {1000, 0, 70}, {0, 0, 50}, {760, 0, 120}, {0, 0, 0} };
/* link restored: a power-up rise and a bright chirp */
static const note_t P_UP[]   = { {900, 3200, 260}, {0, 0, 40}, {3200, 0, 50}, {0, 0, 25}, {4186, 0, 100}, {0, 0, 0} };
/* mains lost: red alert, three slow sirens */
static const note_t P_POWER[] = { {1400, 2700, 320}, {0, 0, 90}, {1400, 2700, 320}, {0, 0, 90}, {1400, 2700, 380},
                                  {0, 0, 0} };
static const note_t P_KEY[]    = { {4186, 0, 12}, {0, 0, 0} };
static const note_t P_TICK[]   = { {2093, 0, 14}, {0, 0, 0} };
static const note_t P_ACCENT[] = { {3136, 0, 20}, {0, 0, 0} };
/* the timer ran out: a short wake-up melody (played again every 1.3 s while it rings) */
static const note_t P_ALARM[]  = { {2637, 0, 110}, {0, 0, 30}, {3136, 0, 110}, {0, 0, 30}, {3951, 0, 110}, {0, 0, 30},
                                   {4186, 0, 210}, {0, 0, 90}, {3951, 0, 90}, {0, 0, 20}, {4186, 0, 260}, {0, 0, 0} };
static const note_t P_MODE[]   = { {2600, 4200, 45}, {0, 0, 0} };
/* battery low: a falling three-note phrase */
static const note_t P_BATTERY[] = { {3136, 0, 100}, {0, 0, 40}, {2637, 0, 100}, {0, 0, 40}, {2093, 0, 260}, {0, 0, 0} };
/* systems online: a swoop up, then a quick arpeggio */
static const note_t P_BOOT[]   = { {600, 2400, 220}, {0, 0, 40}, {2093, 0, 50}, {2637, 0, 50}, {3136, 0, 50},
                                   {4186, 0, 130}, {0, 0, 0} };
/* on the hour: two soft rising notes */
static const note_t P_CHIME[]  = { {3136, 0, 70}, {0, 0, 60}, {4186, 0, 140}, {0, 0, 0} };
static const note_t *const PATTERNS[] = { P_TEST, P_WARN, P_DOWN, P_UP, P_POWER, P_KEY, P_TICK, P_ACCENT, P_ALARM,
                                          P_MODE, P_BATTERY, P_BOOT, P_CHIME };
#define TONE 0xFF                              /* job.pattern for buzzer_tone() */

typedef struct { uint8_t pattern, volume; uint16_t hz, ms; } job_t;
static QueueHandle_t s_q;

static void tone(uint16_t hz, uint32_t duty)
{
    if (hz == 0 || duty == 0) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, BZ_CHANNEL, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, BZ_CHANNEL);
        return;
    }
    ledc_set_freq(LEDC_LOW_SPEED_MODE, BZ_TIMER, hz);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BZ_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BZ_CHANNEL);
}

/* Note lengths need millisecond precision, finer than the FreeRTOS tick (10 ms here: a 9 ms click
 * rounded down to no wait at all, and the glides' 5 ms steps ran instantly). So the task waits on a
 * one-shot esp_timer that wakes it. */
static TaskHandle_t s_task;
static esp_timer_handle_t s_wake;

static void wake_cb(void *arg)
{
    xTaskNotifyGive(s_task);
}

static void wait_ms(uint32_t ms)
{
    if (!ms) return;
    if (!s_wake || esp_timer_start_once(s_wake, (uint64_t)ms * 1000) != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1);
        return;
    }
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

static void buzzer_task(void *arg)
{
    job_t j;
    for (;;) {
        if (xQueueReceive(s_q, &j, portMAX_DELAY) != pdTRUE) continue;
        /* loudness follows the duty cycle; square it so the slider feels even */
        uint32_t duty = (uint32_t)(512.0f * (j.volume / 100.0f) * (j.volume / 100.0f));
        if (duty < 4) duty = 4;
        note_t one[2] = { { j.hz, 0, j.ms }, { 0, 0, 0 } };
        const note_t *pat = j.pattern == TONE ? one : PATTERNS[j.pattern];
        uint32_t total = 0;
        for (const note_t *n = pat; n->ms; n++) {
            if (n->hz && n->to) {
                /* a glide: a new pitch every 5 ms */
                for (uint32_t t = 0; t < n->ms; t += 4) {
                    tone((uint16_t)(n->hz + ((int)n->to - (int)n->hz) * (int)t / (int)n->ms), duty);
                    wait_ms(n->ms - t < 4 ? n->ms - t : 4);
                }
            } else {
                tone(n->hz, duty);
                wait_ms(n->ms);
            }
            total += n->ms;
        }
        tone(0, 0);
        /* a pause between alerts so they stay apart; clicks (metronome, keys) need none */
        wait_ms(total > 60 ? 150 : 5);
    }
}

esp_err_t buzzer_start(void)
{
    const ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = BZ_RES, .timer_num = BZ_TIMER,
        .freq_hz = 2000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&t), TAG, "timer");
    const ledc_channel_config_t c = {
        .gpio_num = BZ_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = BZ_CHANNEL,
        .timer_sel = BZ_TIMER, .duty = 0, .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&c), TAG, "channel");
    s_q = xQueueCreate(6, sizeof(job_t));
    const esp_timer_create_args_t w = { .callback = wake_cb, .name = "buzzer" };
    if (esp_timer_create(&w, &s_wake) != ESP_OK) s_wake = NULL;
    xTaskCreate(buzzer_task, "buzzer", 2560, NULL, 4, &s_task);
    return ESP_OK;
}

void buzzer_play(buzzer_pattern_t p, int volume)
{
    if (!s_q || volume <= 0 || p < 0 || p >= (int)(sizeof PATTERNS / sizeof PATTERNS[0])) return;
    job_t j = { (uint8_t)p, (uint8_t)(volume > 100 ? 100 : volume), 0, 0 };
    xQueueSend(s_q, &j, 0);
}

void buzzer_tone(int hz, int ms, int volume)
{
    if (!s_q || volume <= 0 || hz < 50 || hz > 8000 || ms <= 0) return;
    job_t j = { TONE, (uint8_t)(volume > 100 ? 100 : volume), (uint16_t)hz, (uint16_t)(ms > 5000 ? 5000 : ms) };
    xQueueSend(s_q, &j, 0);
}
