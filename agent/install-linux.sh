#!/usr/bin/env bash
# Install the Pentadeck stats agent as a systemd service on this Linux host.
#   sudo bash install-linux.sh
#   sudo NAME=NAS SERVICES="web:8080 db:5432" bash install-linux.sh
#
# Read-only: it serves CPU / temperature / RAM / disk / battery / uptime and local service checks
# as JSON on :8765 for the device on the LAN. Runs as a throwaway user (DynamicUser) that can write
# nowhere. Undo with: sudo bash uninstall-linux.sh
#
#   NAME      the label the device shows (default LAB)
#   PORT      the port to serve on (default 8765)
#   LAN       who may connect, when ufw is active (default 192.168.0.0/16)
#   SERVICES  local TCP services to check once a second, as name:port pairs (default none)
#   DISKS     mount points to report (default /)
set -euo pipefail

PORT="${PORT:-8765}"
LAN="${LAN:-192.168.0.0/16}"
NAME="${NAME:-LAB}"
SERVICES="${SERVICES:-}"
DISKS="${DISKS:-/}"

[ "$(id -u)" = "0" ] || { echo "run with sudo" >&2; exit 1; }
HERE="$(cd "$(dirname "$0")" && pwd)"

install -d -m 755 /opt/pentadeck-agent
install -m 644 "$HERE/pentadeck_agent.py" /opt/pentadeck-agent/pentadeck_agent.py

ARGS="--name $NAME --port $PORT"
for d in $DISKS; do ARGS="$ARGS --disk $d"; done
for s in $SERVICES; do ARGS="$ARGS --service $s"; done

cat > /etc/systemd/system/pentadeck-agent.service <<UNIT
[Unit]
Description=Pentadeck stats agent (read-only system stats as JSON on :$PORT)
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/usr/bin/python3 /opt/pentadeck-agent/pentadeck_agent.py $ARGS
DynamicUser=yes
Restart=always
RestartSec=5
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
MemoryMax=64M

[Install]
WantedBy=multi-user.target
UNIT

systemctl daemon-reload
systemctl enable --now pentadeck-agent.service

if command -v ufw >/dev/null && ufw status | grep -q "Status: active"; then
    ufw allow from "$LAN" to any port "$PORT" proto tcp comment "Pentadeck stats agent"
fi

sleep 2
systemctl --no-pager --lines=3 status pentadeck-agent.service || true
echo
echo "check: curl -s http://127.0.0.1:$PORT/stats"
