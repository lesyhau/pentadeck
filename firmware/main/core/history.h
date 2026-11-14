#pragma once
/*
 * The last 24 hours, a value a minute: the PC's CPU and GPU load and temperature, the home lab's CPU
 * load and temperature, and the room's sound level (the minute's Leq, dBA). In PSRAM, since the last
 * start. The web page draws it (GET /api/history).
 */
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef enum {
    HIST_PC_CPU, HIST_PC_CPU_TEMP, HIST_PC_GPU, HIST_PC_GPU_TEMP, HIST_LAB_CPU, HIST_LAB_CPU_TEMP, HIST_DBA,
    HIST_SERIES
} hist_series_t;

#define HIST_LEN      1440            /* minutes */
#define HIST_STEP_S   60

void history_init(void);
void history_tick(uint32_t ms);       /* every frame (UI task) */
const char *history_name(hist_series_t s);   /* "pc_cpu", "pc_cpu_temp", ... */
int history_count(void);              /* minutes recorded, up to HIST_LEN */
float history_at(hist_series_t s, int i);    /* i = 0 the oldest; NAN where nothing was known */
time_t history_last(void);            /* when the newest minute ended (0 = the clock wasn't set) */
