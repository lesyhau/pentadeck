#pragma once
/*
 * The five 1.44" 128x128 ST7735S panels of the Times Gate.
 * Hardware (read from Divoom's stock firmware, see docs/HARDWARE.md):
 *   i80 8-bit bus  D0..D7 = GPIO 5,16,4,15,7,6,2,1   WR = GPIO38   DC = GPIO42   12 MHz
 *   CS per panel   GPIO 40,46,10,18,17               RESET (shared) = GPIO41
 *   backlight      GPIO39, LEDC PWM 5 kHz 8-bit (255 = full)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define TG_SCREENS 5
#define TG_W       128
#define TG_H       128
#define TG_FRAME_BYTES (TG_W * TG_H * 2)

/* RGB888 -> RGB565 */
#define TG_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

esp_err_t tg_display_init(void);
bool tg_display_ok(void);

/* Exclusive access to the shared full-frame buffer (TG_W*TG_H pixels, big-endian RGB565 as sent to the panel). */
uint16_t *tg_display_lock(void);
void tg_display_unlock(void);

/* Send the whole locked buffer to one screen (caller holds the lock). */
esp_err_t tg_display_show(int screen);

/* Send the first w*h pixels of the locked buffer to the rectangle (x, y, w, h) of one screen. */
esp_err_t tg_display_show_rect(int screen, int x, int y, int w, int h);

/* Convenience helpers (take the lock themselves). */
esp_err_t tg_display_fill(int screen, uint16_t rgb565);
esp_err_t tg_display_test_pattern(void);

/* Fill a rectangle of a frame buffer (TG_W x TG_H, big-endian RGB565), clipped to the screen. */
void tg_fb_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t rgb565);

/* Send one raw ST7735 command (with optional parameters) to one screen. */
esp_err_t tg_display_cmd(int screen, uint8_t cmd, const uint8_t *params, size_t len);

void tg_backlight_set(uint8_t duty);
uint8_t tg_backlight_get(void);
