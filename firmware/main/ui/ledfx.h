#pragma once
/* What the LED chain shows. Layout (from the boards and a test, 2026-11-08): the left side board
 * 0..19, the back light 20..24, the right side board 25..44. The two side boards are alike
 * ("TimesGate LED-R"): 20 LEDs, chained in the order of their printed names LED2, 4, 6..11 (the short
 * column, on the back edge, bottom to top), LED12..22 (the long column, on the front edge, top to
 * bottom) and LED23 (bottom, middle). Meters fill the side strips by row (ledfx.c knows each LED's),
 * so both reach the same height at the same level. Modes (setting "led_mode"):
 *   off     everything dark
 *   glow    a steady teal glow
 *   status  left strip = PC CPU load, right strip = PC GPU load (both filling from the bottom,
 *           orange from 60 %, red from 80 %, blinking like the screens); back = home lab health
 *   music   all three strips = the same loudness meter: the sides fill from the bottom, the back from
 *           the middle out (no load alerts, so they always follow the sound)
 * In glow the side strips still flash orange/red on a CPU/GPU load alert.
 * The buttons board's LEDs (GPIO44, single colour) stay on at the LED brightness unless the mode is off. */
#include <stdbool.h>
#include <stdint.h>

#define LED_LEFT0   0
#define LED_BACK0   20
#define LED_RIGHT0  25
#define LED_STRIP   20             /* LEDs on each side board */
#define LED_BACK_N  5              /* LEDs in the back light */
#define LED_COUNT   45

void ledfx_start(void);
void ledfx_hold(uint32_t ms);      /* pause the effects (LED tests from the web API) */

/* What a mode other than the dashboard wants on the chain (the LED mode setting still decides
 * whether the LEDs are on at all, and how bright):
 *   NONE      the normal effect for the LED mode
 *   TUNER     a = cents off the nearest note (NAN = no note): flat lights the left strip, sharp
 *             the right, green all round when within 5 cents
 *   PROGRESS  a = 0..1 left (both strips drain from the top), b = 1 while running
 *   BEAT      a flash on every beat that fades (see ledfx_beat)
 *   ALARM     everything flashing orange */
typedef enum { SCENE_NONE, SCENE_TUNER, SCENE_PROGRESS, SCENE_BEAT, SCENE_ALARM } ledfx_scene_t;
void ledfx_scene(ledfx_scene_t scene, float a, float b);
void ledfx_beat(uint32_t at_ms, bool accent);   /* a metronome beat at esp_timer ms */
void ledfx_sleep(bool sleep);      /* everything dark, the buttons board too, until woken */

#define LED_ROWS    10             /* the side strips are filled by row, 0 = bottom (ledfx.c) */
