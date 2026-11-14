#include "core/history.h"

#include <math.h>
#include <string.h>

#include "esp_heap_caps.h"

#include "drivers/mic.h"
#include "net/netdata.h"

static const char *const NAMES[HIST_SERIES] = { "pc_cpu", "pc_cpu_temp", "pc_gpu", "pc_gpu_temp", "lab_cpu",
                                                "lab_cpu_temp", "dba" };

static int16_t *s_v;                  /* [HIST_SERIES][HIST_LEN], tenths; INT16_MIN = unknown */
static int s_n, s_w;                  /* minutes kept, the next one written */
static time_t s_last;
static uint32_t s_minute_at, s_second_at;
static unsigned s_block;
/* this minute so far: the sums of the once-a-second samples, and the sound's energy */
static float s_sum[HIST_SERIES];
static int s_cnt[HIST_SERIES];
static double s_energy;
static int s_blocks;

void history_init(void)
{
    s_v = heap_caps_malloc(sizeof(int16_t) * HIST_SERIES * HIST_LEN, MALLOC_CAP_SPIRAM);
}

const char *history_name(hist_series_t s) { return s < HIST_SERIES ? NAMES[s] : "?"; }
int history_count(void) { return s_v ? s_n : 0; }
time_t history_last(void) { return s_last; }

float history_at(hist_series_t s, int i)
{
    if (!s_v || s >= HIST_SERIES || i < 0 || i >= s_n) return NAN;
    int16_t v = s_v[s * HIST_LEN + (s_w - s_n + i + HIST_LEN) % HIST_LEN];
    return v == INT16_MIN ? NAN : v / 10.0f;
}

static void sample(hist_series_t s, bool ok, float v)
{
    if (!ok || isnan(v)) return;
    s_sum[s] += v;
    s_cnt[s]++;
}

void history_tick(uint32_t ms)
{
    if (!s_v) return;
    if (!s_minute_at) s_minute_at = s_second_at = ms;
    mic_snapshot_t m;
    mic_get(&m);
    if (m.blocks != s_block) {
        s_block = m.blocks;
        if (!isnan(m.dba) && mic_rate() == MIC_RATE) {
            s_energy += pow(10.0, m.dba / 10.0);
            s_blocks++;
        }
    }
    if (ms - s_second_at >= 1000) {
        s_second_at += 1000;
        host_stats_t pc, lab;
        netdata_get_host(0, &pc);
        netdata_get_host(1, &lab);
        sample(HIST_PC_CPU, pc.ok, pc.cpu_load);
        sample(HIST_PC_CPU_TEMP, pc.ok, pc.cpu_temp);
        sample(HIST_PC_GPU, pc.ok && pc.has_gpu, pc.gpu.load);
        sample(HIST_PC_GPU_TEMP, pc.ok && pc.has_gpu, pc.gpu.temp);
        sample(HIST_LAB_CPU, lab.ok, lab.cpu_load);
        sample(HIST_LAB_CPU_TEMP, lab.ok, lab.cpu_temp);
    }
    if (ms - s_minute_at < HIST_STEP_S * 1000) return;
    s_minute_at += HIST_STEP_S * 1000;
    if (s_blocks) {
        s_sum[HIST_DBA] = (float)(10.0 * log10(s_energy / s_blocks));
        s_cnt[HIST_DBA] = 1;
    }
    for (int s = 0; s < HIST_SERIES; s++) {
        float v = s_cnt[s] ? s_sum[s] / s_cnt[s] : NAN;
        s_v[s * HIST_LEN + s_w] = isnan(v) ? INT16_MIN : (int16_t)lroundf(fmaxf(-3000, fminf(3000, v)) * 10);
    }
    s_w = (s_w + 1) % HIST_LEN;
    if (s_n < HIST_LEN) s_n++;
    s_last = netdata_time_valid() ? time(NULL) : 0;
    memset(s_sum, 0, sizeof s_sum);
    memset(s_cnt, 0, sizeof s_cnt);
    s_energy = 0;
    s_blocks = 0;
}
