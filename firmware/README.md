# Pentadeck firmware

An ESP‑IDF 5.5 project for the Divoom Times Gate: an ESP32‑S3 with 8 MB flash and 8 MB PSRAM.

## Build

1. Install [ESP‑IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/)
   and open its shell (`export.sh`, `export.ps1` or the ESP‑IDF command prompt).
2. Copy `main/secrets.example.h` to `main/secrets.h` and fill it in:
   - your Wi‑Fi network and password (2.4 GHz);
   - an access token. The settings page and `tools/push.py` send it with every change. Use a long
     random string.
3. Build:
   ```
   cd firmware
   idf.py set-target esp32s3      # once
   idf.py build
   ```
   The output is `build/pentadeck.bin`, with a copy in `build/bin/pentadeck.bin`.

The settings page is built separately (see [../webui](../webui/README.md)) into
`main/web/index.html.gz`, which is committed, so a firmware build doesn't need Node.

## Install

The first install is done over a serial adapter, once. After that, updates go over Wi‑Fi:
```
python tools/push.py <device-ip> status
python tools/push.py <device-ip> ota build/bin/pentadeck.bin
```
You can also upload `pentadeck.bin` from the settings page (Firmware card).
See [../docs/FLASHING.md](../docs/FLASHING.md) for the full procedure, and for how to go back to the
original firmware.

## Layout

All the C code is one ESP‑IDF component, `main/`, split into folders by role. Includes are written
from `main/`, e.g. `#include "drivers/display.h"`.

| Path | Role |
|---|---|
| `main/main.c`, `main/app.h` | Wi‑Fi, the start‑up sequence, the maintenance API (`/status`, `/ota`, `/switch`, `/reboot`, test endpoints) |
| `main/secrets.h` | your Wi‑Fi details and access token (not in git; copy `secrets.example.h`) |
| `main/drivers/` | the hardware: `display` (the five ST7735S panels on one 8‑bit i80 bus, chip selects driven by hand), `buttons` (the resistor ladder), `buzzer`, `leds` (the WS2812 chain over RMT with DMA, the buttons‑board LEDs), `mic` (PDM microphone: FFT, the equaliser bands, A‑weighted level, pitch, tempo, chroma) |
| `main/gfx/` | `hud`: a small software renderer (shapes, pre‑rendered fonts, images) into RGB565 frame buffers |
| `main/gfx/assets/` | fonts and images, **generated** by `tools/gen_fonts.py` and `tools/gen_assets.py` |
| `main/ui/` | `ui`: the cockpit panels and the 30 fps render loop; `ledfx`: what the LED chain shows |
| `main/modes/` | the Casio‑style modes: `modes` runs them (and SETUP, toasts, ringing, pairing); `mode_music`, `mode_time` and `mode_fx` hold them; `mode_kit.h` is what they share |
| `main/net/` | `netdata` (polls the stats agents; SNTP time; Open‑Meteo weather), `web` (the settings page and `/api/*`), `stream` (the microphone as a live WAV stream on port 8081, for a PC to use as a mic) |
| `main/core/` | `settings` (NVS namespace `tg`, JSON in and out, validation), `power` (low power when nobody is at the desk), `alerts` (when to sound the buzzer), `tzlite` (other places' local time from a POSIX TZ string) |
| `main/web/index.html.gz` | the settings page, built by `../webui` and embedded in the image |
| `tools/push.py` | status, OTA, slot switch and reboot over Wi‑Fi |
| `tools/screenshot.py` | saves what the five screens show as one PNG |
| `tools/make_otadata.py` | builds an `otadata` image that boots a chosen slot (first install) |

To regenerate the assets (needs Pillow; run from the repo root):
```
python firmware/tools/gen_fonts.py
python firmware/tools/gen_assets.py
```

The partition table is the stock one:

```
nvs 0x9000 · otadata 0xd000 · phy_init 0xf000 · ota_0 0x10000 · ota_1 0x310000 · storage 0x610000
```

So the original firmware in `ota_0`, its settings and its files stay where they are until you
overwrite them. Wi‑Fi credentials are kept in RAM only and never written to NVS. Hardware details
are in [../docs/HARDWARE.md](../docs/HARDWARE.md).
