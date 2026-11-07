#pragma once
/*
 * The five buttons on top: one resistor ladder on GPIO8 (ADC1 channel 7), so only one button reads
 * at a time. Left to right: LIGHT, left, power, right, MODE.
 *
 * Events:
 *   BTN_DOWN     the moment a button goes down (left / right: also every 110 ms while held, after 450 ms)
 *   BTN_SHORT    released before the long-press time (LIGHT, power, MODE only)
 *   BTN_LONG     held for 0.8 s (power: 1.5 s), sent once while still held (LIGHT, power, MODE only)
 */
#include <stdbool.h>
#include <stdint.h>

typedef enum { KEY_NONE = -1, KEY_LIGHT, KEY_LEFT, KEY_POWER, KEY_RIGHT, KEY_MODE, KEY_COUNT } btn_key_t;
typedef enum { BTN_DOWN, BTN_SHORT, BTN_LONG } btn_kind_t;

typedef struct {
    btn_key_t key;
    btn_kind_t kind;
    bool repeat;          /* a BTN_DOWN sent by auto-repeat */
} btn_event_t;

void buttons_start(void);
bool buttons_get(btn_event_t *e);          /* next event, false if none (never blocks) */
void buttons_inject(btn_key_t k, btn_kind_t kind);   /* a simulated event (web API, tests) */
btn_key_t buttons_key_from_name(const char *name);  /* "light", "left", "power", "right", "mode" */
