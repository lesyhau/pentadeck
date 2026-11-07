#include "drivers/buttons.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "buttons";

#define SAMPLE_MS   10
#define DEBOUNCE    3              /* samples that must agree: 30 ms */
#define LONG_MS     800
#define LONG_PWR_MS 1500
#define REPEAT_AT   450
#define REPEAT_MS   110

/* ADC readings (12 bit, 12 dB) measured on the device: left ~15, LIGHT ~340, power ~695,
 * MODE ~1045, right ~1435, none 4095. The bounds sit halfway between them. */
static btn_key_t classify(int raw)
{
    if (raw < 180) return KEY_LEFT;
    if (raw < 520) return KEY_LIGHT;
    if (raw < 870) return KEY_POWER;
    if (raw < 1240) return KEY_MODE;
    if (raw < 2700) return KEY_RIGHT;
    return KEY_NONE;
}

static adc_oneshot_unit_handle_t s_adc;
static QueueHandle_t s_q;

static void send(btn_key_t k, btn_kind_t kind, bool repeat)
{
    btn_event_t e = { k, kind, repeat };
    xQueueSend(s_q, &e, 0);
}

static void buttons_task(void *arg)
{
    btn_key_t stable = KEY_NONE, cand = KEY_NONE;
    int agree = 0;
    uint32_t held_ms = 0, next_repeat = 0;
    bool long_sent = false;
    for (;;) {
        int raw = 4095;
        if (adc_oneshot_read(s_adc, ADC_CHANNEL_7, &raw) != ESP_OK) raw = 4095;
        btn_key_t k = classify(raw);
        if (k == cand) {
            if (agree < DEBOUNCE) agree++;
        } else {
            cand = k;
            agree = 1;
        }
        if (agree >= DEBOUNCE && cand != stable) {
            /* a change of key: release the old one, press the new one */
            if (stable != KEY_NONE && !long_sent && stable != KEY_LEFT && stable != KEY_RIGHT) {
                send(stable, BTN_SHORT, false);
            }
            stable = cand;
            held_ms = 0;
            long_sent = false;
            next_repeat = REPEAT_AT;
            if (stable != KEY_NONE) send(stable, BTN_DOWN, false);
        } else if (stable != KEY_NONE) {
            held_ms += SAMPLE_MS;
            bool arrows = stable == KEY_LEFT || stable == KEY_RIGHT;
            if (arrows && held_ms >= next_repeat) {
                send(stable, BTN_DOWN, true);
                next_repeat += REPEAT_MS;
            }
            uint32_t long_ms = stable == KEY_POWER ? LONG_PWR_MS : LONG_MS;
            if (!arrows && !long_sent && held_ms >= long_ms) {
                send(stable, BTN_LONG, false);
                long_sent = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    }
}

void buttons_start(void)
{
    s_q = xQueueCreate(16, sizeof(btn_event_t));
    adc_oneshot_unit_init_cfg_t u = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_chan_cfg_t cc = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    if (adc_oneshot_new_unit(&u, &s_adc) != ESP_OK || adc_oneshot_config_channel(s_adc, ADC_CHANNEL_7, &cc) != ESP_OK) {
        ESP_LOGE(TAG, "ADC init failed: buttons disabled");
        return;
    }
    xTaskCreatePinnedToCore(buttons_task, "buttons", 3072, NULL, 5, NULL, 0);
}

bool buttons_get(btn_event_t *e)
{
    return s_q && xQueueReceive(s_q, e, 0) == pdTRUE;
}

void buttons_inject(btn_key_t k, btn_kind_t kind)
{
    if (s_q && k > KEY_NONE && k < KEY_COUNT) send(k, kind, false);
}

btn_key_t buttons_key_from_name(const char *name)
{
    static const char *names[KEY_COUNT] = { "light", "left", "power", "right", "mode" };
    for (int i = 0; i < KEY_COUNT; i++) if (strcmp(name, names[i]) == 0) return (btn_key_t)i;
    return KEY_NONE;
}
