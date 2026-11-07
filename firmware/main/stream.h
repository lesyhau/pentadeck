#pragma once
/*
 * The microphone as a live audio stream, for a PC to use as a microphone (agent/pentadeck_agent.py --mic).
 *
 *   GET http://<device>:8081/[?gain=DB][&token=TOKEN]     (or the token in an X-Token header)
 *
 * The reply is an endless WAV: 16-bit mono PCM at MIC_RATE (32 kHz), DC removed, `gain` dB louder
 * (0..48, default STREAM_GAIN_DB). VLC, ffmpeg, OBS's media source and the agent can all read it.
 * At most STREAM_MAX listeners at once. While anyone listens, the AUDIO screen reads MIC LIVE (or the
 * middle screen shows MIC + a red dot when no AUDIO screen is showing). The mic_stream setting turns it off.
 */
#include <stdint.h>
#include "esp_err.h"

#define STREAM_PORT     8081
#define STREAM_MAX      2
#define STREAM_GAIN_DB  30

esp_err_t stream_start(void);
void stream_push(const float *x, int n);   /* from the mic task: one block of samples, -1..1 */
int stream_listeners(void);
