#!/usr/bin/env python3
"""Pentadeck stats agent: serves this machine's vital signs as JSON for the Pentadeck firmware.

Standard library only:
  - Windows: kernel32 (CPU, RAM, uptime), shutil (disks), AMD ADL from the Radeon driver
    (GPU load, VRAM, GPU / hotspot / VRAM temperature, and the CPU's SoC temperature via its iGPU)
  - Linux:   /proc and /sys (CPU, CPU temperature, RAM, disks, battery, uptime) and local TCP checks
             for the services you list

    GET /stats   -> JSON (see SCHEMA below)

usage:
    python3 pentadeck_agent.py [--port 8765] [--name LABEL] [--disk PATH ...] [--service NAME:PORT ...]

SCHEMA (null where a machine has no such thing):
{
  "name": "PC", "host": "...", "os": "windows|linux", "ts": 1730900000, "uptime_s": 12345,
  "cpu":  {"model": "...", "threads": 16, "load": 12.3, "temp": 49.0, "temp_src": "SoC|Tctl|..."},
  "ram":  {"used": bytes, "total": bytes},
  "disks": [{"name": "C:", "used": bytes, "total": bytes}],
  "gpu":  {"name": "...", "load": 10.0, "vram_used": bytes, "vram_total": bytes,
           "temp": 40.0, "hotspot": 42.0, "vram_temp": 60.0, "power_w": 15.0} | null,
  "battery": {"percent": 95, "charging": false, "ac": true} | null,
  "services": [{"name": "web", "port": 8080, "up": true, "ms": 1.2}]
}
"""
from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

IS_WINDOWS = os.name == "nt"


# ----------------------------------------------------------------------------- shared helpers

def check_service(port: int, timeout: float = 0.6) -> tuple[bool, float | None]:
    t = time.perf_counter()
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=timeout):
            return True, round((time.perf_counter() - t) * 1000, 1)
    except OSError:
        return False, None


def disk_entry(path: str) -> dict | None:
    try:
        u = shutil.disk_usage(path)
    except OSError:
        return None
    name = path.rstrip("\\/") or "/"
    return {"name": name, "used": u.used, "total": u.total}


# ----------------------------------------------------------------------------- Linux

class LinuxProbe:
    def __init__(self):
        self._cpu = self._cpu_times()
        self.model = self._cpu_model()
        self.threads = os.cpu_count() or 0

    @staticmethod
    def _read(path: str) -> str | None:
        try:
            with open(path) as f:
                return f.read().strip()
        except OSError:
            return None

    @staticmethod
    def _cpu_model() -> str:
        try:
            with open("/proc/cpuinfo") as f:
                for line in f:
                    if line.startswith("model name"):
                        return line.split(":", 1)[1].strip()
        except OSError:
            pass
        return platform.processor()

    @staticmethod
    def _cpu_times() -> tuple[int, int]:
        with open("/proc/stat") as f:
            v = [int(x) for x in f.readline().split()[1:]]
        idle = v[3] + (v[4] if len(v) > 4 else 0)        # idle + iowait
        total = sum(v[:8])                                 # user..steal (guest is already in user)
        return idle, total

    def cpu_load(self) -> float:
        idle, total = self._cpu_times()
        di, dt = idle - self._cpu[0], total - self._cpu[1]
        self._cpu = (idle, total)
        return round(100.0 * (1 - di / dt), 1) if dt > 0 else 0.0

    def cpu_temp(self) -> tuple[float | None, str | None]:
        """Prefer the CPU's own sensor (k10temp Tctl/Tdie, coretemp package), else the ACPI zone."""
        found = {}
        base = "/sys/class/hwmon"
        for h in sorted(os.listdir(base)) if os.path.isdir(base) else []:
            name = self._read(f"{base}/{h}/name")
            for f in os.listdir(f"{base}/{h}"):
                if not (f.startswith("temp") and f.endswith("_input")):
                    continue
                raw = self._read(f"{base}/{h}/{f}")
                if raw is None or not raw.lstrip("-").isdigit():
                    continue
                label = self._read(f"{base}/{h}/{f[:-6]}_label") or f[:-6]
                found[(name, label)] = int(raw) / 1000
        for key in (("k10temp", "Tdie"), ("k10temp", "Tctl"), ("coretemp", "Package id 0"), ("zenpower", "Tdie")):
            if key in found:
                return round(found[key], 1), key[1]
        for (name, label), v in found.items():
            if name in ("acpitz", "cpu_thermal"):
                return round(v, 1), name
        return None, None

    def ram(self) -> dict:
        info = {}
        with open("/proc/meminfo") as f:
            for line in f:
                k, v = line.split(":", 1)
                info[k] = int(v.split()[0]) * 1024
        total = info.get("MemTotal", 0)
        return {"used": total - info.get("MemAvailable", 0), "total": total}

    def uptime(self) -> int:
        raw = self._read("/proc/uptime")
        return int(float(raw.split()[0])) if raw else 0

    def battery(self) -> dict | None:
        base = "/sys/class/power_supply"
        if not os.path.isdir(base):
            return None
        bat, ac = None, None
        for d in sorted(os.listdir(base)):
            kind = self._read(f"{base}/{d}/type")
            if kind == "Battery" and bat is None:
                cap = self._read(f"{base}/{d}/capacity")
                status = self._read(f"{base}/{d}/status") or ""
                if cap and cap.isdigit():
                    bat = {"percent": int(cap), "charging": status == "Charging", "status": status}
            elif kind == "Mains":
                online = self._read(f"{base}/{d}/online")
                ac = online == "1" if online is not None else ac
        if bat is not None:
            bat["ac"] = ac
        return bat

    def gpu(self) -> dict | None:
        return None   # not read on Linux yet


# ----------------------------------------------------------------------------- Windows

if IS_WINDOWS:
    import ctypes
    import ctypes.wintypes as wt
    import winreg

    class _FILETIME(ctypes.Structure):
        _fields_ = [("lo", wt.DWORD), ("hi", wt.DWORD)]

    class _MEMSTATUSEX(ctypes.Structure):
        _fields_ = [("dwLength", wt.DWORD), ("dwMemoryLoad", wt.DWORD), ("ullTotalPhys", ctypes.c_uint64),
                    ("ullAvailPhys", ctypes.c_uint64), ("ullTotalPageFile", ctypes.c_uint64),
                    ("ullAvailPageFile", ctypes.c_uint64), ("ullTotalVirtual", ctypes.c_uint64),
                    ("ullAvailVirtual", ctypes.c_uint64), ("ullAvailExtendedVirtual", ctypes.c_uint64)]

    class _POWERSTATUS(ctypes.Structure):
        _fields_ = [("ACLineStatus", ctypes.c_ubyte), ("BatteryFlag", ctypes.c_ubyte),
                    ("BatteryLifePercent", ctypes.c_ubyte), ("SystemStatusFlag", ctypes.c_ubyte),
                    ("BatteryLifeTime", wt.DWORD), ("BatteryFullLifeTime", wt.DWORD)]


class AmdAdl:
    """AMD Display Library (atiadlxx.dll, installed with the Radeon driver). No admin needed.
    PM log sensors: 8 edge temp, 9 memory temp, 19 GFX activity, 23 ASIC power, 27 hotspot,
    29 SoC temp (on a Ryzen's integrated GPU)."""

    T_EDGE, T_MEM, ACT_GFX, POWER, T_HOTSPOT, T_SOC = 8, 9, 19, 23, 27, 29

    def __init__(self):
        self.ok = False
        try:
            self.dll = ctypes.WinDLL("atiadlxx.dll")
        except OSError:
            return
        crt = ctypes.CDLL("msvcrt")
        crt.malloc.restype = ctypes.c_void_p
        MALLOC = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_int)
        self._malloc = MALLOC(lambda n: crt.malloc(n))   # keep a reference
        self.ctx = ctypes.c_void_p()
        if self.dll.ADL2_Main_Control_Create(self._malloc, 1, ctypes.byref(self.ctx)) != 0:
            return

        class AI(ctypes.Structure):
            _fields_ = [("iSize", ctypes.c_int), ("iAdapterIndex", ctypes.c_int), ("strUDID", ctypes.c_char * 256),
                        ("iBusNumber", ctypes.c_int), ("iDeviceNumber", ctypes.c_int),
                        ("iFunctionNumber", ctypes.c_int), ("iVendorID", ctypes.c_int),
                        ("strAdapterName", ctypes.c_char * 256), ("strDisplayName", ctypes.c_char * 256),
                        ("iPresent", ctypes.c_int), ("iExist", ctypes.c_int), ("strDriverPath", ctypes.c_char * 256),
                        ("strDriverPathExt", ctypes.c_char * 256), ("strPNPString", ctypes.c_char * 256),
                        ("iOSDisplayIndex", ctypes.c_int)]

        class Sens(ctypes.Structure):
            _fields_ = [("supported", ctypes.c_int), ("value", ctypes.c_int)]

        class PmLog(ctypes.Structure):
            _fields_ = [("size", ctypes.c_int), ("sensors", Sens * 256)]

        self._PmLog = PmLog
        n = ctypes.c_int()
        self.dll.ADL2_Adapter_NumberOfAdapters_Get(self.ctx, ctypes.byref(n))
        infos = (AI * max(1, n.value))()
        self.dll.ADL2_Adapter_AdapterInfo_Get(self.ctx, infos, ctypes.sizeof(infos))
        self.discrete = self.integrated = None   # (adapter index, name)
        seen = set()
        for a in infos[:n.value]:
            key = (a.iBusNumber, a.iDeviceNumber)
            if key in seen or not a.iPresent:
                continue
            seen.add(key)
            s = self.sensors(a.iAdapterIndex)
            if s is None:
                continue
            name = a.strAdapterName.decode(errors="replace").strip()
            if self.T_MEM in s and self.discrete is None:
                self.discrete = (a.iAdapterIndex, name)
            elif self.T_SOC in s and self.integrated is None:
                self.integrated = (a.iAdapterIndex, name)
        self.ok = self.discrete is not None or self.integrated is not None

    def sensors(self, index: int) -> dict | None:
        out = self._PmLog()
        out.size = ctypes.sizeof(out)
        if self.dll.ADL2_New_QueryPMLogData_Get(self.ctx, index, ctypes.byref(out)) != 0:
            return None
        return {i: out.sensors[i].value for i in range(1, 64) if out.sensors[i].supported}

    def vram_used_mb(self, index: int) -> int | None:
        mb = ctypes.c_int()
        try:
            rc = self.dll.ADL2_Adapter_VRAMUsage_Get(self.ctx, index, ctypes.byref(mb))
        except AttributeError:
            return None
        return mb.value if rc == 0 else None


class PdhCounterArray:
    """A wildcard Windows performance counter (e.g. per GPU adapter), read through pdh.dll. No admin."""

    PDH_FMT_LARGE = 0x00000400
    PDH_MORE_DATA = 0x800007D2

    def __init__(self, path: str):
        class FmtValue(ctypes.Structure):
            _fields_ = [("CStatus", wt.DWORD), ("largeValue", ctypes.c_longlong)]

        class Item(ctypes.Structure):
            _fields_ = [("szName", wt.LPWSTR), ("FmtValue", FmtValue)]

        self._Item = Item
        self.pdh = ctypes.WinDLL("pdh")
        self.query, self.counter = wt.HANDLE(), wt.HANDLE()
        self.ok = (self.pdh.PdhOpenQueryW(None, 0, ctypes.byref(self.query)) == 0 and
                   self.pdh.PdhAddEnglishCounterW(self.query, path, 0, ctypes.byref(self.counter)) == 0)

    def read(self) -> dict[str, int]:
        if not self.ok or self.pdh.PdhCollectQueryData(self.query) != 0:
            return {}
        size, count = wt.DWORD(0), wt.DWORD(0)
        rc = self.pdh.PdhGetFormattedCounterArrayW(self.counter, self.PDH_FMT_LARGE, ctypes.byref(size),
                                                   ctypes.byref(count), None) & 0xFFFFFFFF
        if rc != self.PDH_MORE_DATA or not size.value:
            return {}
        buf = ctypes.create_string_buffer(size.value)
        if self.pdh.PdhGetFormattedCounterArrayW(self.counter, self.PDH_FMT_LARGE, ctypes.byref(size),
                                                 ctypes.byref(count), buf) != 0:
            return {}
        items = ctypes.cast(buf, ctypes.POINTER(self._Item))
        return {items[i].szName: items[i].FmtValue.largeValue for i in range(count.value)}


def _vram_total_by_name() -> dict[str, int]:
    """Dedicated VRAM size per display adapter, from the registry (no admin needed)."""
    out = {}
    key = r"SYSTEM\ControlSet001\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}"
    try:
        root = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key)
    except OSError:
        return out
    with root:
        for i in range(64):
            try:
                sub = winreg.EnumKey(root, i)
            except OSError:
                break
            try:
                with winreg.OpenKey(root, sub) as s:
                    name = winreg.QueryValueEx(s, "DriverDesc")[0]
                    size = winreg.QueryValueEx(s, "HardwareInformation.qwMemorySize")[0]
                    out[name] = int.from_bytes(size, "little") if isinstance(size, bytes) else int(size)
            except OSError:
                continue
    return out


class WindowsProbe:
    def __init__(self):
        self.k32 = ctypes.windll.kernel32
        self.k32.GetTickCount64.restype = ctypes.c_uint64
        self._cpu = self._cpu_times()
        self.threads = os.cpu_count() or 0
        self.model = self._cpu_model()
        self.adl = AmdAdl()
        self._vram_total = _vram_total_by_name()
        # ADL's VRAM-usage call returns nonsense on this driver; Windows' own counter is right.
        self._vram_used = PdhCounterArray(r"\GPU Adapter Memory(*)\Dedicated Usage")

    @staticmethod
    def _cpu_model() -> str:
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as k:
                return winreg.QueryValueEx(k, "ProcessorNameString")[0].strip()
        except OSError:
            return platform.processor()

    def _cpu_times(self) -> tuple[int, int]:
        idle, kern, user = _FILETIME(), _FILETIME(), _FILETIME()
        self.k32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kern), ctypes.byref(user))
        v = lambda ft: (ft.hi << 32) | ft.lo
        return v(idle), v(kern) + v(user)      # kernel time includes idle time

    def cpu_load(self) -> float:
        idle, total = self._cpu_times()
        di, dt = idle - self._cpu[0], total - self._cpu[1]
        self._cpu = (idle, total)
        return round(100.0 * (1 - di / dt), 1) if dt > 0 else 0.0

    def cpu_temp(self) -> tuple[float | None, str | None]:
        # Windows has no native core temperature for Ryzen; the CPU's integrated GPU reports the SoC
        # (I/O die) temperature through the Radeon driver.
        if self.adl.ok and self.adl.integrated:
            s = self.adl.sensors(self.adl.integrated[0]) or {}
            if AmdAdl.T_SOC in s:
                return float(s[AmdAdl.T_SOC]), "SoC"
        return None, None

    def ram(self) -> dict:
        m = _MEMSTATUSEX()
        m.dwLength = ctypes.sizeof(m)
        self.k32.GlobalMemoryStatusEx(ctypes.byref(m))
        return {"used": m.ullTotalPhys - m.ullAvailPhys, "total": m.ullTotalPhys}

    def uptime(self) -> int:
        return int(self.k32.GetTickCount64() // 1000)

    def battery(self) -> dict | None:
        p = _POWERSTATUS()
        if not self.k32.GetSystemPowerStatus(ctypes.byref(p)) or p.BatteryFlag & 128 or p.BatteryFlag == 255:
            return None   # no system battery
        return {"percent": p.BatteryLifePercent, "charging": bool(p.BatteryFlag & 8), "ac": p.ACLineStatus == 1}

    def gpu(self) -> dict | None:
        if not (self.adl.ok and self.adl.discrete):
            return None
        index, name = self.adl.discrete
        s = self.adl.sensors(index) or {}
        per_adapter = self._vram_used.read()
        used = max(per_adapter.values()) if per_adapter else None   # the discrete card holds the most
        total = self._vram_total.get(name)
        g = lambda k: float(s[k]) if k in s else None
        return {
            "name": name, "load": g(AmdAdl.ACT_GFX),
            "vram_used": used, "vram_total": total,
            "temp": g(AmdAdl.T_EDGE), "hotspot": g(AmdAdl.T_HOTSPOT), "vram_temp": g(AmdAdl.T_MEM),
            "power_w": g(AmdAdl.POWER),
        }


# ----------------------------------------------------------------------------- sampler + server

class Agent:
    def __init__(self, name: str, disks: list[str], services: list[tuple[str, int]]):
        self.name, self.disks, self.services = name, disks, services
        self.probe = WindowsProbe() if IS_WINDOWS else LinuxProbe()
        self.snapshot = b"{}"
        self.lock = threading.Lock()

    def sample(self) -> dict:
        p = self.probe
        temp, src = p.cpu_temp()
        services = []
        for svc_name, port in self.services:
            up, ms = check_service(port)
            services.append({"name": svc_name, "port": port, "up": up, "ms": ms})
        return {
            "name": self.name, "host": socket.gethostname(), "os": "windows" if IS_WINDOWS else "linux",
            "ts": int(time.time()), "uptime_s": p.uptime(),
            "cpu": {"model": p.model, "threads": p.threads, "load": p.cpu_load(), "temp": temp, "temp_src": src},
            "ram": p.ram(),
            "disks": [d for d in (disk_entry(x) for x in self.disks) if d],
            "gpu": p.gpu(),
            "battery": p.battery(),
            "services": services,
        }

    def run_sampler(self, period: float = 1.0):
        while True:
            try:
                data = json.dumps(self.sample(), separators=(",", ":")).encode()
                with self.lock:
                    self.snapshot = data
            except Exception as e:   # keep serving the last good sample
                print("sample failed:", e, flush=True)
            time.sleep(period)


def make_handler(agent: Agent):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path.split("?")[0] not in ("/", "/stats"):
                self.send_error(404)
                return
            with agent.lock:
                body = agent.snapshot
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, fmt, *args):
            pass

    return Handler


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--name", default=None, help="label shown on the device (default: host name)")
    ap.add_argument("--disk", action="append", default=None, help="path to report (repeatable)")
    ap.add_argument("--service", action="append", default=[], help="NAME:PORT checked on 127.0.0.1 (repeatable)")
    a = ap.parse_args()
    disks = a.disk or (["C:\\"] if IS_WINDOWS else ["/"])
    services = []
    for s in a.service:
        n, _, port = s.rpartition(":")
        services.append((n or f"port{port}", int(port)))
    agent = Agent(a.name or socket.gethostname(), disks, services)
    threading.Thread(target=agent.run_sampler, daemon=True).start()
    time.sleep(1.2)   # first CPU sample needs an interval
    server = ThreadingHTTPServer(("0.0.0.0", a.port), make_handler(agent))
    print(f"pentadeck agent: http://0.0.0.0:{a.port}/stats", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
