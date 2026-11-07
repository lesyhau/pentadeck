# Installing Pentadeck

The Times Gate's USB‑C port only supplies power: the chip's USB pins are wired to the microphone.
So the first install goes through the ESP32‑S3 module's UART, using a 3.3 V USB‑serial adapter such
as a CP2102. After that, every update goes over Wi‑Fi.

The procedure writes only the second app slot (`ota_1`) and the boot selector (`otadata`). The
bootloader, the original app in `ota_0`, its settings and its files are left as they are.

## You need

- A 3.3 V USB‑serial adapter. **Not 5 V.**
- Thin wires, and a way to hold them on the module's pads: a soldering iron, or a steady hand with
  probe clips.
- `esptool` (`pip install esptool`, or the copy that comes with ESP‑IDF).
- Pentadeck built, with `firmware/build/pentadeck.bin` present (see [../firmware/README.md](../firmware/README.md)).

## 1. Open the device and wire the UART

Open the case. The ESP32‑S3‑WROOM‑1U module is on the main board.

| Adapter | Module |
|---|---|
| RX | pin 37, TXD0 (GPIO43) |
| TX | pin 36, RXD0 (GPIO44) |
| GND | the module's shield, or any ground |

Leave the adapter's 3.3 V/VCC pin unconnected. The device is powered from its own USB‑C supply.

## 2. Enter download mode

The board has unpopulated **SW1** pads, which connect IO0 to GND. Short them while plugging in the
device's power, then release.

Check that the chip answers:
```
python -m esptool --chip esp32s3 --port COM5 --before no-reset --after no-reset chip-id
```
Use your adapter's port, for example `/dev/ttyUSB0` on Linux.

## 3. Back up the whole flash (do not skip)

```
python -m esptool --chip esp32s3 --port COM5 --baud 115200 --before no-reset --after no-reset ^
    read-flash 0 0x800000 times-gate-backup.bin
python -m esptool --chip esp32s3 --port COM5 --before no-reset --after no-reset verify-flash 0 times-gate-backup.bin
```
This takes about 12 minutes at 115200 baud.
- **Keep this file private.** It holds your Wi‑Fi settings, your device's identity, and the
  vendor's firmware.
- It is the only way back to the exact original state.

## 4. Write Pentadeck into the second slot

Make an `otadata` image that boots `ota_1`, from your backup:
```
python firmware/tools/make_otadata.py times-gate-backup.bin 1 otadata_ota1.bin
```

Then write both, staying in download mode (re‑enter it if the device restarted):
```
python -m esptool --chip esp32s3 --port COM5 --baud 115200 --before no-reset --after no-reset ^
    write-flash 0x310000 firmware/build/pentadeck.bin 0xd000 otadata_ota1.bin
```

**Disconnect the adapter's TX wire** (the device drives GPIO44 for the buttons‑board LEDs), then
power‑cycle the device.

The screens light up and the device joins your Wi‑Fi.
- **Find its IP:** check your router's client list for `pentadeck`, or read the serial log at 115200 baud.
- **No Wi‑Fi:** if it can't join within 3 minutes, it boots the other slot (the original firmware),
  so a typo in `secrets.h` can't strand it.

## 5. Updates over Wi‑Fi

```
python firmware/tools/push.py <ip> ota firmware/build/pentadeck.bin
```
You can also use the Firmware card on the settings page. The new image goes into the slot that
isn't running, and that slot boots.
- **The first Wi‑Fi update overwrites the original firmware in `ota_0`.**
- From then on, the two slots hold the current Pentadeck and the previous one.

## Going back

| What you want | How |
|---|---|
| The other slot, while it still holds the original firmware | `python firmware/tools/push.py <ip> switch` |
| The exact original state | In download mode, write your backup back: `python -m esptool --chip esp32s3 --port COM5 --before no-reset --after no-reset write-flash 0x0 times-gate-backup.bin` |
| The original app over Wi‑Fi, without your settings | Extract `ota_0` from your backup (offset `0x10000`, `0x300000` bytes) and push it with `push.py <ip> ota <file>` |
