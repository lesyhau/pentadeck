#include "leds.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "leds";

#define LED_GPIO 0
#define RES_HZ   10000000            /* 0.1 us per RMT tick */

static rmt_channel_handle_t s_ch;
static rmt_encoder_handle_t s_enc;
static SemaphoreHandle_t s_lock;
static uint8_t s_buf[LEDS_MAX * 3];

#define KEY_GPIO    44
#define KEY_TIMER   LEDC_TIMER_3
#define KEY_CHANNEL LEDC_CHANNEL_3
static int s_key_pct = -1;

static esp_err_t keypad_init(void)
{
    const ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT, .timer_num = KEY_TIMER,
        .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&t), TAG, "key timer");
    const ledc_channel_config_t c = {
        .gpio_num = KEY_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = KEY_CHANNEL,
        .timer_sel = KEY_TIMER, .duty = 0, .hpoint = 0,
    };
    return ledc_channel_config(&c);
}

void leds_keypad(uint8_t pct)
{
    if (pct > 100) pct = 100;
    if (pct == s_key_pct) return;
    s_key_pct = pct;
    uint32_t duty = (uint32_t)(1023.0f * (pct / 100.0f) * (pct / 100.0f));   /* perceptual */
    ledc_set_duty(LEDC_LOW_SPEED_MODE, KEY_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, KEY_CHANNEL);
}

esp_err_t leds_start(void)
{
    if (keypad_init() != ESP_OK) ESP_LOGE(TAG, "keypad LEDs init failed");
    s_lock = xSemaphoreCreateMutex();
    /* DMA: the whole 45-LED frame (1080 bits) streams without the CPU refilling RMT memory. With
     * the plain 64-symbol buffer, a refill delayed by Wi-Fi or the screens' DMA stretched a bit and
     * garbled the end of the chain (the right strip). */
    rmt_tx_channel_config_t cc = {
        .gpio_num = LED_GPIO, .clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = RES_HZ,
        .mem_block_symbols = 1024, .trans_queue_depth = 2, .flags.with_dma = true,
    };
    if (rmt_new_tx_channel(&cc, &s_ch) != ESP_OK) {
        ESP_LOGW(TAG, "no RMT DMA, falling back to the largest plain buffer");
        cc.flags.with_dma = false;
        cc.mem_block_symbols = 192;
        ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&cc, &s_ch), TAG, "channel");
    }
    /* WS2812: 0 = 0.3 us high / 0.9 us low, 1 = 0.9 us high / 0.3 us low, MSB first */
    const rmt_bytes_encoder_config_t ec = {
        .bit0 = { .level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9 },
        .bit1 = { .level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3 },
        .flags.msb_first = 1,
    };
    ESP_RETURN_ON_ERROR(rmt_new_bytes_encoder(&ec, &s_enc), TAG, "encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(s_ch), TAG, "enable");
    gpio_set_drive_capability(LED_GPIO, GPIO_DRIVE_CAP_1);
    return leds_off();
}

esp_err_t leds_show(const uint32_t *rgb, int n, uint8_t brightness)
{
    if (!s_ch) return ESP_ERR_INVALID_STATE;
    if (n > LEDS_MAX) n = LEDS_MAX;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_buf, 0, sizeof s_buf);
    for (int i = 0; i < n; i++) {
        uint32_t c = rgb[i];
        uint8_t r = (c >> 16 & 0xFF) * brightness / 255, g = (c >> 8 & 0xFF) * brightness / 255,
                b = (c & 0xFF) * brightness / 255;
        s_buf[i * 3 + 0] = g;                       /* WS2812 order: G, R, B */
        s_buf[i * 3 + 1] = r;
        s_buf[i * 3 + 2] = b;
    }
    const rmt_transmit_config_t tc = { .loop_count = 0 };
    esp_err_t err = rmt_transmit(s_ch, s_enc, s_buf, sizeof s_buf, &tc);
    if (err == ESP_OK) err = rmt_tx_wait_all_done(s_ch, 100);
    esp_rom_delay_us(300);                          /* latch */
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t leds_off(void)
{
    return leds_show(NULL, 0, 0);
}

esp_err_t leds_test_groups(uint8_t brightness)
{
    static const uint32_t pal[] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF,
                                    0xFFFFFF, 0xFF6000, 0x8000FF, 0x00FF80, 0xFF0080, 0x80FF00, 0x0080FF };
    uint32_t c[LEDS_MAX];
    for (int i = 0; i < LEDS_MAX; i++) {
        uint32_t base = pal[(i / 10) % (sizeof pal / sizeof pal[0])];
        if (i % 10 != 0) {                          /* the group's first LED at full, the rest at 1/4 */
            base = ((base >> 16 & 0xFF) / 4) << 16 | ((base >> 8 & 0xFF) / 4) << 8 | (base & 0xFF) / 4;
        }
        c[i] = base;
    }
    return leds_show(c, LEDS_MAX, brightness);
}
