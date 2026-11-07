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
   The output is `build/pentadeck.bin`.

The settings page is built separately (see [../webui](../webui/README.md)) into
`main/web/index.html.gz`, which is committed, so a firmware build doesn't need Node.

## Install

The first install is done over a serial adapter, once. After that, updates go over Wi‑Fi:
```
python tools/push.py <device-ip> status
python tools/push.py <device-ip> ota build/pentadeck.bin
```
You can also upload `pentadeck.bin` from the settings page (Firmware card).
See [../docs/FLASHING.md](../docs/FLASHING.md) for the full procedure, and for how to go back to the
original firmware.

## Layout

| File | Role |
|---|---|
| `main/main.c` | Wi‑Fi, the start‑up sequence, the maintenance API (`/status`, `/ota`, `/switch`, `/reboot`, test endpoints) |
| `main/display.c` | the five ST7735S panels on one 8‑bit i80 bus, chip selects driven by hand |
| `main/hud.c` | a small software renderer (shapes, pre‑rendered fonts, images) into RGB565 frame buffers |
| `main/ui.c` | the cockpit panels and the 30 fps render loop |
| `main/netdata.c` | polls the stats agents; SNTP time; Open‑Meteo weather |
| `main/mic.c` | I2S microphone: FFT, the equaliser bands, A‑weighted level |
| `main/leds.c`, `main/ledfx.c` | the WS2812 chain (RMT with DMA) and its effects; the buttons‑board LEDs |
| `main/buzzer.c`, `main/alerts.c` | alert sounds, and when to play them |
| `main/settings.c` | settings in NVS (namespace `tg`), JSON in and out, validation |
| `main/web.c` | the settings page and `/api/*` |
| `main/assets/` | fonts and images, **generated** by `tools/gen_fonts.py` and `tools/gen_assets.py` |
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
