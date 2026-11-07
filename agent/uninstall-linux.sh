#!/usr/bin/env bash
# Remove the Pentadeck stats agent:  sudo bash uninstall-linux.sh  (same PORT / LAN as the install)
set -euo pipefail
PORT="${PORT:-8765}"
LAN="${LAN:-192.168.0.0/16}"
[ "$(id -u)" = "0" ] || { echo "run with sudo" >&2; exit 1; }
systemctl disable --now pentadeck-agent.service 2>/dev/null || true
rm -f /etc/systemd/system/pentadeck-agent.service
systemctl daemon-reload
rm -rf /opt/pentadeck-agent
if command -v ufw >/dev/null; then
    ufw delete allow from "$LAN" to any port "$PORT" proto tcp 2>/dev/null || true
fi
echo "removed"
