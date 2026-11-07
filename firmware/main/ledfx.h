#pragma once
/* What the LED chain shows. Layout (mapped 2026-11-07): left strip 0..14 bottom -> top, back 15..29
 * left -> right (seen from the front), right strip 30..44 top -> bottom. Modes (setting "led_mode"):
 *   off     everything dark
 *   glow    a steady teal glow
 *   status  left strip = PC CPU load, right strip = PC GPU load (both filling from the bottom,
 *           orange from 60 %, red from 80 %, blinking like the screens); back = home lab health
 *   music   side strips = loudness meters from the bottom, back = 15-band spectrum
 * In glow and music the side strips still flash orange/red on a CPU/GPU load alert.
 * The buttons board's LEDs (GPIO44, single colour) stay on at the LED brightness unless the mode is off. */
#include <stdbool.h>
#include <stdint.h>

#define LED_LEFT0   0
#define LED_BACK0   15
#define LED_RIGHT0  30
#define LED_STRIP   15
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
