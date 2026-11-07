#include "drivers/display.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"

static const char *TAG = "display";

/* ---- pins and timing (from the stock firmware) ---- */
static const int DATA_PINS[8] = { 5, 16, 4, 15, 7, 6, 2, 1 };
static const int CS_PINS[TG_SCREENS] = { 40, 46, 10, 18, 17 };
#define PIN_WR        38
#define PIN_DC        42
#define PIN_RESET     41
#define PIN_BACKLIGHT 39
#define PCLK_HZ       (12 * 1000 * 1000)
#define GAP_X         2
#define GAP_Y         3

#define BL_TIMER   LEDC_TIMER_1
#define BL_CHANNEL LEDC_CHANNEL_1

/* The bus has ONE panel IO with no CS of its own; we drive the five CS lines ourselves.
 * (ESP-IDF's per-device CS handling only *releases* the previous CS when switching devices, relying
 *  on a pull-up. GPIO46 is a strapping pin with a pull-down, so screen 1 stayed selected and got
 *  every frame. Divoom's firmware drives the CS lines by hand too.) */
#define SEL_NONE (-1)
#define SEL_ALL  (TG_SCREENS)

static esp_lcd_i80_bus_handle_t s_bus;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_done;   /* given by the DMA-done callback */
static SemaphoreHandle_t s_lock;   /* one user of the frame buffer at a time */
static uint16_t *s_buf;            /* TG_W*TG_H, DMA capable, big-endian RGB565 */
static bool s_ok;
static uint8_t s_backlight;

static bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

/* ---- chip selects ---- */

static void select_screen(int which)   /* 0..4, SEL_ALL or SEL_NONE; CS is active low */
{
    for (int i = 0; i < TG_SCREENS; i++) {
        gpio_set_level(CS_PINS[i], (which == SEL_ALL || which == i) ? 0 : 1);
    }
}

/* ---- ST7735 commands (to whatever is currently selected) ---- */

static esp_err_t cmd(int unused, int c, const uint8_t *data, size_t len)
{
    (void)unused;
    return esp_lcd_panel_io_tx_param(s_io, c, data, len);
}

#define CMD(s, c, ...)                                  \
    do {                                                \
        const uint8_t d_[] = { __VA_ARGS__ };           \
        ESP_RETURN_ON_ERROR_LITE(cmd((s), (c), d_, sizeof d_)); \
    } while (0)

#define ESP_RETURN_ON_ERROR_LITE(x)        \
    do {                                   \
        esp_err_t e_ = (x);                \
        if (e_ != ESP_OK) return e_;       \
    } while (0)

/* Divoom's ST7735S start-up sequence, byte for byte (esp_lcd_panel_st7735.c in the stock app),
 * followed by INVON (the stock code calls esp_lcd_panel_invert_color(panel, true)). */
static esp_err_t panel_init(int s)
{
    ESP_RETURN_ON_ERROR_LITE(cmd(s, 0x11, NULL, 0));   /* sleep out */
    vTaskDelay(pdMS_TO_TICKS(120));
    CMD(s, 0xB1, 0x05, 0x3C, 0x3C);                    /* frame rate: normal */
    CMD(s, 0xB2, 0x05, 0x3C, 0x3C);                    /* frame rate: idle */
    CMD(s, 0xB3, 0x05, 0x3C, 0x3C, 0x05, 0x3C, 0x3C);  /* frame rate: partial */
    CMD(s, 0xB4, 0x03);                                /* inversion control */
    CMD(s, 0xC0, 0xA7, 0x07);                          /* power control 1..5 */
    CMD(s, 0xC1, 0xE9);
    CMD(s, 0xC2, 0x0D, 0x00);
    CMD(s, 0xC3, 0x8A, 0x26);
    CMD(s, 0xC4, 0x8A, 0xEA);
    CMD(s, 0xC5, 0x01);                                /* VCOM */
    CMD(s, 0x36, 0xC8);                                /* MADCTL: MY | MX | BGR */
    CMD(s, 0xE0, 0x07, 0x0E, 0x00, 0x1C, 0x20, 0x17, 0x0F, 0x13,
                 0x11, 0x12, 0x12, 0x38, 0x00, 0x09, 0x07, 0x10);   /* gamma + */
    CMD(s, 0xE1, 0x0E, 0x0C, 0x07, 0x0E, 0x13, 0x0A, 0x02, 0x06,
                 0x05, 0x09, 0x1B, 0x2F, 0x00, 0x0A, 0x04, 0x10);   /* gamma - */
    CMD(s, 0x3A, 0x55);                                /* 16 bits per pixel */
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR_LITE(cmd(s, 0x29, NULL, 0));   /* display on */
    ESP_RETURN_ON_ERROR_LITE(cmd(s, 0x21, NULL, 0));   /* inversion on */
    return ESP_OK;
}

static esp_err_t set_window(int s, int x0, int y0, int x1, int y1)   /* inclusive */
{
    x0 += GAP_X; x1 += GAP_X; y0 += GAP_Y; y1 += GAP_Y;
    CMD(s, 0x2A, x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF);
    CMD(s, 0x2B, y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF);
    return ESP_OK;
}

/* ---- backlight ---- */

void tg_backlight_set(uint8_t duty)
{
    s_backlight = duty;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}

uint8_t tg_backlight_get(void)
{
    return s_backlight;
}

static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = BL_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR_LITE(ledc_timer_config(&timer));
    const ledc_channel_config_t ch = {
        .gpio_num = PIN_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_CHANNEL,
        .timer_sel = BL_TIMER,
        .duty = 0,          /* dark until the panels show something sensible */
        .hpoint = 0,
    };
    return ledc_channel_config(&ch);
}

/* ---- public ---- */

esp_err_t tg_display_init(void)
{
    s_done = xSemaphoreCreateBinary();
    s_lock = xSemaphoreCreateMutex();
    s_buf = heap_caps_malloc(TG_FRAME_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_done || !s_lock || !s_buf) {
        ESP_LOGE(TAG, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR_LITE(backlight_init());

    /* shared hardware reset */
    const gpio_config_t rst = { .pin_bit_mask = 1ULL << PIN_RESET, .mode = GPIO_MODE_OUTPUT };
    ESP_RETURN_ON_ERROR_LITE(gpio_config(&rst));
    gpio_set_level(PIN_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    esp_lcd_i80_bus_config_t bus = {
        .dc_gpio_num = PIN_DC,
        .wr_gpio_num = PIN_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .bus_width = 8,
        .max_transfer_bytes = TG_FRAME_BYTES,
    };
    for (int i = 0; i < 8; i++) {
        bus.data_gpio_nums[i] = DATA_PINS[i];
    }
    ESP_RETURN_ON_ERROR_LITE(esp_lcd_new_i80_bus(&bus, &s_bus));

    const esp_lcd_panel_io_i80_config_t io = {
        .cs_gpio_num = -1,   /* CS lines are ours */
        .pclk_hz = PCLK_HZ,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_color_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_levels = { .dc_idle_level = 0, .dc_cmd_level = 0, .dc_dummy_level = 0, .dc_data_level = 1 },
    };
    ESP_RETURN_ON_ERROR_LITE(esp_lcd_new_panel_io_i80(s_bus, &io, &s_io));

    /* CS lines: plain push-pull outputs, all deselected (high) */
    gpio_config_t cs = { .mode = GPIO_MODE_OUTPUT };
    for (int i = 0; i < TG_SCREENS; i++) {
        cs.pin_bit_mask |= 1ULL << CS_PINS[i];
        gpio_set_level(CS_PINS[i], 1);
    }
    ESP_RETURN_ON_ERROR_LITE(gpio_config(&cs));
    select_screen(SEL_NONE);

    /* Initialise all five panels at once (all CS low), like the stock firmware. */
    select_screen(SEL_ALL);
    esp_err_t err = panel_init(0);
    select_screen(SEL_NONE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_ok = true;
    ESP_LOGI(TAG, "5 panels initialised");
    return ESP_OK;
}

bool tg_display_ok(void)
{
    return s_ok;
}

uint16_t *tg_display_lock(void)
{
    if (!s_ok) {
        return NULL;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    return s_buf;
}

void tg_display_unlock(void)
{
    xSemaphoreGive(s_lock);
}

esp_err_t tg_display_show(int screen)
{
    return tg_display_show_rect(screen, 0, 0, TG_W, TG_H);
}

esp_err_t tg_display_show_rect(int screen, int x, int y, int w, int h)
{
    if (!s_ok || screen < 0 || screen >= TG_SCREENS || w <= 0 || h <= 0 || x < 0 || y < 0 ||
        x + w > TG_W || y + h > TG_H) {
        return ESP_ERR_INVALID_ARG;
    }
    select_screen(screen);
    esp_err_t err = set_window(screen, x, y, x + w - 1, y + h - 1);
    if (err == ESP_OK) {
        xSemaphoreTake(s_done, 0);   /* clear a stale signal */
        err = esp_lcd_panel_io_tx_color(s_io, 0x2C, s_buf, (size_t)w * h * 2);
    }
    /* wait for the DMA to finish before deselecting, reusing the buffer or switching screen */
    if (err == ESP_OK && xSemaphoreTake(s_done, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGW(TAG, "screen %d: transfer timeout", screen);
        err = ESP_ERR_TIMEOUT;
    }
    select_screen(SEL_NONE);
    return err;
}

esp_err_t tg_display_cmd(int screen, uint8_t c, const uint8_t *params, size_t len)
{
    if (!s_ok || screen < 0 || screen >= TG_SCREENS) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    select_screen(screen);
    esp_err_t err = cmd(screen, c, len ? params : NULL, len);
    select_screen(SEL_NONE);
    xSemaphoreGive(s_lock);
    return err;
}

static inline uint16_t be(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}

static void rect(uint16_t *fb, int x, int y, int w, int h, uint16_t c)
{
    uint16_t v = be(c);
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            if (i >= 0 && i < TG_W && j >= 0 && j < TG_H) {
                fb[j * TG_W + i] = v;
            }
        }
    }
}

void tg_fb_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t rgb565)
{
    rect(fb, x, y, w, h, rgb565);
}

esp_err_t tg_display_fill(int screen, uint16_t rgb565)
{
    uint16_t *fb = tg_display_lock();
    if (!fb) {
        return ESP_ERR_INVALID_STATE;
    }
    rect(fb, 0, 0, TG_W, TG_H, rgb565);
    esp_err_t err = tg_display_show(screen);
    tg_display_unlock();
    return err;
}

/* Screen n: its own colour, a white frame, a black square in the TOP-LEFT corner (orientation),
 * and n+1 white blocks along the bottom (which screen is which). */
esp_err_t tg_display_test_pattern(void)
{
    static const uint16_t colours[TG_SCREENS] = {
        TG_RGB(255, 0, 0), TG_RGB(0, 255, 0), TG_RGB(0, 0, 255), TG_RGB(255, 255, 0), TG_RGB(255, 0, 255),
    };
    esp_err_t first_err = ESP_OK;
    for (int s = 0; s < TG_SCREENS; s++) {
        uint16_t *fb = tg_display_lock();
        if (!fb) {
            return ESP_ERR_INVALID_STATE;
        }
        rect(fb, 0, 0, TG_W, TG_H, TG_RGB(255, 255, 255));
        rect(fb, 4, 4, TG_W - 8, TG_H - 8, colours[s]);
        rect(fb, 10, 10, 24, 24, 0);
        for (int k = 0; k <= s; k++) {
            rect(fb, 14 + k * 21, 96, 14, 14, TG_RGB(255, 255, 255));
        }
        esp_err_t err = tg_display_show(s);
        tg_display_unlock();
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }
    return first_err;
}
