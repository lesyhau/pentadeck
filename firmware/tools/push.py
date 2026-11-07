"""Talk to the custom firmware over Wi-Fi.

usage:
    python push.py <device-ip> status
    python push.py <device-ip> ota <app.bin>     # install a new app image (ours, or the stock Divoom one)
    python push.py <device-ip> switch            # boot the other slot
    python push.py <device-ip> reboot
The X-Token is read from ../main/secrets.h.
"""
import os
import re
import sys
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))


def token():
    text = open(os.path.join(HERE, "..", "main", "secrets.h"), encoding="utf-8").read()
    return re.search(r'#define\s+OTA_TOKEN\s+"([^"]+)"', text).group(1)


def call(ip, path, body=None, timeout=10):
    req = urllib.request.Request(f"http://{ip}{path}", data=body, method="POST" if body is not None else "GET")
    if body is not None:
        req.add_header("X-Token", token())
        req.add_header("Content-Type", "application/octet-stream")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


def main():
    ip, cmd = sys.argv[1], sys.argv[2]
    if cmd == "status":
        print(*call(ip, "/api/status"))
    elif cmd == "ota":
        data = open(sys.argv[3], "rb").read()
        print(f"uploading {len(data)} bytes ...", flush=True)
        print(*call(ip, "/ota", data, timeout=300))
    elif cmd in ("switch", "reboot"):
        print(*call(ip, "/" + cmd, b""))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
