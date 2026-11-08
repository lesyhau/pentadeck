# Divoom Times Gate — hardware map

Confirmed on the device on 2026-11-07 (v0.2 test pattern: order, orientation and colours correct).
Pins were read from Divoom's stock firmware v4000194 (`esp_lcd` init code and LEDC configs).

## MCU
- ESP32-S3-WROOM-1U (QFN56, rev v0.2), 8 MB embedded PSRAM (octal, AP), external antenna (u.FL)
- Flash GD25Q64 8 MB, DIO 80 MHz, VDD_SPI 3.3 V (eFuse)
- eFuses: no secure boot, no flash encryption, download mode enabled, JTAG enabled
- Stock bootloader: ESP-IDF 4.4.2 (boots ESP-IDF 5.5 apps fine)

## Screens: 5 × 1.44" 128×128 TFT (WA44BA081C-20Z, FPC FP-A44BA081C), controller ST7735S
| Signal | GPIO |
|---|---|
| i80 D0..D7 | 5, 16, 4, 15, 7, 6, 2, 1 |
| WR (pixel clock) | 38 (12 MHz) |
| DC | 42 (high = data) |
| CS screen 0..4 (left → right) | 40, 46, 10, 18, 17 |
| RESET (shared) | 41 |
| Backlight | 39, LEDC PWM 5 kHz, 8-bit, 255 = full |

**Chip selects must be driven by hand** (push-pull, high = deselected): ESP-IDF 5.5's per-device i80 CS only
*releases* the previous CS when switching devices, and GPIO46 has a pull-down (strapping pin + board resistor),
so screen 1 then stays selected and receives every frame. The firmware uses one i80 IO with `cs_gpio_num = -1`
and initialises all five panels at once with every CS low (as the stock firmware does).

Wi-Fi: the u.FL antenna lead must be connected (−31 dBm vs −88 dBm without it). Full 32 KB frames over HTTP
reach ~12.6 frames/s (~400 KB/s) with the antenna.

ST7735S init (Divoom's): 11, wait · B1 05 3C 3C · B2 05 3C 3C · B3 05 3C 3C 05 3C 3C · B4 03 · C0 A7 07 · C1 E9 · C2 0D 00 ·
C3 8A 26 · C4 8A EA · C5 01 · 36 C8 · E0 07 0E 00 1C 20 17 0F 13 11 12 12 38 00 09 07 10 ·
E1 0E 0C 07 0E 13 0A 02 06 05 09 1B 2F 00 0A 04 10 · 3A 55 · wait · 29 · 21 (inversion on).
Address window offset (gap): x + 2, y + 3. Pixels: RGB565, high byte first.

## Microphone (buttons board, ribbon at J10)
Standard I2S MEMS mic (confirmed 2026-11-07): **BCK GPIO19, WS GPIO3, DATA GPIO20**, 32-bit, 32 kHz, left slot.
The stock firmware also supports a PDM variant (CLK 19, DATA 20) and probes which one is fitted; this unit has I2S.
GPIO19/20 are the chip's USB D-/D+: release the USB pad (`usb_serial_jtag_ll_phy_enable_pad(false)`) and
configure GPIO20 with the pull-up disabled (clears the USB D+ pull-up) before starting I2S. The USB-Serial-JTAG
console must be off (`CONFIG_ESP_CONSOLE_SECONDARY_NONE`, `CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=n`).
Quiet room ≈ −62…−66 dBFS (RMS).

## Other peripherals
| Function | GPIO |
|---|---|
| Buzzer | 9, LEDC PWM (50 % duty, frequency = tone) |
| SD-NAND (CSNP4GCR01, ~512 MB, `/sdcard`) | likely 4-bit SDMMC: 48 (CLK?), 47, 13, 14, 21, 45 |
| UART0 console | TXD0 43 (module pin 37), RXD0 44 (module pin 36) |
| Buttons-board LEDs | **44** (= RXD0): plain on/off (stock `divoom_screen_ctrl_key_pad` → `gpio_set_level(44, on)`); we PWM it (LEDC timer 3 / ch 3), so console input is gone — ROM download mode unaffected. Confirmed lit 2026-11-07 |
| Boot (IO0) | SW1 pads (unpopulated) — short to GND at power-up for download mode |
| USB D-/D+ | 19 / 20 — wired to the mic (I2S BCK / DATA), so the USB port is power-only |
| RGB LEDs (side strips, back, buttons board) | **0**: one WS2812-style chain. Stock: `divoom_spi_OnelineLED_init` = SPI3, MOSI GPIO0, 8 MHz, max transfer 1440 B. We drive it with RMT (10 MHz, GRB). **45 LEDs: left side board 0–19, back light 20–24, right side board 25–44.** The side boards ("TimesGate LED-R", alike on both sides) carry 20 LEDs each, chained in the order of their printed names: LED2, 4, 6–11 (back edge, bottom→top), LED12–22 (front edge, top→bottom), LED23 (bottom, middle). Mapped 2026-11-08 with the board out. Buttons-board LEDs are NOT on this chain |
| Buttons (top, left to right: LIGHT, left, power, right, MODE) | **8** (ADC1 channel 7): one resistor ladder, 12 dB attenuation. Readings: none 4095, left ~15, LIGHT ~340, power ~695, MODE ~1045, right ~1435 (one button at a time) |
| Unknown / to check | 11, 12 (both float around 500 on the ADC; perhaps the ambient light sensor) |

## Flash layout (stock, kept)
nvs 0x9000 · otadata 0xd000 · phy_init 0xf000 · ota_0 0x10000 (3 MB) · ota_1 0x310000 (3 MB) · storage (spiffs) 0x610000 (1.9 MB)
