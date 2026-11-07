#include "mic.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hal/usb_serial_jtag_ll.h"

static const char *TAG = "mic";

/* The stock firmware supports two mic boards (it probes which one is fitted):
 *   standard I2S mic: BCK GPIO19, WS GPIO3, DATA GPIO20, 32-bit, 32 kHz, left slot
 *   PDM mic:          CLK GPIO19, DATA GPIO20, 16-bit, 32 kHz
 * We try them in turn and keep the first that delivers a real signal. */
#define PIN_CLK  19
#define PIN_WS   3
#define PIN_DATA 20
#define RATE     32000
#define N        1024            /* FFT size: 32 ms per block, 31.25 Hz per bin */
#define F_LO     50.0f
#define F_HI     12000.0f

typedef enum { MODE_I2S_LEFT, MODE_I2S_RIGHT, MODE_PDM_LEFT, MODE_PDM_RIGHT, MODE_COUNT } mic_mode_t;
static const char *MODE_NAMES[MODE_COUNT] = { "i2s-left", "i2s-right", "pdm-left", "pdm-right" };

static i2s_chan_handle_t s_rx;
static mic_mode_t s_mode;
static bool s_ok;
static mic_snapshot_t s_snap;
static float s_probe[MODE_COUNT] = { -999, -999, -999, -999 };
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static int32_t s_raw[N];          /* big enough for N 32-bit or N 16-bit samples */
static float s_x[N];              /* samples, -1..1 */
static void fft(float *re, float *im, int n);
static float s_re[N], s_im[N], s_win[N];
static int s_band_lo[MIC_BANDS], s_band_hi[MIC_BANDS];
static float s_aw[N / 2];          /* A-weighting per FFT bin, as a power factor */

/* Tuner: YIN pitch detection on the signal decimated to 16 kHz. A 1024-sample window (64 ms) and
 * lags of 7..400 samples cover 40 Hz (a bass's low E is 41 Hz) to 2.3 kHz (a flute's C7 is 2.1 kHz). */
#define P_RATE  (RATE / 2)
#define P_LEN   2048
#define P_W     1024
#define P_TMIN  7                /* 2.3 kHz: the top of a flute */
#define P_TMAX  400
static volatile bool s_tuner;
static float s_ring[P_LEN];
static float s_d[P_TMAX + 2];

void mic_tuner(bool on)
{
    s_tuner = on;
}

/* Tempo: per 8 ms sub-block, the energy of the bass (one-pole low-pass at ~150 Hz, the kick) and of
 * the treble (first difference, the hats and attacks); the onset strength is how much either rose
 * (in log energy), half-wave rectified. */
static volatile bool s_tempo;
static float s_onset[MIC_ONSETS];
static int s_onset_w;                  /* next write index */
static uint32_t s_onset_ms;
static float s_lo_y, s_prev_x, s_lo_e = 1e-9f, s_hi_e = 1e-9f;

void mic_tempo(bool on)
{
    s_tempo = on;
}

int mic_onsets(float *out, int n, uint32_t *last_ms)
{
    if (n > MIC_ONSETS) n = MIC_ONSETS;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < n; i++) out[i] = s_onset[(s_onset_w - n + i + MIC_ONSETS) % MIC_ONSETS];
    *last_ms = s_onset_ms;
    portEXIT_CRITICAL(&s_mux);
    return n;
}

static void onsets_update(void)
{
    const int SUB = RATE / MIC_ONSET_HZ;               /* 256 samples */
    const float k = 1.0f - expf(-2.0f * (float)M_PI * 150.0f / RATE);
    float out[N / (RATE / MIC_ONSET_HZ)];
    int m = 0;
    for (int b = 0; b + SUB <= N; b += SUB) {
        float el = 0, eh = 0;
        for (int i = b; i < b + SUB; i++) {
            s_lo_y += k * (s_x[i] - s_lo_y);
            el += s_lo_y * s_lo_y;
            float d = s_x[i] - s_prev_x;
            s_prev_x = s_x[i];
            eh += d * d;
        }
        el += 1e-9f;
        eh += 1e-9f;
        float o = fmaxf(0, logf(el / s_lo_e)) + 0.5f * fmaxf(0, logf(eh / s_hi_e));
        s_lo_e = el;
        s_hi_e = eh;
        out[m++] = o;
    }
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < m; i++) {
        s_onset[s_onset_w] = out[i];
        s_onset_w = (s_onset_w + 1) % MIC_ONSETS;
    }
    s_onset_ms = (uint32_t)(esp_timer_get_time() / 1000);
    portEXIT_CRITICAL(&s_mux);
}

/* ------------------------------------------------------------- chroma */

#define C_RATE (RATE / 4)               /* 8 kHz */
#define C_N    4096                     /* 0.51 s, 1.95 Hz a bin */
#define C_HOP  2048                     /* a frame every 256 ms */
static volatile bool s_chroma_on;
static float *c_ring, *c_re, *c_im;     /* in PSRAM, allocated on first use */
static int c_w, c_new, c_dec;
static float c_b[5], c_s1[4], c_s2[4];  /* the anti-alias low-pass, two biquads (coefficients, state) */
static float c_b2[5];
static float s_chroma_v[12], s_chroma_e;
static uint32_t s_chroma_n;

void mic_chroma_on(bool on)
{
    s_chroma_on = on;
}

bool mic_chroma(float out[12], uint32_t *frame, float *energy)
{
    portENTER_CRITICAL(&s_mux);
    memcpy(out, s_chroma_v, sizeof s_chroma_v);
    *frame = s_chroma_n;
    *energy = s_chroma_e;
    portEXIT_CRITICAL(&s_mux);
    return s_chroma_n > 0;
}

/* a low-pass biquad's coefficients b0, b1, b2, a1, a2 (fs = 32 kHz) */
static void lp_coef(float *c, float fc, float q)
{
    float w = 2.0f * (float)M_PI * fc / RATE, al = sinf(w) / (2.0f * q), co = cosf(w), a0 = 1 + al;
    c[0] = (1 - co) / 2 / a0; c[1] = (1 - co) / a0; c[2] = (1 - co) / 2 / a0;
    c[3] = -2 * co / a0; c[4] = (1 - al) / a0;
}

static float bq(const float *c, float *st, float x)
{
    /* transposed direct form II: st[0], st[1] */
    float y = c[0] * x + st[0];
    st[0] = c[1] * x - c[3] * y + st[1];
    st[1] = c[2] * x - c[4] * y;
    return y;
}

static void chroma_frame(void)
{
    /* Hann-windowed copy of the last C_N samples, oldest first */
    for (int i = 0; i < C_N; i++) {
        float w = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (C_N - 1));
        c_re[i] = c_ring[(c_w + i) % C_N] * w;
        c_im[i] = 0;
    }
    fft(c_re, c_im, C_N);
    for (int k = 0; k < C_N / 2; k++) c_re[k] = sqrtf(c_re[k] * c_re[k] + c_im[k] * c_im[k]);   /* magnitude */
    float v[12] = { 0 }, e = 0;
    const float bin = (float)C_RATE / C_N;
    int k0 = (int)(65.0f / bin), k1 = (int)(1300.0f / bin);   /* above ~1.3 kHz it is mostly overtones */
    float top = 1e-9f;
    for (int k = k0; k <= k1; k++) if (c_re[k] > top) top = c_re[k];
    for (int k = k0; k <= k1; k++) {
        float m = c_re[k];
        e += m;
        if (!(m > c_re[k - 1] && m >= c_re[k + 1]) || m < 0.05f * top) continue;   /* real peaks only */
        /* the peak's true frequency from a parabola through it (in log magnitude) */
        float a = logf(c_re[k - 1] + 1e-9f), b = logf(m + 1e-9f), c = logf(c_re[k + 1] + 1e-9f);
        float den = a - 2 * b + c, sh = den < 0 ? 0.5f * (a - c) / den : 0;
        float f = (k + sh) * bin;
        float midi = 69.0f + 12.0f * log2f(f / 440.0f);
        int n = (int)lroundf(midi);
        float off = midi - n;                                           /* -0.5 .. 0.5 semitone */
        float w = cosf((float)M_PI * off);                              /* in tune counts most */
        /* mildly log-compressed, so one loud note (or a speaker's resonance) can't drown the others;
         * tuned offline on synthetic chords (bass + triad + melody + noise: 72 of 72 named) */
        v[((n % 12) + 12) % 12] += logf(1.0f + 10.0f * m / top) * w * w;
    }
    portENTER_CRITICAL(&s_mux);
    memcpy(s_chroma_v, v, sizeof v);
    s_chroma_e = e;
    s_chroma_n++;
    portEXIT_CRITICAL(&s_mux);
}

static void chroma_update(void)
{
    if (!c_ring) {
        c_ring = heap_caps_calloc(C_N, sizeof(float), MALLOC_CAP_SPIRAM);
        c_re = heap_caps_calloc(C_N, sizeof(float), MALLOC_CAP_SPIRAM);
        c_im = heap_caps_calloc(C_N, sizeof(float), MALLOC_CAP_SPIRAM);
        if (!c_ring || !c_re || !c_im) { s_chroma_on = false; return; }
        lp_coef(c_b, 3000.0f, 0.5412f);                                 /* 4th-order Butterworth */
        lp_coef(c_b2, 3000.0f, 1.3066f);
    }
    for (int i = 0; i < N; i++) {
        float y = bq(c_b2, c_s2, bq(c_b, c_s1, s_x[i]));
        if (++c_dec < 4) continue;
        c_dec = 0;
        c_ring[c_w] = y;
        c_w = (c_w + 1) % C_N;
        if (++c_new >= C_HOP) {
            c_new = 0;
            chroma_frame();
        }
    }
}

int mic_tuner_samples(float *out, int n)
{
    if (n > P_LEN) n = P_LEN;
    portENTER_CRITICAL(&s_mux);
    memcpy(out, s_ring + P_LEN - n, n * sizeof(float));
    portEXIT_CRITICAL(&s_mux);
    return n;
}

/* A 2nd-order Butterworth low-pass at 1.5 kHz on the 16 kHz signal, run before YIN. Without it the
 * sharp overtones (a piezo or a bright string is mostly overtones) only line up at whole-sample lags
 * near a multiple of the period, and YIN jumps an octave. Measured on the device: 82..1200 Hz all
 * within 5 cents with it, three octave errors without. */
static float s_lp_b0, s_lp_b1, s_lp_b2, s_lp_a1, s_lp_a2, s_lp_x1, s_lp_x2, s_lp_y1, s_lp_y2;

static void lp_plan(void)
{
    float w = 2.0f * (float)M_PI * 1500.0f / P_RATE, al = sinf(w) / (2.0f * 0.7071f), c = cosf(w);
    float a0 = 1 + al;
    s_lp_b0 = (1 - c) / 2 / a0;
    s_lp_b1 = (1 - c) / a0;
    s_lp_b2 = (1 - c) / 2 / a0;
    s_lp_a1 = -2 * c / a0;
    s_lp_a2 = (1 - al) / a0;
}

static float lp(float v)
{
    float o = s_lp_b0 * v + s_lp_b1 * s_lp_x1 + s_lp_b2 * s_lp_x2 - s_lp_a1 * s_lp_y1 - s_lp_a2 * s_lp_y2;
    s_lp_x2 = s_lp_x1; s_lp_x1 = v; s_lp_y2 = s_lp_y1; s_lp_y1 = o;
    return o;
}

/* Returns the fundamental in Hz (NAN if none) and the clarity (1 - the dip's depth): YIN's rule,
 * the first dip under 0.15 (followed down to its bottom), else the deepest dip if it is under 0.3,
 * with the lag refined between samples by a parabola through the dip. */
static float yin(const float *x, float *clarity)
{
    s_d[0] = 1.0f;
    float run = 0;
    for (int t = 1; t <= P_TMAX + 1; t++) {
        float d = 0;
        for (int j = 0; j < P_W; j++) {
            float v = x[j] - x[j + t];
            d += v * v;
        }
        run += d;
        s_d[t] = run > 0 ? d * t / run : 1.0f;
    }
    int best = -1;
    for (int t = P_TMIN; t <= P_TMAX; t++) {
        if (s_d[t] < 0.15f) {
            while (t + 1 <= P_TMAX && s_d[t + 1] < s_d[t]) t++;
            best = t;
            break;
        }
    }
    if (best < 0) {
        int g = P_TMIN;
        for (int t = P_TMIN; t <= P_TMAX; t++) if (s_d[t] < s_d[g]) g = t;
        if (s_d[g] < 0.3f) best = g;
    }
    if (best < 0) {
        *clarity = 0;
        return NAN;
    }
    float a = s_d[best - 1], b = s_d[best], c = s_d[best + 1];
    float den = a - 2 * b + c;
    float sh = den != 0 ? 0.5f * (a - c) / den : 0;
    if (sh < -1) sh = -1;
    if (sh > 1) sh = 1;
    float v = b - 0.25f * (a - c) * sh;
    *clarity = 1.0f - (v < 0 ? 0 : v);
    return (float)P_RATE / (best + sh);
}

/* ---------------------------------------------------------------- analysis */

static void fft(float *re, float *im, int n)
{
    for (int i = 1, j = 0; i < n; i++) {           /* bit reversal */
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / len;
        float wr = cosf(ang), wi = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = i + k + len / 2;
                float tr = re[b] * cr - im[b] * ci;
                float ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr;        im[a] += ti;
                float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
}

static void plan_bands(void)
{
    for (int i = 0; i < N; i++) {
        s_win[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (N - 1));   /* Hann */
    }
    const float bin_hz = (float)RATE / N;
    for (int b = 0; b < MIC_BANDS; b++) {
        float f0 = F_LO * powf(F_HI / F_LO, (float)b / MIC_BANDS);
        float f1 = F_LO * powf(F_HI / F_LO, (float)(b + 1) / MIC_BANDS);
        int lo = (int)lroundf(f0 / bin_hz), hi = (int)lroundf(f1 / bin_hz);
        if (lo < 1) lo = 1;
        if (hi <= lo) hi = lo + 1;
        s_band_lo[b] = lo;
        s_band_hi[b] = hi;    /* bins [lo, hi) */
    }
    /* A-weighting (IEC 61672): R_A(f) = 12194^2 f^4 / ((f^2+20.6^2) sqrt((f^2+107.7^2)(f^2+737.9^2)) (f^2+12194^2)),
     * A(f) = 20 log10 R_A(f) + 2.00 dB; stored squared (power). */
    s_aw[0] = 0;
    for (int k = 1; k < N / 2; k++) {
        double f2 = (double)k * bin_hz * k * bin_hz;
        double ra = (12194.0 * 12194.0 * f2 * f2) /
                    ((f2 + 20.6 * 20.6) * sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0));
        ra *= 1.2589254;                              /* +2.00 dB: 0 dB at 1 kHz */
        s_aw[k] = (float)(ra * ra);
    }
}

/* Standard deviation of the block after removing DC (also fills s_x without DC). */
static float block_std(void)
{
    float mean = 0;
    for (int i = 0; i < N; i++) mean += s_x[i];
    mean /= N;
    float sum2 = 0;
    for (int i = 0; i < N; i++) {
        s_x[i] -= mean;
        sum2 += s_x[i] * s_x[i];
    }
    return sqrtf(sum2 / N);
}

static void analyse(void)
{
    float rms = block_std();
    float peak = 0;
    for (int i = 0; i < N; i++) {
        if (fabsf(s_x[i]) > peak) peak = fabsf(s_x[i]);
        s_re[i] = s_x[i] * s_win[i];
        s_im[i] = 0;
    }
    fft(s_re, s_im, N);

    mic_snapshot_t snap;
    snap.dbfs = 20.0f * log10f(rms + 1e-9f);
    snap.peak_dbfs = 20.0f * log10f(peak + 1e-9f);
    /* A-weighted mean square via Parseval: one-sided Hann spectrum, sum(w^2) = 3N/8, so
     * ms = 16 / (3 N^2) * sum |X_k|^2 over k = 1 .. N/2-1 */
    float pa = 0;
    for (int k = 1; k < N / 2; k++) pa += (s_re[k] * s_re[k] + s_im[k] * s_im[k]) * s_aw[k];
    pa *= 16.0f / (3.0f * (float)N * N);
    snap.dba = 10.0f * log10f(pa + 1e-14f) + 120.0f;
    for (int b = 0; b < MIC_BANDS; b++) {
        float p = 0;
        for (int k = s_band_lo[b]; k < s_band_hi[b]; k++) {
            p += s_re[k] * s_re[k] + s_im[k] * s_im[k];
        }
        /* average power per bin, scaled so a full-scale sine lands near 0 dB */
        p = p / (s_band_hi[b] - s_band_lo[b]) * 16.0f / ((float)N * N);
        snap.bands[b] = 10.0f * log10f(p + 1e-12f);
    }
    if (s_tempo) onsets_update();
    if (s_chroma_on) chroma_update();
    snap.pitch_hz = NAN;
    snap.pitch_clarity = 0;
    if (s_tuner) {
        /* decimate by 2 (a 2-tap average is enough of a low-pass for pitch), append to the ring */
        static float fresh[N / 2];
        for (int i = 0; i < N / 2; i++) fresh[i] = lp(0.5f * (s_x[2 * i] + s_x[2 * i + 1]));
        portENTER_CRITICAL(&s_mux);                    /* so /api/pcm never sees a half-shifted ring */
        memmove(s_ring, s_ring + N / 2, (P_LEN - N / 2) * sizeof(float));
        memcpy(s_ring + P_LEN - N / 2, fresh, sizeof fresh);
        portEXIT_CRITICAL(&s_mux);
        if (snap.dbfs > -62.0f) {
            float cl;
            float hz = yin(s_ring + P_LEN - (P_W + P_TMAX + 2), &cl);
            snap.pitch_hz = hz;
            snap.pitch_clarity = cl;
        }
    }
    portENTER_CRITICAL(&s_mux);
    snap.blocks = s_snap.blocks + 1;
    snap.mode = MODE_NAMES[s_mode];
    memcpy(snap.probe_dbfs, s_probe, sizeof s_probe);
    s_snap = snap;
    portEXIT_CRITICAL(&s_mux);
}

/* ----------------------------------------------------------------- capture */

/* GPIO19/20 are the chip's USB D-/D+. After reset the USB PHY owns the pads and holds a ~1.5 kOhm
 * pull-up on D+ (GPIO20), which pins the mic's data line high. Release them, as the stock firmware's
 * gpio_config(GPIO20, input, pull-down) did: gpio_config with pull-up disabled also clears the USB
 * D+ pull-up. */
static void release_usb_pins(void)
{
    usb_serial_jtag_ll_phy_enable_pad(false);
    const gpio_config_t data = {
        .pin_bit_mask = 1ULL << PIN_DATA,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    gpio_config(&data);
    const gpio_config_t clk = {
        .pin_bit_mask = 1ULL << PIN_CLK,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&clk);
}

static esp_err_t open_mode(mic_mode_t mode)
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 6;
    chan.dma_frame_num = 256;
    esp_err_t err = i2s_new_channel(&chan, NULL, &s_rx);
    if (err != ESP_OK) {
        return err;
    }
    if (mode == MODE_I2S_LEFT || mode == MODE_I2S_RIGHT) {
        i2s_std_config_t cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = PIN_CLK,
                .ws = PIN_WS,
                .dout = I2S_GPIO_UNUSED,
                .din = PIN_DATA,
            },
        };
        cfg.slot_cfg.slot_mask = mode == MODE_I2S_LEFT ? I2S_STD_SLOT_LEFT : I2S_STD_SLOT_RIGHT;
        err = i2s_channel_init_std_mode(s_rx, &cfg);
    } else {
        i2s_pdm_rx_config_t cfg = {
            .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(RATE),
            .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = { .clk = PIN_CLK, .din = PIN_DATA, .invert_flags = { .clk_inv = false } },
        };
        cfg.slot_cfg.slot_mask = mode == MODE_PDM_LEFT ? I2S_PDM_SLOT_LEFT : I2S_PDM_SLOT_RIGHT;
        err = i2s_channel_init_pdm_rx_mode(s_rx, &cfg);
    }
    if (err == ESP_OK) {
        err = i2s_channel_enable(s_rx);
    }
    if (err != ESP_OK) {
        i2s_del_channel(s_rx);
        s_rx = NULL;
        return err;
    }
    s_mode = mode;
    return ESP_OK;
}

static void close_mode(void)
{
    if (s_rx) {
        i2s_channel_disable(s_rx);
        i2s_del_channel(s_rx);
        s_rx = NULL;
    }
}

/* Read one block of N samples into s_x (-1..1). */
static esp_err_t read_block(void)
{
    bool wide = s_mode == MODE_I2S_LEFT || s_mode == MODE_I2S_RIGHT;
    size_t want = N * (wide ? 4 : 2), got = 0;
    esp_err_t err = i2s_channel_read(s_rx, s_raw, want, &got, pdMS_TO_TICKS(200));
    if (err != ESP_OK || got != want) {
        return err != ESP_OK ? err : ESP_ERR_INVALID_SIZE;
    }
    if (wide) {
        for (int i = 0; i < N; i++) s_x[i] = s_raw[i] / 2147483648.0f;
    } else {
        const int16_t *p = (const int16_t *)s_raw;
        for (int i = 0; i < N; i++) s_x[i] = p[i] / 32768.0f;
    }
    return ESP_OK;
}

/* Try each wiring for ~0.4 s; keep the first with a real signal (a working mic always has some noise). */
static void pick_mode(void)
{
    float best_std = -1;
    mic_mode_t best = MODE_I2S_LEFT;
    for (int m = 0; m < MODE_COUNT; m++) {
        if (open_mode((mic_mode_t)m) != ESP_OK) {
            ESP_LOGW(TAG, "%s: cannot open", MODE_NAMES[m]);
            continue;
        }
        float std = 0;
        int blocks = 0;
        for (int i = 0; i < 14; i++) {
            if (read_block() == ESP_OK && i >= 2) {   /* skip the first blocks (filters settling) */
                std += block_std();
                blocks++;
            }
        }
        std = blocks ? std / blocks : 0;
        s_probe[m] = 20.0f * log10f(std + 1e-9f);
        ESP_LOGI(TAG, "%s: signal %.1f dBFS", MODE_NAMES[m], s_probe[m]);
        close_mode();
        if (std > best_std) {
            best_std = std;
            best = (mic_mode_t)m;
        }
    }
    open_mode(best);
    ESP_LOGI(TAG, "using %s", MODE_NAMES[best]);
}

static volatile bool s_reprobe;

void mic_reprobe(void)
{
    s_reprobe = true;
}

static void mic_task(void *arg)
{
    pick_mode();
    s_ok = s_rx != NULL;
    for (;;) {
        if (s_reprobe) {
            s_reprobe = false;
            close_mode();
            pick_mode();
            s_ok = s_rx != NULL;
        }
        esp_err_t err = s_rx ? read_block() : ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) {
            analyse();
        } else if (err != ESP_ERR_TIMEOUT) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

esp_err_t mic_start(void)
{
    plan_bands();
    lp_plan();
    release_usb_pins();
    s_snap.mode = "probing";
    xTaskCreatePinnedToCore(mic_task, "mic", 4096, NULL, 6, NULL, 1);
    return ESP_OK;
}

bool mic_ok(void)
{
    return s_ok;
}

void mic_get(mic_snapshot_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_snap;
    portEXIT_CRITICAL(&s_mux);
}
