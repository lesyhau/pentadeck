# Pentadeck stats agent

`pentadeck_agent.py` serves one machine's vital signs as JSON at `http://<machine>:8765/stats`, for
the device to poll. It needs Python 3.9 or newer and uses the standard library only.

It is read‑only: it reports numbers and changes nothing on the machine.

| | Windows | Linux |
|---|---|---|
| CPU load, RAM, uptime | kernel32 | `/proc` |
| Disks | any paths you pass | any mount points you pass |
| CPU temperature | the AMD SoC sensor through the Radeon driver (ADL), when present | `/sys` hwmon (`k10temp` Tctl, `coretemp` and others) |
| GPU load, temperatures, VRAM | AMD Radeon through ADL, plus Windows' VRAM counter | not reported |
| Battery, AC | GetSystemPowerStatus | `/sys/class/power_supply` |
| Services | TCP checks of local ports you list | TCP checks of local ports you list |

NVIDIA and Intel GPUs on Windows are not read yet. Pull requests are welcome.

## Run

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

## Start it automatically

**Linux (systemd):**

```
sudo NAME=LAB SERVICES="web:8080" bash install-linux.sh
```

This installs it as a locked‑down service with no write access and no home directory, and opens
the port to your LAN in `ufw` if `ufw` is active. Undo it with `sudo bash uninstall-linux.sh`.

**Windows:** put a shortcut in the Startup folder (`shell:startup`) that runs:

```
pythonw.exe -I C:\path\to\pentadeck_agent.py --name PC --disk C:\
```
