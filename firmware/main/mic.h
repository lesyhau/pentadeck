#pragma once
/*
 * PDM microphone on the buttons board: CLK = GPIO19, DATA = GPIO20 (the chip's USB pins),
 * I2S0 in PDM RX mode, 32 kHz, 16-bit mono (left), as in the stock firmware.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define MIC_BANDS 16
#define MIC_RATE  32000           /* samples per second */

typedef struct {
    float dbfs;               /* loudness of the last block, dB full scale (RMS), about -90..0 */
    float dba;                /* A-weighted sound level of the last block, dB SPL. Assumes the usual MEMS
                                 sensitivity (-26 dBFS at 94 dB SPL), so SPL = dBFS + 120; not calibrated */
    float peak_dbfs;          /* loudest sample of the last block */
    float bands[MIC_BANDS];   /* spectrum, dB, 50 Hz .. 12 kHz, log spaced */
    unsigned blocks;          /* blocks processed so far */
    const char *mode;         /* which mic wiring was detected */
    float probe_dbfs[4];      /* signal level seen while probing i2s-left, i2s-right, pdm-left, pdm-right */
    float pitch_hz;           /* the tuner's fundamental, Hz (NAN: no clear pitch, or the tuner is off) */
    float pitch_clarity;      /* 0..1, how periodic the sound is (1 = a pure tone) */
} mic_snapshot_t;

esp_err_t mic_start(void);            /* starts the capture + analysis task */
bool mic_ok(void);
void mic_get(mic_snapshot_t *out);    /* copy of the latest analysis */
void mic_reprobe(void);               /* try every wiring again and keep the best */
void mic_tuner(bool on);
/* Tempo: an onset-strength envelope at 125 frames per second (8 ms), kept for 8.2 s. */
#define MIC_ONSET_HZ 125
#define MIC_ONSETS   1024
void mic_tempo(bool on);              /* compute the envelope (only while the tempo finder shows) */
/* Copy the newest n values, oldest first; *last_ms = esp_timer ms when the newest frame ended. */
int mic_onsets(float *out, int n, uint32_t *last_ms);
/* Chords and key: how strongly each of the 12 pitch classes (C, C#, .. B) sounds, from a 4096-point
 * spectrum at 8 kHz (0.5 s, 2 Hz a bin) every 64 ms, C2 (65 Hz) to E6 (1.3 kHz), spectral peaks only. */
void mic_chroma_on(bool on);
/* The latest frame (not normalised) and its number (changes every 64 ms); false if none yet. */
bool mic_chroma(float out[12], uint32_t *frame, float *energy);
/* The tuner's last 2048 samples (16 kHz, float), for tests; returns the count copied. */
int mic_tuner_samples(float *out, int n);
/* Low power: capture at 8 kHz and compute only the level (stays at 32 kHz while the mic is streamed). */
void mic_low_power(bool on);
int mic_rate(void);                   /* the capture rate now, Hz */
/* Presence: esp_timer ms of the last sound clearly above the room's background (0: none yet), and that
 * background (dBFS, NAN until measured). */
uint32_t mic_presence_ms(void);
void mic_read_stats(int *err, unsigned *fails, unsigned *got);   /* the last failed read, for /audio */
float mic_floor_db(void);              /* run pitch detection (costs CPU, so only while the tuner shows) */
