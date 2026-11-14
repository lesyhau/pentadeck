#pragma once
/* What the LED chain shows. Layout (from the boards and a test, 2026-11-08): the left side board
 * 0..19, the back light 20..24, the right side board 25..44. The two side boards are alike
 * ("TimesGate LED-R"): 20 LEDs, chained in the order of their printed names LED2, 4, 6..11 (the short
 * column, on the back edge, bottom to top), LED12..22 (the long column, on the front edge, top to
 * bottom) and LED23 (bottom, middle). Meters fill the side strips by row (ledfx.c knows each LED's),
 * so both reach the same height at the same level.
 * Each zone (left strip, back light, right strip) runs its own effect in its own colour (settings
 * "led_left", "led_back", "led_right" and "led_*_color", teal by default). Meters fill the sides from
 * the bottom and the back from the middle out; the moving effects run round a side board's loop of
 * LEDs and along the back from left to right:
 *   off      dark
 *   glow     steady
 *   breathe  slowly brightening and dimming
 *   wave     a band of light running round
 *   comet    a bright head with a fading tail
 *   sparkle  random LEDs flashing over a dim glow
 *   pulse    as bright as the room is loud
 *   fire     flickering embers (its own colours)
 *   rainbow  the colour wheel rolling along the zone (its own colours)
 *   cpu/gpu  the PC's CPU or GPU load as a meter; orange from 60 % load or 75 °C, red from 80 % or
 *            88 °C (the screens' thresholds); a load alert blinks like the screens
 *   lab      the home lab's health: the colour breathing, amber on battery, a red pulse when something is down
 *   music    a loudness meter from the mic, turning orange and red when loud
 *   spectrum the sound's spectrum: bass at the bottom of the side strips (the middle of the back), treble
 *            at the top (the ends), each step as bright as its part of the sound; a flash on the beat.
 *            In the visual effects' colours (settings viz_palette): the zone's own, a rainbow, or fire
 * The buttons board's LEDs (GPIO44, single colour) stay on at the LED brightness unless every zone is off. */
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

/* What a mode other than the dashboard wants on the chain (the LED settings still decide whether
 * the LEDs are on at all, and how bright):
 *   NONE      each zone's own effect
 *   TUNER     a = cents off the nearest note (NAN = no note): flat lights the left strip, sharp
 *             the right, green all round when within 5 cents
 *   PROGRESS  a = 0..1 left (both strips drain from the top), b = 1 while running
 *   BEAT      a flash on every beat that fades (see ledfx_beat)
 *   ALARM     everything flashing orange
 *   NOTIFY    everything pulsing in one colour, b = 0xRRGGBB (a notification, a sound heard) */
typedef enum { SCENE_NONE, SCENE_TUNER, SCENE_PROGRESS, SCENE_BEAT, SCENE_ALARM, SCENE_NOTIFY } ledfx_scene_t;
void ledfx_scene(ledfx_scene_t scene, float a, float b);
void ledfx_beat(uint32_t at_ms, bool accent);   /* a metronome beat at esp_timer ms */
void ledfx_sleep(bool sleep);      /* everything dark, the buttons board too, until woken */

#define LED_ROWS    10             /* the side strips are filled by row, 0 = bottom (ledfx.c) */
