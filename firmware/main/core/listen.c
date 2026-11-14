#include "core/listen.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include "drivers/mic.h"
#include "net/netdata.h"

static const char *TAG = "listen";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static unsigned s_block;

#define BLOCK_S ((float)MIC_WAVE / MIC_RATE)   /* one mic block: 32 ms */

/* ---------------------------------------------------------------- dose */

/* Kept in no-init RAM: a restart (an update, a crash) doesn't lose the day so far; a power cut does. */
typedef struct {
    uint32_t magic;
    int32_t day;                   /* year * 1000 + day of the year, -1 = not known yet */
    double dose;                   /* % */
    double energy, secs;           /* for the Leq */
    uint8_t alerted;
} dose_t;
#define DOSE_MAGIC 0x5D05E001u
static __NOINIT_ATTR dose_t s_dose;
static volatile uint32_t s_dose_alerts;

static void dose_reset(int32_t day)
{
    memset(&s_dose, 0, sizeof s_dose);
    s_dose.magic = DOSE_MAGIC;
    s_dose.day = day;
}

static void dose_add(float dba, float dt)
{
    if (s_dose.magic != DOSE_MAGIC) dose_reset(-1);
    if (netdata_time_valid()) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        int32_t day = tm.tm_year * 1000 + tm.tm_yday;
        if (s_dose.day >= 0 && s_dose.day != day) dose_reset(day);   /* past midnight: a new day */
        s_dose.day = day;
    }
    /* NIOSH REL: 85 dBA for 8 h = 100 %, a 3 dB exchange rate */
    double allowed = 8.0 * 3600.0 / pow(2.0, (dba - 85.0) / 3.0);
    s_dose.dose += dt / allowed * 100.0;
    s_dose.energy += pow(10.0, dba / 10.0) * dt;
    s_dose.secs += dt;
    if (s_dose.dose >= 100.0 && !s_dose.alerted) {
        s_dose.alerted = 1;
        s_dose_alerts++;
    }
}

void listen_dose(float *pct, float *leq)
{
    bool ok = s_dose.magic == DOSE_MAGIC && s_dose.secs > 0;
    *pct = ok ? (float)s_dose.dose : 0;
    *leq = ok ? (float)(10.0 * log10(s_dose.energy / s_dose.secs)) : NAN;
}

uint32_t listen_dose_alerts(void) { return s_dose_alerts; }

/* ------------------------------------------------------------- sounds */

#define SHAPE_FLOOR (-40.0f)            /* a shape spans 40 dB under its loudest band */
#define LEARN_MS    4000
#define MATCH_R     0.85f               /* the correlation a match needs, for ... */
#define MATCH_N     3                   /* ... this many blocks in a row (~0.1 s) */
#define COOLDOWN_MS 8000

typedef struct {
    listen_slot_t s;
    int8_t shape[MIC_FINE];             /* dB under the loudest band, -40..0 */
} slot_t;

typedef struct {
    uint32_t version;
    slot_t slot[LISTEN_SLOTS];
} stored_t;
#define STORE_VERSION 1

static slot_t s_slot[LISTEN_SLOTS];
static int s_hits[LISTEN_SLOTS];
static uint32_t s_last_hit[LISTEN_SLOTS];
static volatile uint32_t s_heard_gen;
static int s_heard_slot;
static char s_heard_name[LISTEN_NAME];

static struct {
    int slot;                          /* -1 = not learning */
    char name[LISTEN_NAME];
    uint32_t start, until;             /* start 0: begins on the next frame */
    double acc[MIC_FINE];
    int n;
} s_learn = { .slot = -1 };
static volatile int s_learn_result;

static void save(void)
{
    stored_t st = { .version = STORE_VERSION };
    portENTER_CRITICAL(&s_mux);
    memcpy(st.slot, s_slot, sizeof s_slot);
    portEXIT_CRITICAL(&s_mux);
    nvs_handle_t h;
    if (nvs_open("tg", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, "sounds", &st, sizeof st) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

void listen_init(void)
{
    nvs_handle_t h;
    if (nvs_open("tg", NVS_READONLY, &h) != ESP_OK) return;
    stored_t st;
    size_t len = sizeof st;
    if (nvs_get_blob(h, "sounds", &st, &len) == ESP_OK && len == sizeof st && st.version == STORE_VERSION) {
        memcpy(s_slot, st.slot, sizeof s_slot);
        for (int i = 0; i < LISTEN_SLOTS; i++) s_slot[i].s.name[LISTEN_NAME - 1] = 0;
        ESP_LOGI(TAG, "learned sounds loaded");
    }
    nvs_close(h);
}

void listen_slots(listen_slot_t out[LISTEN_SLOTS])
{
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < LISTEN_SLOTS; i++) out[i] = s_slot[i].s;
    portEXIT_CRITICAL(&s_mux);
}

bool listen_learn(int slot, const char *name)
{
    if (slot < 0 || slot >= LISTEN_SLOTS || !name || !name[0]) return false;
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    if (s_learn.slot < 0) {
        s_learn.slot = slot;
        strlcpy(s_learn.name, name, sizeof s_learn.name);
        for (char *c = s_learn.name; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;   /* the screens use capitals */
        s_learn.start = 0;
        s_learn.n = 0;
        memset(s_learn.acc, 0, sizeof s_learn.acc);
        s_learn_result = 0;
        ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

int listen_learning(uint32_t *left_ms)
{
    int slot = s_learn.slot;
    if (left_ms) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        *left_ms = slot < 0 ? 0 : !s_learn.start ? LEARN_MS : (int32_t)(s_learn.until - now) > 0 ? s_learn.until - now : 0;
    }
    return slot;
}

int listen_learn_result(void) { return s_learn_result; }

bool listen_enable(int slot, bool on)
{
    if (slot < 0 || slot >= LISTEN_SLOTS || !s_slot[slot].s.used) return false;
    portENTER_CRITICAL(&s_mux);
    s_slot[slot].s.on = on;
    portEXIT_CRITICAL(&s_mux);
    save();
    return true;
}

bool listen_forget(int slot)
{
    if (slot < 0 || slot >= LISTEN_SLOTS) return false;
    portENTER_CRITICAL(&s_mux);
    memset(&s_slot[slot], 0, sizeof s_slot[slot]);
    portEXIT_CRITICAL(&s_mux);
    save();
    return true;
}

uint32_t listen_heard(int *slot, char name[LISTEN_NAME])
{
    portENTER_CRITICAL(&s_mux);
    uint32_t g = s_heard_gen;
    if (slot) *slot = s_heard_slot;
    if (name) memcpy(name, s_heard_name, LISTEN_NAME);
    portEXIT_CRITICAL(&s_mux);
    return g;
}

/* a block's spectrum as a shape: dB under its loudest band, floored */
static void shape_of(const float *fine, float *out)
{
    float mx = -200;
    for (int i = 0; i < MIC_FINE; i++) mx = fmaxf(mx, fine[i]);
    for (int i = 0; i < MIC_FINE; i++) out[i] = fmaxf(SHAPE_FLOOR, fine[i] - mx);
}

static float correlation(const float *a, const int8_t *b)
{
    float ma = 0, mb = 0;
    for (int i = 0; i < MIC_FINE; i++) { ma += a[i]; mb += b[i]; }
    ma /= MIC_FINE;
    mb /= MIC_FINE;
    float sab = 0, saa = 0, sbb = 0;
    for (int i = 0; i < MIC_FINE; i++) {
        float da = a[i] - ma, db = b[i] - mb;
        sab += da * db;
        saa += da * da;
        sbb += db * db;
    }
    return saa > 1e-6f && sbb > 1e-6f ? sab / sqrtf(saa * sbb) : 0;
}

static void learn_block(const mic_snapshot_t *m, float floor_db, uint32_t ms)
{
    if (!s_learn.start) {
        s_learn.start = ms;
        s_learn.until = ms + LEARN_MS;
    }
    if (m->dbfs > floor_db + 8.0f) {               /* only what stands out of the room */
        for (int i = 0; i < MIC_FINE; i++) s_learn.acc[i] += pow(10.0, m->fine[i] / 10.0);
        s_learn.n++;
    }
    if ((int32_t)(ms - s_learn.until) < 0) return;
    if (s_learn.n < 4) {                           /* under 0.13 s of it */
        s_learn_result = 2;
    } else {
        float fine[MIC_FINE], shape[MIC_FINE];
        for (int i = 0; i < MIC_FINE; i++) fine[i] = (float)(10.0 * log10(s_learn.acc[i] / s_learn.n + 1e-15));
        shape_of(fine, shape);
        slot_t sl = { .s = { .used = true, .on = true } };
        strlcpy(sl.s.name, s_learn.name, sizeof sl.s.name);
        for (int i = 0; i < MIC_FINE; i++) sl.shape[i] = (int8_t)lroundf(shape[i]);
        portENTER_CRITICAL(&s_mux);
        s_slot[s_learn.slot] = sl;
        portEXIT_CRITICAL(&s_mux);
        save();
        s_learn_result = 1;
        ESP_LOGI(TAG, "learned '%s' in slot %d from %d blocks", sl.s.name, s_learn.slot, s_learn.n);
    }
    portENTER_CRITICAL(&s_mux);
    s_learn.slot = -1;
    portEXIT_CRITICAL(&s_mux);
}

static void match_block(const mic_snapshot_t *m, float floor_db, uint32_t ms)
{
    bool loud = m->dbfs > floor_db + 10.0f;
    float shape[MIC_FINE];
    if (loud) shape_of(m->fine, shape);
    for (int i = 0; i < LISTEN_SLOTS; i++) {
        if (!s_slot[i].s.used || !s_slot[i].s.on || !loud) { s_hits[i] = 0; continue; }
        if (correlation(shape, s_slot[i].shape) < MATCH_R) { s_hits[i] = 0; continue; }
        if (++s_hits[i] < MATCH_N) continue;
        s_hits[i] = 0;
        if (s_last_hit[i] && ms - s_last_hit[i] < COOLDOWN_MS) continue;
        s_last_hit[i] = ms ? ms : 1;
        portENTER_CRITICAL(&s_mux);
        s_heard_slot = i;
        memcpy(s_heard_name, s_slot[i].s.name, LISTEN_NAME);
        s_heard_gen++;
        portEXIT_CRITICAL(&s_mux);
        ESP_LOGI(TAG, "heard '%s'", s_heard_name);
    }
}

void listen_update(uint32_t ms)
{
    mic_snapshot_t m;
    mic_get(&m);
    if (m.blocks == s_block) return;
    unsigned fresh = m.blocks - s_block;
    s_block = m.blocks;
    if (mic_rate() != MIC_RATE) return;             /* low power: the level only, nobody about */
    if (!isnan(m.dba)) dose_add(m.dba, fminf(1.0f, fresh * BLOCK_S));
    float floor_db = mic_floor_db();
    if (isnan(floor_db)) floor_db = -70.0f;
    if (s_learn.slot >= 0) learn_block(&m, floor_db, ms);
    else match_block(&m, floor_db, ms);
}
