#include "net/stream.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_http_server.h"   /* httpd_query_key_value() only */
#include "esp_log.h"
#include "lwip/sockets.h"

#include "app.h"
#include "core/settings.h"
#include "drivers/mic.h"

static const char *TAG = "stream";

static bool stream_enabled(void);

#define RING  32768            /* samples kept: 1.02 s at 32 kHz (a power of two) */
#define CHUNK 512              /* samples per send: 16 ms */
#define TICK  (pdMS_TO_TICKS(10) ? pdMS_TO_TICKS(10) : 1)

static float *s_ring;           /* in PSRAM */
static uint32_t s_written;      /* samples ever written; the ring holds the last RING of them */
static int s_listeners;
static bool s_primed;           /* the DC blocker has a starting point */
static float s_x1, s_y1;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

int stream_listeners(void)
{
    return __atomic_load_n(&s_listeners, __ATOMIC_RELAXED);
}

void stream_push(const float *x, int n)
{
    if (!stream_listeners() || !s_ring) {
        s_primed = false;
        return;
    }
    if (!s_primed) {            /* start the filter at the current offset, so there's no thump */
        s_x1 = x[0];
        s_y1 = 0;
        s_primed = true;
    }
    /* One-pole DC blocker, corner ~20 Hz: an I2S MEMS mic carries a large offset. */
    const float r = 0.996f;
    uint32_t w = s_written;
    for (int i = 0; i < n; i++) {
        float y = x[i] - s_x1 + r * s_y1;
        s_x1 = x[i];
        s_y1 = y;
        s_ring[(w + i) & (RING - 1)] = y;
    }
    __atomic_store_n(&s_written, w + n, __ATOMIC_RELEASE);
}

/* ------------------------------------------------------------- one listener */

typedef struct {
    int fd;
    float gain;
} client_t;

static bool send_all(int fd, const void *p, int len)
{
    const char *b = p;
    while (len > 0) {
        int n = send(fd, b, len, 0);
        if (n <= 0) return false;
        b += n;
        len -= n;
    }
    return true;
}

static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

static void client_task(void *arg)
{
    client_t c = *(client_t *)arg;
    free(arg);

    static const char head[] = "HTTP/1.0 200 OK\r\nContent-Type: audio/wav\r\nCache-Control: no-store\r\n"
                               "Connection: close\r\n\r\n";
    /* WAV header with the sizes left open (0xFFFFFFFF), the usual way to stream one */
    uint8_t wav[44];
    memcpy(wav, "RIFF", 4);
    put32(wav + 4, 0xFFFFFFFFu);
    memcpy(wav + 8, "WAVEfmt ", 8);
    put32(wav + 16, 16);
    put16(wav + 20, 1);                 /* PCM */
    put16(wav + 22, 1);                 /* mono */
    put32(wav + 24, MIC_RATE);
    put32(wav + 28, MIC_RATE * 2);
    put16(wav + 32, 2);
    put16(wav + 34, 16);
    memcpy(wav + 36, "data", 4);
    put32(wav + 40, 0xFFFFFFFFu);

    int16_t buf[CHUNK];
    uint32_t pos = __atomic_load_n(&s_written, __ATOMIC_ACQUIRE);   /* from the next block on */
    uint32_t gen = settings_generation();
    int idle = 0;
    bool ok = send_all(c.fd, head, sizeof head - 1) && send_all(c.fd, wav, sizeof wav);
    while (ok) {
        if (settings_generation() != gen) {     /* turned off in the settings: cut the listener off */
            gen = settings_generation();
            if (!stream_enabled()) break;
        }
        uint32_t w = __atomic_load_n(&s_written, __ATOMIC_ACQUIRE);
        uint32_t avail = w - pos;
        if (avail > RING - 4 * CHUNK) {        /* fell behind (a slow network): skip to now */
            pos = w;
            continue;
        }
        if (avail == 0) {
            vTaskDelay(TICK);
            /* no samples for 2 s (the mic re-probing, or stopped): send silence, which also
             * notices a listener that has gone away */
            if (++idle * 10 >= 2000) {
                idle = 0;
                memset(buf, 0, sizeof buf);
                ok = send_all(c.fd, buf, sizeof buf);
            }
            continue;
        }
        idle = 0;
        int n = avail < CHUNK ? (int)avail : CHUNK;
        for (int i = 0; i < n; i++) {
            float v = s_ring[(pos + i) & (RING - 1)] * c.gain * 32767.0f;
            buf[i] = v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : (int16_t)lrintf(v);
        }
        ok = send_all(c.fd, buf, n * 2);
        pos += n;
    }

    close(c.fd);
    portENTER_CRITICAL(&s_mux);
    s_listeners--;
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "listener left (%d listening)", stream_listeners());
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ server */

static void reply(int fd, const char *status, const char *text)
{
    char b[192];
    int n = snprintf(b, sizeof b, "HTTP/1.0 %s\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n%s", status, text);
    send_all(fd, b, n);
}

/* The request line and headers, up to the blank line. */
static bool read_request(int fd, char *buf, int cap)
{
    int got = 0;
    while (got < cap - 1) {
        int n = recv(fd, buf + got, cap - 1 - got, 0);
        if (n <= 0) return false;
        got += n;
        buf[got] = 0;
        if (strstr(buf, "\r\n\r\n")) return true;
    }
    return false;
}

static bool header_value(const char *req, const char *name, char *out, int cap)
{
    size_t nl = strlen(name);
    for (const char *p = strstr(req, "\r\n"); p && p[2]; p = strstr(p + 2, "\r\n")) {
        const char *line = p + 2;
        if (strncasecmp(line, name, nl) != 0 || line[nl] != ':') continue;
        const char *v = line + nl + 1;
        while (*v == ' ' || *v == '\t') v++;
        const char *e = strstr(v, "\r\n");
        int len = e ? (int)(e - v) : (int)strlen(v);
        if (len >= cap) return false;
        memcpy(out, v, len);
        out[len] = 0;
        return true;
    }
    return false;
}

static bool token_ok(const char *t)
{
    const char *want = app_token();
    size_t n = strlen(want);
    if (strlen(t) != n) return false;
    unsigned diff = 0;
    for (size_t i = 0; i < n; i++) diff |= (unsigned char)(t[i] ^ want[i]);
    return diff == 0;
}

static bool stream_enabled(void)
{
    tg_settings_t s;
    settings_get(&s);
    return s.mic_stream;
}

static void handle(int fd, const struct sockaddr_in *peer)
{
    struct timeval rt = { .tv_sec = 3 }, st = { .tv_sec = 5 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof rt);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &st, sizeof st);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

    char req[1024];
    if (!read_request(fd, req, sizeof req) || strncmp(req, "GET ", 4) != 0) {
        reply(fd, "400 Bad Request", "send GET /\n");
        close(fd);
        return;
    }
    /* the query string: between '?' and the space before HTTP/1.x, on the first line */
    char query[192] = "";
    const char *eol = strstr(req, "\r\n"), *q = strchr(req, '?');
    if (q && q < eol) {
        const char *e = memchr(q, ' ', eol - q);
        int len = (int)((e ? e : eol) - (q + 1));
        if (len > 0 && len < (int)sizeof query) {
            memcpy(query, q + 1, len);
            query[len] = 0;
        }
    }
    char tok[96] = "";
    if (!header_value(req, "X-Token", tok, sizeof tok)) httpd_query_key_value(query, "token", tok, sizeof tok);
    if (!token_ok(tok)) {
        reply(fd, "403 Forbidden", "missing or wrong token (X-Token header or ?token=)\n");
        close(fd);
        return;
    }
    if (!stream_enabled()) {
        reply(fd, "503 Service Unavailable", "the mic stream is turned off in the device's settings\n");
        close(fd);
        return;
    }
    float db = STREAM_GAIN_DB;
    char g[12];
    if (httpd_query_key_value(query, "gain", g, sizeof g) == ESP_OK) db = strtof(g, NULL);
    if (!(db >= 0)) db = 0;
    if (db > 48) db = 48;

    bool full;
    portENTER_CRITICAL(&s_mux);
    full = s_listeners >= STREAM_MAX;
    if (!full) s_listeners++;
    portEXIT_CRITICAL(&s_mux);
    if (full) {
        reply(fd, "503 Service Unavailable", "too many listeners\n");
        close(fd);
        return;
    }
    client_t *c = malloc(sizeof *c);
    if (c) *c = (client_t){ .fd = fd, .gain = powf(10.0f, db / 20.0f) };
    if (!c || xTaskCreate(client_task, "stream_tx", 4096, c, 4, NULL) != pdPASS) {
        free(c);
        close(fd);
        portENTER_CRITICAL(&s_mux);
        s_listeners--;
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    char ip[16];
    inet_ntoa_r(peer->sin_addr, ip, sizeof ip);
    ESP_LOGI(TAG, "listener %s, gain %.0f dB (%d listening)", ip, db, stream_listeners());
}

static void server_task(void *arg)
{
    int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = {
        .sin_family = AF_INET,
        .sin_port = htons(STREAM_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (ls < 0 || bind(ls, (struct sockaddr *)&a, sizeof a) != 0 || listen(ls, 2) != 0) {
        ESP_LOGE(TAG, "cannot listen on port %d", STREAM_PORT);
        if (ls >= 0) close(ls);
        vTaskDelete(NULL);
    }
    ESP_LOGI(TAG, "mic stream on port %d", STREAM_PORT);
    for (;;) {
        struct sockaddr_in peer;
        socklen_t pl = sizeof peer;
        int fd = accept(ls, (struct sockaddr *)&peer, &pl);
        if (fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        handle(fd, &peer);
    }
}

esp_err_t stream_start(void)
{
    s_ring = heap_caps_calloc(RING, sizeof(float), MALLOC_CAP_SPIRAM);
    if (!s_ring) return ESP_ERR_NO_MEM;
    if (xTaskCreate(server_task, "stream", 4096, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
