#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* The on-device cockpit: five 128x128 screens drawn by hud.c. Call after tg_display_init(). */
esp_err_t ui_start(void);

/* Render statistics for the last second as a JSON object; returns its length. */
int ui_stats_json(char *buf, int n);

/* Render what screen `screen` (0..4) shows right now into `out` (128x128 big-endian RGB565), for
 * screenshots. Safe to call from another task. */
esp_err_t ui_capture(int screen, uint16_t *out);

/* Your own image on the clock screen, in place of the spinning logo: UI_EMBLEM x UI_EMBLEM pixels,
 * big-endian RGB565, kept in NVS. NULL clears it (the logo comes back). Safe from another task. */
#define UI_EMBLEM 36
esp_err_t ui_emblem_set(const uint8_t *px, size_t len);
bool ui_emblem_get(uint8_t *out);           /* false: the logo shows */
