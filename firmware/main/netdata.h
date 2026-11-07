#pragma once
/* Everything the device fetches over the network: the stats agents, the weather, and the time. */
#include <stdbool.h>
#include <stdint.h>

#define HOST_MAX_DISKS    3
#define HOST_MAX_SERVICES 6

typedef struct {
    bool ok;                  /* last poll succeeded and is recent (< 5 s) */
    bool ever;                /* we have received data at least once */
    int64_t age_ms;           /* time since the last good poll */
    char name[16], host[32], os[8];
    char cpu_model[48];
    int threads;
    float cpu_load;           /* % */
    float cpu_temp;           /* °C, NAN if unknown */
    float cpu_hot;            /* hottest CPU sensor ("cpu.hotspot"), NAN if the agent has none */
    float ram_temp;           /* DIMM temperature ("ram.temp"), NAN if the agent has none */
    char temp_src[10];
    uint64_t ram_used, ram_total;
    int n_disks;
    struct { char name[12]; uint64_t used, total; } disks[HOST_MAX_DISKS];
    bool has_gpu;
    struct {
        char name[32];
        float load, temp, hotspot, vram_temp;   /* NAN where unknown */
        uint64_t vram_used, vram_total;
    } gpu;
    bool has_battery;
    int bat_pct;
    bool charging, ac;
    int n_services;
    struct { char name[12]; bool up; float ms; } services[HOST_MAX_SERVICES];
    uint32_t uptime_s;
} host_stats_t;

typedef struct {
    bool ok;
    float temp_c;
    float humidity;           /* relative humidity, % */
    int code;                 /* WMO weather code */
    bool is_day;
    int64_t age_ms;
} weather_t;

void netdata_start(void);                       /* call once Wi-Fi is up */
void netdata_get_host(int i, host_stats_t *out);
void netdata_get_weather(weather_t *out);
bool netdata_time_valid(void);                  /* SNTP has set the clock */
void netdata_apply_tz(void);                    /* re-read the time zone from settings */
