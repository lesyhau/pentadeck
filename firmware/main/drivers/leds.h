#pragma once
/* The addressable RGB LEDs (side strips, back, buttons board): one WS2812-style chain on GPIO0,
 * as in Divoom's firmware (divoom_spi_OnelineLED_init: SPI3 MOSI = GPIO0). Which index sits where
 * is in ledfx.h. */
#include <stdint.h>
#include "esp_err.h"

#define LEDS_MAX 48          /* the chain has 45 */

esp_err_t leds_start(void);
/* Send n colours (0xRRGGBB, scaled by brightness 0..255); n <= LEDS_MAX, the rest are sent dark. */
esp_err_t leds_show(const uint32_t *rgb, int n, uint8_t brightness);
esp_err_t leds_off(void);
/* Mapping aid: LEDs in groups of ten, each group its own colour (red, green, blue, yellow, magenta,
 * cyan, white, orange, ...), the first of each group brighter. */
esp_err_t leds_test_groups(uint8_t brightness);

/* The buttons board's LEDs: one plain on/off line on GPIO44 (stock: divoom_screen_ctrl_key_pad ->
 * gpio_set_level(44, on)), driven here with PWM so it can be dimmed. GPIO44 is also UART0 RX, so the
 * serial console stops taking input; flashing over UART (ROM download mode) is unaffected. */
void leds_keypad(uint8_t pct);     /* 0..100 % */
