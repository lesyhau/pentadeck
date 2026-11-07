#pragma once
/*
 * Low power when nobody seems to be at the desk.
 *
 * The device is "idle" once nothing has shown that someone is about for the settings' idle_min minutes
 * (0 = never). Signs of someone: sound clearly above the room's background (mic.c), a button, keyboard
 * or mouse input on a PC (its agent's input_idle_s), an alarm ringing, a pairing request, POST
 * /api/power?state=wake. Day or night doesn't matter.
 *
 * While idle: the screens fade to idle_brightness and redraw at 4 fps, the LEDs go off, the mic
 * captures at 8 kHz and computes only the level, Wi-Fi uses modem sleep, the agents are polled every
 * 5 s, and the CPU may slow to 80 MHz. A mic stream (stream.c) keeps the mic and Wi-Fi at full speed.
 */
#include <stdbool.h>
#include <stdint.h>

void power_start(void);
bool power_idle(void);
void power_wake(const char *why);     /* someone is here: back to full power, restart the countdown */
void power_sleep_now(void);           /* go idle now (until the next sign of someone) */

/* For /api/status: the state, what last showed someone ("sound", "button", "pc input", ...), and how
 * long ago, in seconds. */
const char *power_last_reason(void);
uint32_t power_quiet_s(void);
