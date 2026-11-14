# HTTP API

The device serves everything on port 80, except the microphone stream (port 8081, [below](#microphone-stream)).
- **Reads** need nothing.
- **Changes** need the access token from `firmware/main/secrets.h` in an `X-Token` header. Without
  it they answer `403`.
- **Pairing** gets a browser the token without typing it: the device shows a code on its screens,
  and pressing its power button (in person) hands the token over.

## Settings page

| Method | Path | Body or query | Returns |
|---|---|---|---|
| GET | `/` | | the settings page (gzip) |
| GET | `/api/status` | | live state: the mode shown (`mode`), the tempo finder's reading (`tempo_bpm`), firmware, slot, uptime, IP, RSSI, heap, time, weather, each source's latest stats, the microphone's level (`mic.dbfs`, `mic.dba`) and how many are streaming it (`mic.listeners`), the day's sound dose (`hearing.dose_pct`, `hearing.leq_dba`, [below](#sound-dose)), the low‑power state (`power.state`: `full` or `idle`; `power.last_sign`: what last showed someone, e.g. `sound`, `button`, `pc input`; `power.quiet_s`), render statistics |
| GET | `/api/settings` | | the settings (below), plus the allowed values (`panels`, `led_effects`, `led_modes`, `audio_views`, `viz_palettes`, `clap_actions`) |
| POST | `/api/settings` | JSON, any subset of the settings | the settings now in effect, or `400` with the reason |
| GET | `/api/screen?s=0..4` | | what that screen shows now: 128×128 big‑endian RGB565 (32768 bytes). `firmware/tools/screenshot.py` saves all five as a PNG |
| POST | `/api/beep` | `?p=N&v=0..100`, or `?hz=50..8000&ms=..&v=..` | plays a sound: 0 test, 1 hot, 2 link lost, 3 link restored, 4 mains lost, 5 key, 6 tick, 7 accent, 8 timer done, 9 mode, 10 battery low, 11 start-up, 12 hourly chime; or one plain tone |
| POST | `/api/pair` | | asks on the device's screens to pair a browser; returns `{"id", "code"}` (no token needed; one request at a time, at most one every 5 s, 60 s to answer) |
| GET | `/api/pair?id=N` | | `{"state": "waiting" \| "allowed" \| "refused"}`; once the owner pressed power on the device, `allowed` comes with the token, once |
| POST | `/api/button` | `?k=light\|left\|power\|right\|mode&e=short\|long\|down` | presses a button from afar (as if on the device) |
| GET | `/api/pcm` | | while the tuner runs: its last 2048 samples (16 kHz, float32), for testing the pitch detection |
| POST | `/api/power` | `?state=idle\|wake` | low power now (until the next sign of someone), or back to full power |
| POST | `/api/notify` | JSON `{"title", "text", "seconds", "sound", "color"}` | a message on the screens, [below](#notifications) |
| GET | `/api/spectrum` | | the AUDIO screen's 16 bands now, bass first, each 0..100, and the level: `{"bands": [...], "dba": 41.2}` |
| GET | `/api/history` | | the last 24 hours, a value a minute, [below](#history) |
| GET | `/api/sounds` | | the learned sounds, [below](#learned-sounds) |
| POST | `/api/sounds` | `?slot=0..2&learn=NAME`, `&on=0\|1` or `&forget=1` | learns, turns on or off, or forgets a sound; returns what GET does |
| GET | `/api/emblem` | | the clock image: 36×36 big‑endian RGB565 (2592 bytes), or `404` while the logo shows |
| POST | `/api/emblem` | those 2592 bytes, or `?clear=1` | puts an image on the clock screen in place of the spinning logo, or brings the logo back |
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
  "led_left": "cpu", "led_back": "lab", "led_right": "gpu", "led_brightness": 40,
  "led_left_color": "#2a8cb2", "led_back_color": "#2a8cb2", "led_right_color": "#2a8cb2",
  "key_beep": true,
  "chime": false, "alarm_on": true, "alarm_time": "07:00", "alarm_days": 31,
  "tuner_a4": 440, "metro_bpm": 100, "timer_min": 5,
  "idle_min": 10, "idle_brightness": 10,
  "mic_stream": true,
  "audio_view": "eq",
  "accent": "#2a8cb2", "viz_palette": "accent",
  "clap": "off", "noise_alert": true,
  "world_names": ["LONDON", "NEW YORK"], "world_tz": ["GMT0BST,M3.5.0/1,M10.5.0", "EST5EDT,M3.2.0,M11.1.0"],
  "world_order": [1, 0, 2]
}
```

| Field | Meaning |
|---|---|
| `sources` | `sources[0]` feeds the CPU and GPU panels, and `sources[1]` feeds the home‑lab panel. An empty string turns that source off. |
| `tz` | a POSIX TZ string |
| `lat`, `lon` | the location for the weather. `0, 0` means not set. |
| `night_start`, `night_end` | `HH:MM`. Empty means no night window. During the window the screens and LEDs dim and the buzzer stays quiet. |
| `layout` | the five screens from left to right, each one of `lab`, `cpu`, `nav` (clock), `gpu`, `audio`, `off` |
| `led_left`, `led_back`, `led_right` | each LED zone's effect: `off`, `glow` (steady), `breathe` (fading in and out), `wave` (a band of light running round), `comet` (a bright dot with a fading tail), `sparkle` (random twinkles), `pulse` (as bright as the room is loud), `fire` (flickering embers), `rainbow`, `spectrum` (the music along the strip, bass at one end), `cpu` or `gpu` (the PC's load as a meter; orange from 60 % load or 75 °C, red from 80 % or 88 °C, blinking on a load alert), `lab` (the home lab's health), `music` (a loudness meter). The buttons board's LEDs stay on unless all three are `off` |
| `led_left_color`, `led_back_color`, `led_right_color` | each zone's colour, `#rrggbb`, or `accent` (the default) to follow the accent colour, changing when it does: what the effects show, and the "all is well" colour of the meters. `fire`, `rainbow` and `spectrum` bring their own colours |
| `led_mode` | a preset for all three zones: `off`, `glow`, `status` (`cpu`, `lab`, `gpu`) or `music`. Read back as the preset the zones match, or `custom`. Zones sent beside it win. The device's setup menu (LED MODE) and a long LIGHT press work with the presets |
| `world_names`, `world_tz` | the World mode's two other places: a name (up to 15 letters) and a POSIX time zone each. Daylight saving works with the `Mm.w.d` rules. |
| `world_order` | which clock the World mode shows on screens 2, 3 and 4: `0` = home, `1` and `2` = the two places, each once. `[1, 0, 2]` puts home in the middle. The AWAKE planner lists them top to bottom in the same order |
| `tuner_a4`, `metro_bpm`, `timer_min` | what the tuner, metronome and timer were left at (saved 2.5 s after the last button press) |
| `idle_min` | low power after this many minutes with no sign of anyone (no sound above the room's background, no button, no keyboard or mouse on a PC whose agent reports `input_idle_s`), 0..120; 0 = never. Day or night doesn't matter. See [Low power](#low-power). |
| `mic_stream` | PCs may stream the mic (port 8081). Off refuses new streams and cuts open ones; the device's own uses of the mic (equaliser, tuner, low‑power wake‑up) carry on |
| `audio_view` | the AUDIO screen: `eq` (the 16‑band equaliser), `vu` (a needle VU meter; 0 VU is where the LED meters turn orange), or a visual effect: `mirror` (the spectrum mirrored up and down), `scope` (the waveform), `waterfall` (the spectrum over time), `ripple` (rings sent out by the beat) or `radial` (the spectrum round a circle). The list is in `audio_views`. Also in the device's setup menu as AUDIO VIEW |
| `accent` | the accent colour, `#rrggbb` (teal `#2a8cb2` by default): the screens' frames, headings, bars and logo, and the settings page, all in its hue and saturation (their lightness stays). The setup menu's ACCENT steps through presets |
| `viz_palette` | the visual effects' colours, on the AUDIO screen and in the VISUAL mode: `accent`, `rainbow` or `fire` (the list is in `viz_palettes`). Power changes it in the VISUAL mode |
| `clap` | what a double clap does: `off`, `leds` (the LEDs on or off), `light` (the screens on or off), `mode` (the next mode) or `dash` (back to the dashboard). Two sharp claps 150–600 ms apart, then quiet; a third clap makes it a rhythm and cancels it. The device's own beeps never count |
| `noise_alert` | a card on the screens when the day's sound dose passes 100 % ([below](#sound-dose)) |
| `idle_brightness` | the screens' level in low power, 0..100 % (never brighter than the normal or night level) |
| `alarm_days` | which days the alarm rings: bit 0 = Monday … bit 6 = Sunday (31 = weekdays, 96 = weekends, 127 = every day) |

## Microphone stream

`GET http://<device>:8081/` streams the microphone live: an endless WAV of 16‑bit mono PCM at 32 kHz
(64 KB/s), with the DC offset removed. **It needs the token**, in an `X-Token` header or as
`?token=…` (for players that can't send headers).

| Query | Meaning |
|---|---|
| `gain=0..48` | dB of gain (default 30: normal speech at a metre then peaks around −20 dBFS) |
| `token=…` | the access token, if not sent as `X-Token` |

Any web page may read the stream (it sends `Access-Control-Allow-Origin: *`); the token still guards it.
The settings page's **Listen** button plays it this way.

At most two listeners at once (a third gets `503`). While anyone listens, the AUDIO screen's title
turns into a red **MIC LIVE**; when no AUDIO screen is showing (a mode, or AUDIO off in the layout),
the middle screen shows a red **MIC ●** instead. With the `mic_stream` setting off, the stream answers
`503` and anyone listening is cut off. Audio arrives in 32 ms blocks, so it lags the room by about 50 ms before the network.

- **As a Windows microphone:** run the stats agent with `--mic` (see [agent/README.md](../agent/README.md#microphone)).
- **VLC / OBS (Media Source) / ffplay:** open `http://<device>:8081/?token=…`.
- **ffmpeg:** `ffmpeg -headers "X-Token: …" -i http://<device>:8081/ out.wav`

## Notifications

`POST /api/notify` puts a card over the middle screen and pulses the LEDs in its colour. Any button on
the device puts it away; that press does nothing else.

```json
{ "title": "DINNER", "text": "Ready in five minutes", "seconds": 30, "sound": "chime", "color": "#ffb000" }
```

| Field | Meaning |
|---|---|
| `title` | up to 23 letters, in capitals on the screen. `title` or `text` is needed |
| `text` | up to 95 characters, wrapped over a few lines |
| `seconds` | how long it stays, 10 by default; `0` keeps it until a button is pressed |
| `sound` | `chime` (default), `none`, `test`, `up`, `down`, `warn`, `power`, `battery`, `alarm`, `boot` or `key` |
| `color` | `#rrggbb`, the accent by default |

```sh
curl -X POST -H "X-Token: …" -d '{"title":"BUILD","text":"main passed"}' http://<device>/api/notify
```

## Sound dose

The device counts the day's sound as a hearing‑safety meter does (NIOSH): 85 dBA for 8 hours is
100 %, and every 3 dB more halves the time (88 dBA for 4 hours is 100 % too). `hearing.dose_pct` is
today's dose and `hearing.leq_dba` today's average level (Leq). It starts again at local midnight,
survives a restart, and not a power cut. The mic is not calibrated, so take it as a guide within a
few dB. With `noise_alert` on, a card on the screens says when the dose passes 100 %, once a day.

## Learned sounds

Up to three sounds the screens announce when they're heard: a doorbell, a kettle, a phone. Each is
learned by listening to it for 4 seconds. The device keeps its shape over 32 bands from 300 Hz to
10 kHz, and hears it again when the room is well above its background and the shape matches for
about 0.1 s. After it is heard, a sound is ignored for 8 s.

`GET /api/sounds`:

```json
{
  "slots": [{ "used": true, "on": true, "name": "DOORBELL" }, { "used": false, "on": false, "name": "" }, { "used": false, "on": false, "name": "" }],
  "learning": -1, "left_ms": 0, "result": "learned"
}
```

`learning` is the slot being learned (or −1) and `left_ms` what is left of its 4 s. `result` says how the
last learning ended: `learned`, `nothing` (nothing loud enough was heard) or `none` (none yet). Names
are up to 15 letters, URL‑encoded in `learn=`.

## History

`GET /api/history` returns a value a minute for up to the last 24 hours, oldest first, kept in PSRAM
from when the device starts (a restart starts it again). `null` is a minute with nothing known.

```json
{
  "step_s": 60, "count": 1440, "last": 1794729600,
  "series": { "pc_cpu": [12.5, ...], "pc_cpu_temp": [...], "pc_gpu": [...], "pc_gpu_temp": [...],
              "lab_cpu": [...], "lab_cpu_temp": [...], "dba": [...] }
}
```

`last` is when the newest minute ended (Unix time; `0` while the clock is not set yet). Loads are
in %, temperatures in °C, and `dba` is the minute's average level.

## Low power

When nobody seems to be at the desk for `idle_min` minutes, the device goes into low power:

| | Full power | Low power |
|---|---|---|
| Screens | the set brightness, 30 fps | fade to `idle_brightness`, 4 fps |
| LEDs | each zone's effect | off |
| Microphone | 32 kHz, spectrum, pitch, tempo, chords | 8 kHz, loudness only (enough to hear someone) |
| Wi‑Fi | always awake | modem sleep (replies take up to a few hundred ms longer) |
| Agents | polled every `poll_ms` | polled every 5 s |
| CPU | 240 MHz | may drop to 80 MHz |

It wakes on any of these:
- **Sound** clearly above the room's background (10 dB, three times within 5 s). The background is relearned all the time, so a fan or an air conditioner that keeps running doesn't count.
- **A button.** The press that wakes it does nothing else.
- **Keyboard or mouse** on a PC whose agent reports `input_idle_s` (within the agent's poll).
- An alarm or timer ringing, a pairing request, or `POST /api/power?state=wake`.

Alerts still sound in low power. While a PC streams the mic, the mic and Wi‑Fi stay at full speed.

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
  "input_idle_s": 42,
  "cpu":  {"model": "...", "threads": 16, "load": 12.3, "temp": 49.0, "temp_src": "SoC|Tctl|..."},
  "ram":  {"used": 0, "total": 0},
  "disks": [{"name": "C:", "used": 0, "total": 0}],
  "gpu":  {"name": "...", "load": 10.0, "vram_used": 0, "vram_total": 0,
           "temp": 40.0, "hotspot": 42.0, "vram_temp": 60.0, "power_w": 15.0},
  "battery": {"percent": 95, "charging": false, "ac": true},
  "services": [{"name": "web", "port": 8080, "up": true, "ms": 1.2}]
}
```

`input_idle_s` is the number of seconds since the last keyboard or mouse input on that machine, a
sign that someone is at the desk (the device's low power counts the last input like a sound). On
Linux it comes from systemd‑logind and is coarse: once the desktop marks the session idle, the
seconds since; `null` before that, since many desktops never set the mark. It is `null` on a
headless machine, or when the agent can't see the user's session (on Windows, run it at logon as the installer does, not as a
service).

Anything that serves this shape works as a source. For example, you could write a small script
for a NAS or a router.
