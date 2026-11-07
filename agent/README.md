# Pentadeck stats agent

`pentadeck_agent.py` serves one machine's vital signs as JSON at `http://<machine>:8765/stats`, for
the device to poll. It needs Python 3.9 or newer and uses the standard library only.

It is read‑only: it reports numbers and changes nothing on the machine. On Windows it can also
turn the device's microphone into a PC microphone ([below](#microphone)); that part is off unless you ask for it.

| | Windows | Linux |
|---|---|---|
| CPU load, RAM, uptime | kernel32 | `/proc` |
| Disks | any paths you pass | any mount points you pass |
| CPU temperature | the AMD SoC sensor through the Radeon driver (ADL), when present | `/sys` hwmon (`k10temp` Tctl, `coretemp` and others) |
| GPU load, temperatures, VRAM | AMD Radeon through ADL, plus Windows' VRAM counter | not reported |
| Battery, AC | GetSystemPowerStatus | `/sys/class/power_supply` |
| Services | TCP checks of local ports you list | TCP checks of local ports you list |
| Time since the last keyboard or mouse input | GetLastInputInfo, in your own session | systemd‑logind's idle hint of the desktop session (coarse), none on a headless server |

NVIDIA and Intel GPUs on Windows are not read yet. Pull requests are welcome.

The input idle time (`input_idle_s`) tells the device someone is at the desk. On Linux it is only
reported once the desktop marks the session idle (often its screen‑blank delay), since many desktops
never set that mark; the device's microphone covers the rest.

## Install

The installers start the agent by itself at every boot or logon, open its port to your LAN, check
that it answers, and print the URL to put into the device's settings page. Run one again to
upgrade: it keeps your previous settings.

### Windows

Double‑click `install-windows.cmd` and answer the questions (Enter keeps the default). Or, in
PowerShell, without downloading anything first:

```
irm https://raw.githubusercontent.com/lesyhau/pentadeck/main/agent/install-windows.ps1 | iex
```

With options, it asks nothing:

```
powershell -ExecutionPolicy Bypass -File install-windows.ps1 -Name PC -Disks C:\,D:\ -Mic -Token your-token
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/lesyhau/pentadeck/main/agent/install-windows.ps1))) -Name PC
```

| Option | Meaning | Default |
|---|---|---|
| `-Name` | the label the device shows | this computer's name |
| `-Disks` | drives to report | every fixed drive |
| `-Port` | the port to serve on | 8765 |
| `-Services` | local ports to check, as `name:port` | none |
| `-Mic` | also make the device's microphone a PC microphone ([below](#microphone)) | off |
| `-Token` | the device's access token, for `-Mic` | the one saved last time |
| `-Yes` | ask nothing; use the previous install's settings or the defaults | |
| `-NoFirewall` | leave the firewall alone | |
| `-MakePrivate` | if your network is set to Public, set it to Private | it asks |

What it does:

- Finds Python 3.9 or newer. If there's none, it offers to run `winget install Python.Python.3.13`.
  The Microsoft Store's `python` shortcut doesn't count.
- Copies the agent to `%LOCALAPPDATA%\Pentadeck`, puts a "Pentadeck agent" shortcut into your
  Startup folder, and starts it. It runs with `pythonw.exe`, so there's no window.
- Replaces an older setup: an older agent on the same port is stopped, and a `timegate-agent.vbs`
  (or similar) in the Startup folder moves to `%LOCALAPPDATA%\Pentadeck-backup`.
- Asks for admin rights once, to allow the port from your local subnet on Private networks. Windows
  blocks it on a network set to Public, so if yours is, it offers to set it to Private. If you
  decline the admin prompt, it says so; run it again later.
- With `-Mic`, saves the token as your `PENTADECK_TOKEN` environment variable (not on the command
  line), and warns if VB‑CABLE isn't installed.

It needs no admin rights otherwise. It runs in your own session, not as a service, so it can see
keyboard and mouse activity and play into your audio devices; it starts when you log in.

To remove it, double‑click `uninstall-windows.cmd`, or run
`powershell -ExecutionPolicy Bypass -File uninstall-windows.ps1`. It stops the agent, removes the
shortcut, the folder and the firewall rule, and asks whether to remove the saved token.

### Linux

It needs systemd and Python 3.9 or newer (`sudo apt install python3`, `sudo dnf install python3`
or `sudo pacman -S python`).

```
curl -fsSL https://raw.githubusercontent.com/lesyhau/pentadeck/main/agent/install-linux.sh | sudo bash
curl -fsSL https://raw.githubusercontent.com/lesyhau/pentadeck/main/agent/install-linux.sh | sudo NAME=NAS bash
sudo NAME=NAS SERVICES="web:8080 db:5432" bash install-linux.sh
```

Options are environment variables, set after `sudo`:

| Variable | Meaning | Default |
|---|---|---|
| `NAME` | the label the device shows | the short host name |
| `PORT` | the port to serve on | 8765 |
| `DISKS` | mount points to report, separated by spaces | `/` plus real filesystems mounted under `/home`, `/srv`, `/mnt` or `/data` |
| `SERVICES` | local ports to check, as `name:port` pairs | none |
| `LAN` | who may connect, in `ufw` or `firewalld` | this host's subnet |

It copies the agent to `/opt/pentadeck-agent` and runs it as a locked‑down service: a throwaway
user, a read‑only system, no home directory. It opens the port to your LAN in `ufw` or `firewalld`
if one is active. To remove it: `sudo bash uninstall-linux.sh`, or the same `curl` line with
`uninstall-linux.sh`.

## Run by hand

```
python pentadeck_agent.py --name PC --disk C:\ --disk D:\
python3 pentadeck_agent.py --name LAB --disk / --service web:8080 --service db:5432
```

Options:

| Option | Meaning | Default |
|---|---|---|
| `--port` | the port to serve on | 8765 |
| `--name` | the label the device shows | the host name |
| `--disk` | a path to report; repeat it for more | `C:\` on Windows, `/` on Linux |
| `--service NAME:PORT` | a local TCP port to check once a second; repeat it for more | none |

Check it with `curl http://127.0.0.1:8765/stats`. Then put `http://<machine-ip>:8765/stats` into the
settings page's Data card. Allow the port through the machine's firewall, for your LAN only.

## Microphone

On Windows, `--mic` plays the device's microphone into a virtual audio cable, so any app (Zoom,
Discord, Teams, OBS, voice typing) can pick it as a microphone. The device's USB port can't carry
audio (its USB pins are wired to the mic), so the sound comes over Wi‑Fi.

1. Install [VB‑CABLE](https://vb-audio.com/Cable/) (free). It adds a playback device "CABLE Input"
   and a recording device "CABLE Output".
2. Install the agent with `-Mic` and the device's token (from `firmware/main/secrets.h`):
   ```
   powershell -ExecutionPolicy Bypass -File install-windows.ps1 -Mic -Token your-token
   ```
   or run it by hand:
   ```
   set PENTADECK_TOKEN=your-token
   python pentadeck_agent.py --name PC --disk C:\ --mic
   ```
3. In the app, choose **CABLE Output** as the microphone (**CABLE-A Output** with VB‑Audio's A+B pack).

| Option | Meaning | Default |
|---|---|---|
| `--mic [DEVICE]` | stream the mic from the device at this address | the device that polls `/stats` |
| `--mic-token` | the device's access token | the `PENTADECK_TOKEN` environment variable |
| `--mic-gain` | gain in dB, 0..48 | the device's (30) |
| `--mic-out` | the playback device to play into, matched by name (e.g. `"CABLE-B Input"`) | `CABLE Input`, else the first `CABLE-x Input` (VB‑Audio's A+B pack) |

- The delay is about 150–200 ms (Wi‑Fi, a 60 ms buffer, Windows' mixer): fine for calls and recording.
- The device's AUDIO screen shows a red **MIC LIVE** while the stream is open (the middle screen
  shows **MIC ●** when the AUDIO screen isn't showing).
- The settings page's Microphone card can turn the stream off; the agent then waits and retries.
- If the device or Wi‑Fi drops, the agent plays silence and reconnects by itself.
- Too quiet or too loud: change `--mic-gain` (each 6 dB doubles or halves the level).
- Linux: there's no `--mic`; the stream is a plain WAV over HTTP that ffmpeg, VLC or OBS can read.
  See [docs/API.md](../docs/API.md#microphone-stream).
