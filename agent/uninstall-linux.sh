#!/usr/bin/env bash
# Remove the Pentadeck stats agent:  sudo bash uninstall-linux.sh
#   curl -fsSL https://raw.githubusercontent.com/lesyhau/pentadeck/main/agent/uninstall-linux.sh | sudo bash
# It reads the PORT and LAN the install used (or takes them from the environment, like the install).
set -euo pipefail
DIR=/opt/pentadeck-agent

main() {
    [ "$(id -u)" = "0" ] || { echo "run with sudo" >&2; exit 1; }
    local OLD_PORT="" OLD_LAN=""
    if [ -f "$DIR/install.env" ]; then
        # shellcheck disable=SC1091
        . "$DIR/install.env"
    fi
    PORT="${PORT:-${OLD_PORT:-8765}}"
    LAN="${LAN:-${OLD_LAN:-192.168.0.0/16}}"

    systemctl disable --now pentadeck-agent.service 2>/dev/null || true
    rm -f /etc/systemd/system/pentadeck-agent.service
    systemctl daemon-reload
    rm -rf "$DIR"
    if command -v ufw >/dev/null; then
        ufw delete allow from "$LAN" to any port "$PORT" proto tcp </dev/null >/dev/null 2>&1 || true
    fi
    if command -v firewall-cmd >/dev/null && firewall-cmd --state >/dev/null 2>&1; then
        firewall-cmd --permanent --remove-rich-rule="rule family=\"ipv4\" source address=\"$LAN\" port port=\"$PORT\" protocol=\"tcp\" accept" >/dev/null 2>&1 &&
            firewall-cmd --reload >/dev/null 2>&1 || true
    fi
    echo "removed"
}

main "$@"
