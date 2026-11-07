# HTTP API

The device serves everything on port 80.
- **Reads** need nothing.
- **Changes** need the access token from `firmware/main/secrets.h` in an `X-Token` header. Without
  it they answer `403`.
- **Pairing** gets a browser the token without typing it: the device shows a code on its screens,
  and pressing its power button (in person) hands the token over.

## Settings page

| Method | Path | Body or query | Returns |
|---|---|---|---|
| GET | `/` | | the settings page (gzip) |
| GET | `/api/status` | | live state: the mode shown (`mode`), the tempo finder's reading (`tempo_bpm`), firmware, slot, uptime, IP, RSSI, heap, time, weather, each source's latest stats, the microphone's level (`mic.dbfs`, `mic.dba`), render statistics |
| GET | `/api/settings` | | the settings (below), plus the allowed values (`panels`, `led_modes`) |
| POST | `/api/settings` | JSON, any subset of the settings | the settings now in effect, or `400` with the reason |
| GET | `/api/screen?s=0..4` | | what that screen shows now: 128×128 big‑endian RGB565 (32768 bytes). `firmware/tools/screenshot.py` saves all five as a PNG |
| POST | `/api/beep` | `?p=N&v=0..100`, or `?hz=50..8000&ms=..&v=..` | plays a sound: 0 test, 1 hot, 2 link lost, 3 link restored, 4 mains lost, 5 key, 6 tick, 7 accent, 8 timer done, 9 mode, 10 battery low, 11 start-up, 12 hourly chime; or one plain tone |
| POST | `/api/pair` | | asks on the device's screens to pair a browser; returns `{"id", "code"}` (no token needed; one request at a time, at most one every 5 s, 60 s to answer) |
| GET | `/api/pair?id=N` | | `{"state": "waiting" \| "allowed" \| "refused"}`; once the owner pressed power on the device, `allowed` comes with the token, once |
| POST | `/api/button` | `?k=light\|left\|power\|right\|mode&e=short\|long\|down` | presses a button from afar (as if on the device) |
| GET | `/api/pcm` | | while the tuner runs: its last 2048 samples (16 kHz, float32), for testing the pitch detection |
| POST | `/api/leds` | `?mode=auto\|groups\|off\|fill\|one\|range\|keys` with `c=RRGGBB`, `n`, `i`, `from`, `to`, `b=0..255` | LED tests. A test holds the effects for 2 minutes; `mode=auto` hands the LEDs back |

### Settings

```json
{
  "sources": ["http://192.168.1.20:8765/stats", "http://192.168.1.30:8765/stats"],
  "poll_ms": 1000,
  "tz": "CET-1CEST,M3.5.0,M10.5.0/3",
  "lat": 52.52, "lon": 13.405,
  "hour24": true,
  "brightness": 100, "night_start": "23:00", "night_end": "07:00", "night_brightness": 40,
  "layout": ["lab", "cpu", "nav", "gpu", "audio"],
  "buzzer": true, "buzzer_volume": 40, "quiet_night": true,
  "alert_cpu_temp": 90, "alert_gpu_temp": 85, "alert_vram_temp": 95,
  "led_mode": "status", "led_brightness": 40,
  "key_beep": true,
  "chime": false, "alarm_on": true, "alarm_time": "07:00", "alarm_days": 31,
  "tuner_a4": 440, "metro_bpm": 100, "timer_min": 5,
  "world_names": ["LONDON", "NEW YORK"], "world_tz": ["GMT0BST,M3.5.0/1,M10.5.0", "EST5EDT,M3.2.0,M11.1.0"]
}
```

| Field | Meaning |
|---|---|
| `sources` | `sources[0]` feeds the CPU and GPU panels, and `sources[1]` feeds the home‑lab panel. An empty string turns that source off. |
| `tz` | a POSIX TZ string |
| `lat`, `lon` | the location for the weather. `0, 0` means not set. |
| `night_start`, `night_end` | `HH:MM`. Empty means no night window. During the window the screens and LEDs dim and the buzzer stays quiet. |
| `layout` | the five screens from left to right, each one of `lab`, `cpu`, `nav` (clock), `gpu`, `audio`, `off` |
| `led_mode` | one of `off`, `glow`, `status`, `music` |
| `world_names`, `world_tz` | the World mode's two other places: a name (up to 15 letters) and a POSIX time zone each. Daylight saving works with the `Mm.w.d` rules. |
| `tuner_a4`, `metro_bpm`, `timer_min` | what the tuner, metronome and timer were left at (saved 2.5 s after the last button press) |
| `alarm_days` | which days the alarm rings: bit 0 = Monday … bit 6 = Sunday (31 = weekdays, 96 = weekends, 127 = every day) |

## Maintenance

| Method | Path | Body | Does |
|---|---|---|---|
| GET | `/status` | | firmware, slot, uptime, RSSI, IP, free heap, and why it last restarted (`reset`) |
| POST | `/ota` | the app image (`.bin`) | writes it to the slot that isn't running, then boots it |
| POST | `/switch` | | boots the other slot |
| POST | `/reboot` | | reboots |
| POST | `/frame?s=N` | 128×128 big‑endian RGB565 (32768 bytes) | shows a raw frame on screen N (0..4); for tests |
| POST | `/backlight?d=0..255` | | sets the backlight directly |

`firmware/tools/push.py` wraps `/status`, `/ota`, `/switch` and `/reboot`.

## Stats agent

`GET http://<machine>:8765/stats` returns the following. Fields a machine doesn't have are `null`.

```json
{
  "name": "PC", "host": "...", "os": "windows|linux", "ts": 1730900000, "uptime_s": 12345,
  "cpu":  {"model": "...", "threads": 16, "load": 12.3, "temp": 49.0, "temp_src": "SoC|Tctl|..."},
  "ram":  {"used": 0, "total": 0},
  "disks": [{"name": "C:", "used": 0, "total": 0}],
  "gpu":  {"name": "...", "load": 10.0, "vram_used": 0, "vram_total": 0,
           "temp": 40.0, "hotspot": 42.0, "vram_temp": 60.0, "power_w": 15.0},
  "battery": {"percent": 95, "charging": false, "ac": true},
  "services": [{"name": "web", "port": 8080, "up": true, "ms": 1.2}]
}
```

Anything that serves this shape works as a source. For example, you could write a small script
for a NAS or a router.
