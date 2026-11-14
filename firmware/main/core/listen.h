#pragma once
/*
 * What the device listens for, beyond the screens' meters:
 *
 *  - The day's sound dose, as a hearing-safety meter counts it (NIOSH: 85 dBA for 8 hours is 100 %,
 *    and every 3 dB more halves the time), with the day's Leq. It restarts at local midnight and
 *    survives a restart, not a power cut. Levels assume the usual MEMS sensitivity:
 *    close, not calibrated.
 *  - Learned sounds: up to three (a doorbell, a kettle, a phone), each learned by listening to it
 *    for 4 s. A sound is a shape over the mic's 32 fine bands (300 Hz .. 10 kHz); it is heard when the
 *    room is well above its background and the spectrum's shape matches for about 0.1 s.
 *
 * Fed once a frame by the UI task; the modes show what was heard (modes.c).
 */
#include <stdbool.h>
#include <stdint.h>

#define LISTEN_SLOTS 3
#define LISTEN_NAME  16

typedef struct {
    bool used;                     /* learned */
    bool on;                       /* listened for */
    char name[LISTEN_NAME];        /* "DOORBELL" */
} listen_slot_t;

void listen_init(void);            /* load the learned sounds */
void listen_update(uint32_t ms);   /* every frame (UI task) */

/* the day's dose, %, and Leq, dBA (NAN before any sound was measured today) */
void listen_dose(float *pct, float *leq);
/* bumped when the dose passes 100 % (once a day) */
uint32_t listen_dose_alerts(void);

void listen_slots(listen_slot_t out[LISTEN_SLOTS]);
/* learn slot 0..2 as `name` over the next 4 s; false if a learning is already running */
bool listen_learn(int slot, const char *name);
/* -1: not learning, else the slot; *left_ms: what is left of it */
int listen_learning(uint32_t *left_ms);
/* how the last learning ended: 0 none yet, 1 learned, 2 nothing heard (too quiet) */
int listen_learn_result(void);
bool listen_enable(int slot, bool on);
bool listen_forget(int slot);
/* bumped on every sound heard; *slot and name: the latest */
uint32_t listen_heard(int *slot, char name[LISTEN_NAME]);
