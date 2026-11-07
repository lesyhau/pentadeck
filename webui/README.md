# Pentadeck web UI

The settings page Pentadeck serves on your LAN. Open `http://<device-ip>/` (or
`http://pentadeck.local/`) on a phone or a computer. It has four pages, each with its own address,
so back and forward work and a reload stays put:

- **Overview** (`#/`): the PC and home lab the device watches (load, temperatures, RAM, battery,
  services) and the device itself (firmware, Wi-Fi, memory, weather, microphone), refreshed every
  2 seconds.
- **Remote** (`#/remote`): the five screens, live and as large as the page allows, side by side as on
  the device, and its five buttons. A tap is a short press, a hold is a long one (LEDs on/off, sleep,
  home), and the arrows repeat while held. The header names the current mode. The frames (160 KB a
  round, up to about 2.5 rounds a second) are fetched only while this page is open, the strip is on
  screen and the browser tab is in front, never more than one round at a time, and the **Live**
  switch stops them.
- **Settings** (`#/settings`): brightness and night dimming, what each of the five screens shows, LED
  mode and brightness, sounds and the daily alarm, the world clock, the stats agents, time zone and
  location. Nothing reaches the device until you press **Save**. Unsaved changes survive a visit to
  another page; a dot on the Settings tab says they are there.
- **Device** (`#/device`): pairing, over-the-air updates from a `.bin` app image, reboot, and what the
  device says about itself (firmware, running slot, IP, Wi-Fi, uptime, free memory, last reset).

On a phone the pages are tabs along the bottom; from 1024 px wide they sit in the header.

### Pairing

Anything that changes the device (saving, testing the LEDs or a sound, the remote's buttons,
rebooting, updating) needs this browser to be paired. **Pair with the device** on the Device page
shows a 4-digit code; the device's screens ask *Pair this browser?* with the same code, and a press of
its **power** button hands this browser the device's token, which is kept in this browser only
(localStorage). **Forget this browser** drops it. A change attempted before pairing, or refused by the
device, opens the same pairing in a dialog over the page; once paired, the dialog offers to do the
change again.

It is built for phones first: it works at 375 px wide, respects the notch and the home bar, and can
be added to the Home Screen. Light, dark and system appearance are available from the header.

The page is Vite + React 18 + TypeScript + Tailwind CSS 3. The build produces **one self-contained
HTML file** (script, styles, fonts and icons inlined) that the firmware embeds gzipped and serves at
`/`. It never loads anything from the internet, because the device is often on a network without it.

## Requirements

Node.js 18 or newer.

```sh
npm install
```

## Develop

```sh
PENTADECK_URL=http://192.168.1.50 npm run dev
```

This opens a dev server at <http://localhost:8098> with hot reload. Requests to `/api`, `/status`, `/reboot`
and `/ota` are proxied to the device at `PENTADECK_URL`, which defaults to `http://pentadeck.local`.
On Windows PowerShell, use `$env:PENTADECK_URL = "http://192.168.1.50"; npm run dev`.

Saving, testing the LEDs or the buzzer, the remote's buttons, rebooting and updating all act on the
real device. Pair the dev page as you would the real one (it is a separate browser origin, so it is
paired separately); the token it receives is the firmware's `OTA_TOKEN`.

```sh
npm run typecheck   # tsc --noEmit
```

## Build

```sh
npm run build              # dist/index.html and ../firmware/main/web/index.html.gz
node build.mjs --no-gz     # dist/index.html only
```

`npm run build` writes the gzipped page (level 9) to `../firmware/main/web/index.html.gz`. The
firmware build embeds that file, so rebuild the firmware afterwards. The build fails if the page
still references any file or URL outside itself.

### Size budget

The page ships inside the firmware image, so keep the gzipped file **under about 160 KB**. Today
it is about 139 KB. The fonts are the largest part (Inter 400 and 500, Orbitron 700, Latin subset,
woff2), and each extra Inter weight adds about 24 KB. Add a weight only if the design truly needs it.

## Layout

| Path | What it is |
| --- | --- |
| `src/main.tsx`, `src/App.tsx` | The entry point, and the shell: header, tabs, the current page, the save bar, the pairing dialog, and every write to the device |
| `src/pages/` | One file per page: `Overview.tsx` (polled every 2 s by `hooks/useStatus.ts`), `Remote.tsx` (the screens' live view and the five buttons), `Settings.tsx` with its cards in `DeviceSettingsForm.tsx` (Display, LEDs, Sounds & alarm, World clock, Data & location), and `Device.tsx` (access, firmware, reboot, device info) |
| `src/components/` | Pieces the app shell and pages share: `Nav.tsx` (the page tabs: a bottom bar on a phone, in the header from `lg`) and `Pairing.tsx` (the flow, `usePairing`, the code panel and the pairing dialog) |
| `src/hooks/` | `useStatus.ts` (polls `/api/status`) and `useSettings.ts` (the settings draft, kept above the pages: load, edit, validate, save, undo) |
| `src/lib/` | No React: `api.ts` (the device's HTTP API and formatting helpers), `form.ts` (draft, payload, validation with the same limits the device enforces), `router.ts` (the four pages, their hash addresses `#/`, `#/remote`, `#/settings`, `#/device`, and the page title), `appearance.ts` (light, dark or system, and the pre-paint script that prevents a flash of the wrong theme) |
| `src/ui/` | The page's small design system (buttons, fields, switches, sliders, stat tiles, dialogs and more) |
| `src/styles/` | `index.css` (Tailwind's entry), `theme.css` (design tokens `--pd-*`, light and dark, and the few styles Tailwind can't express), `fonts.css` (the bundled font faces) |
| `src/brand/` | The logo mark, favicon and Home Screen icon |

## Device API

| Method | Path | Notes |
| --- | --- | --- |
| GET | `/api/status` | Live readings |
| GET | `/status` | The firmware's record: version, build, slots, uptime, last reset reason |
| POST | `/api/pair` | No token. Asks the owner on the device's screens (60 s); answers `{id, code}`, or 429 while a request is pending |
| GET | `/api/pair?id=<id>` | `{"state":"waiting"}`, `"refused"`, or `"allowed"` with the token |
| GET | `/api/settings` | Current settings, plus `led_modes` and `panels` |
| POST | `/api/settings` | JSON body; answers with the saved settings |
| POST | `/api/leds?mode=groups&b=60` / `?mode=auto` | LED test pattern / resume effects |
| POST | `/api/beep?v=<volume>` | Test beep |
| GET | `/api/screen?s=0..4` | That screen's frame: 128 × 128, big-endian RGB565, 32768 bytes |
| POST | `/api/button?k=light\|left\|power\|right\|mode&e=short\|long\|down` | Press a button |
| POST | `/reboot` | Restart |
| POST | `/ota` | Raw app image (`application/octet-stream`) |

Every POST except `/api/pair` must carry the token in the `X-Token` header; without it the device
answers 401 or 403. The page gets the token by pairing.

## License

MIT
