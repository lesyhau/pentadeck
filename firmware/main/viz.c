/* Mic test view on one screen: 16-band spectrum with peak hold, loudness bar at the bottom. */
#include "viz.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#include "mic.h"

#define DB_FLOOR  (-90.0f)
#define DB_TOP    (-25.0f)
#define BAR_BASE  104
#define BAR_MAX   96

static int s_screen = 4;
static uint16_t s_fb[TG_W * TG_H];

static uint16_t level_colour(float f)   /* 0..1: teal -> amber -> red */
{
    if (f < 0.6f) return TG_RGB(34, 211, 238);
    if (f < 0.85f) return TG_RGB(245, 158, 11);
    return TG_RGB(239, 68, 68);
}

static float clamp01(float v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

static void viz_task(void *arg)
{
    float shown[MIC_BANDS] = { 0 }, peak[MIC_BANDS] = { 0 }, level = 0;
    for (;;) {
        mic_snapshot_t m;
        mic_get(&m);
        tg_fb_rect(s_fb, 0, 0, TG_W, TG_H, 0);
        for (int b = 0; b < MIC_BANDS; b++) {
            float f = clamp01((m.bands[b] - DB_FLOOR) / (DB_TOP - DB_FLOOR));
            shown[b] = f > shown[b] ? f : shown[b] - 0.035f;          /* fast attack, slow release */
            if (shown[b] < 0) shown[b] = 0;
            peak[b] = shown[b] > peak[b] ? shown[b] : peak[b] - 0.008f;
            int h = (int)(shown[b] * BAR_MAX);
            int x = b * 8 + 1;
            tg_fb_rect(s_fb, x, BAR_BASE - h, 6, h, level_colour(shown[b]));
            int py = BAR_BASE - (int)(peak[b] * BAR_MAX) - 2;
            tg_fb_rect(s_fb, x, py, 6, 2, TG_RGB(255, 255, 255));
        }
        float lf = clamp01((m.dbfs + 80.0f) / 75.0f);
        level = lf > level ? lf : level - 0.02f;
        if (level < 0) level = 0;
        tg_fb_rect(s_fb, 2, 112, 124, 12, TG_RGB(30, 41, 59));
        tg_fb_rect(s_fb, 2, 112, (int)(124 * level), 12, level_colour(level));

        uint16_t *dst = tg_display_lock();
        if (dst) {
            memcpy(dst, s_fb, TG_FRAME_BYTES);
            tg_display_show(s_screen);
            tg_display_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(40));   /* about 20 fps */
    }
}

void viz_start(int screen)
{
    s_screen = screen;
    xTaskCreatePinnedToCore(viz_task, "viz", 4096, NULL, 4, NULL, 1);
}
