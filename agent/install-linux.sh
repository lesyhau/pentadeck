#!/usr/bin/env bash
# Install the Pentadeck stats agent as a systemd service on this Linux host.
#   sudo bash install-linux.sh
#   sudo NAME=NAS SERVICES="web:8080 db:5432" bash install-linux.sh
#   curl -fsSL https://raw.githubusercontent.com/lesyhau/pentadeck/main/agent/install-linux.sh | sudo bash
#   curl -fsSL https://raw.githubusercontent.com/lesyhau/pentadeck/main/agent/install-linux.sh | sudo NAME=NAS bash
#
# Read-only: it serves CPU / temperature / RAM / disk / battery / uptime and local service checks
# as JSON on :8765 for the device on the LAN. Runs as a throwaway user (DynamicUser) that can write
# nowhere. Run it again to upgrade; settings you don't pass keep their previous values.
# Undo with: sudo bash uninstall-linux.sh
#
#   NAME      the label the device shows (default: the short host name)
#   PORT      the port to serve on (default 8765)
#   LAN       who may connect, when ufw or firewalld is active (default: this host's subnet)
#   SERVICES  local TCP services to check once a second, as name:port pairs (default none)
#   DISKS     mount points to report (default: / plus real filesystems under /home /srv /mnt /data)
set -euo pipefail

AGENT_URL="https://raw.githubusercontent.com/lesyhau/pentadeck/main/agent/pentadeck_agent.py"
DIR=/opt/pentadeck-agent
TMP=""
UNIT=/etc/systemd/system/pentadeck-agent.service

die() { echo "$*" >&2; exit 1; }

short_host() { local h; h="$(hostname -s 2>/dev/null || uname -n)"; echo "${h%%.*}"; }

# The network of the default route's interface, e.g. 192.168.1.0/24.
lan_subnet() {
    local dev cidr ip bits a b c d n mask
    dev="$(ip -4 route show default 2>/dev/null | awk '{for (i = 1; i < NF; i++) if ($i == "dev") { print $(i + 1); exit }}')"
    [ -n "$dev" ] || return 1
    cidr="$(ip -o -4 addr show dev "$dev" scope global 2>/dev/null | awk '{print $4; exit}')"
    [ -n "$cidr" ] || return 1
    ip="${cidr%/*}"; bits="${cidr#*/}"
    IFS=. read -r a b c d <<<"$ip"
    n=$(( (a << 24) | (b << 16) | (c << 8) | d ))
    mask=$(( bits == 0 ? 0 : (0xFFFFFFFF << (32 - bits)) & 0xFFFFFFFF ))
    n=$(( n & mask ))
    echo "$(( n >> 24 & 255 )).$(( n >> 16 & 255 )).$(( n >> 8 & 255 )).$(( n & 255 ))/$bits"
}

# / plus real local filesystems mounted under /home, /srv, /mnt or /data: no tmpfs, overlay, snap
# squashfs or network shares, and each device once (btrfs subvolumes and bind mounts repeat it).
detect_disks() {
    local out="/" seen src target fstype rest
    seen=" $(awk '$2 == "/" { s = $1 } END { print s }' /proc/self/mounts) "
    while read -r src target fstype rest; do
        case "$fstype" in
            ext2|ext3|ext4|xfs|btrfs|zfs|f2fs|bcachefs|jfs|reiserfs|ntfs|ntfs3|fuseblk|exfat) ;;
            *) continue ;;
        esac
        case "$target" in
            /home|/home/*|/srv|/srv/*|/mnt|/mnt/*|/data|/data/*) ;;
            *) continue ;;
        esac
        case "$target" in *\\*) continue ;; esac     # a space or the like, escaped: skip it
        case "$seen" in *" $src "*) continue ;; esac
        seen="$seen$src "
        out="$out $target"
    done < /proc/self/mounts
    echo "$out"
}

# One argument for a systemd ExecStart= line.
q() {
    local s="$1"
    s="${s//\\/\\\\}"; s="${s//\"/\\\"}"; s="${s//%/%%}"; s="${s//\$/\$\$}"
    printf '"%s"' "$s"
}

python_hint() {
    echo "Python 3.9 or newer is needed. Install it with:" >&2
    if command -v apt-get >/dev/null; then echo "    sudo apt install python3" >&2
    elif command -v dnf >/dev/null; then echo "    sudo dnf install python3" >&2
    elif command -v pacman >/dev/null; then echo "    sudo pacman -S python" >&2
    elif command -v zypper >/dev/null; then echo "    sudo zypper install python3" >&2
    else echo "    sudo apt install python3   |   sudo dnf install python3   |   sudo pacman -S python" >&2
    fi
    exit 1
}

firewall_close() {   # PORT LAN: remove what firewall_open added
    if command -v ufw >/dev/null; then
        ufw delete allow from "$2" to any port "$1" proto tcp </dev/null >/dev/null 2>&1 || true
    fi
    if command -v firewall-cmd >/dev/null && firewall-cmd --state >/dev/null 2>&1; then
        firewall-cmd --permanent --remove-rich-rule="rule family=\"ipv4\" source address=\"$2\" port port=\"$1\" protocol=\"tcp\" accept" >/dev/null 2>&1 &&
            firewall-cmd --reload >/dev/null 2>&1 || true
    fi
}

firewall_open() {    # PORT LAN
    if command -v ufw >/dev/null && ufw status 2>/dev/null | grep -q "Status: active"; then
        ufw allow from "$2" to any port "$1" proto tcp comment "Pentadeck stats agent" </dev/null >/dev/null
        echo "ufw: allowed TCP $1 from $2"
    fi
    if command -v firewall-cmd >/dev/null && firewall-cmd --state >/dev/null 2>&1; then
        firewall-cmd --permanent --add-rich-rule="rule family=\"ipv4\" source address=\"$2\" port port=\"$1\" protocol=\"tcp\" accept" >/dev/null
        firewall-cmd --reload >/dev/null
        echo "firewalld: allowed TCP $1 from $2"
    fi
}

fetch() {   # URL FILE
    if command -v curl >/dev/null; then curl -fsSL --max-time 30 "$1" -o "$2"
    elif command -v wget >/dev/null; then wget -qO "$2" "$1"
    else "$PY" -I -c 'import sys, urllib.request; urllib.request.urlretrieve(sys.argv[1], sys.argv[2])' "$1" "$2"
    fi
}

main() {
    [ "$(id -u)" = "0" ] || die "run with sudo"
    if [ ! -d /run/systemd/system ] || ! command -v systemctl >/dev/null; then die "this installer needs systemd"; fi

    PY="$(command -v python3 || true)"
    [ -n "$PY" ] || python_hint
    "$PY" -c 'import sys; sys.exit(sys.version_info < (3, 9))' || python_hint

    # Settings: what you pass, else the previous install's, else the defaults.
    local OLD_NAME="" OLD_PORT="" OLD_LAN="" OLD_SERVICES="" OLD_DISKS="" w i
    if [ -f "$DIR/install.env" ]; then
        # shellcheck disable=SC1091
        . "$DIR/install.env"
    elif [ -f "$UNIT" ]; then
        # installed by an older version of this script: read its options off the ExecStart line
        read -r -a w <<<"$(sed -n 's/^ExecStart=[^ ]* [^ ]*pentadeck_agent\.py //p' "$UNIT")"
        for (( i = 0; i + 1 < ${#w[@]}; i++ )); do
            case "${w[i]}" in
                --name) OLD_NAME="${w[i + 1]}" ;;
                --port) OLD_PORT="${w[i + 1]}" ;;
                --disk) OLD_DISKS="${OLD_DISKS:+$OLD_DISKS }${w[i + 1]}" ;;
                --service) OLD_SERVICES="${OLD_SERVICES:+$OLD_SERVICES }${w[i + 1]}" ;;
            esac
        done
        OLD_LAN="192.168.0.0/16"   # its default
    fi
    NAME="${NAME:-${OLD_NAME:-$(short_host)}}"
    PORT="${PORT:-${OLD_PORT:-8765}}"
    LAN="${LAN:-${OLD_LAN:-$(lan_subnet || echo 192.168.0.0/16)}}"
    SERVICES="${SERVICES-$OLD_SERVICES}"
    DISKS="${DISKS:-${OLD_DISKS:-$(detect_disks)}}"
    case "$PORT" in ''|*[!0-9]*) die "PORT must be a number" ;; esac

    # The agent: next to this script, or downloaded (curl ... | sudo bash).
    local here=""
    if [ -n "${BASH_SOURCE[0]:-}" ] && [ -f "${BASH_SOURCE[0]}" ]; then
        here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    fi
    TMP="$(mktemp)"
    trap 'rm -f "$TMP"' EXIT
    if [ -n "$here" ] && [ -f "$here/pentadeck_agent.py" ]; then
        cp "$here/pentadeck_agent.py" "$TMP"
    else
        echo "downloading $AGENT_URL"
        fetch "$AGENT_URL" "$TMP" || die "download failed"
    fi
    "$PY" -I -c 'import ast, sys; ast.parse(open(sys.argv[1], "rb").read())' "$TMP" ||
        die "the agent file is not valid Python (a failed download?)"

    install -d -m 755 "$DIR"
    install -m 644 "$TMP" "$DIR/pentadeck_agent.py"
    {
        printf 'OLD_NAME=%q\n' "$NAME"; printf 'OLD_PORT=%q\n' "$PORT"; printf 'OLD_LAN=%q\n' "$LAN"
        printf 'OLD_SERVICES=%q\n' "$SERVICES"; printf 'OLD_DISKS=%q\n' "$DISKS"
    } > "$DIR/install.env"
    chmod 644 "$DIR/install.env"

    local ARGS d s protect_home=yes
    ARGS="--name $(q "$NAME") --port $PORT"
    for d in $DISKS; do
        ARGS="$ARGS --disk $(q "$d")"
        # ProtectHome=yes hides /home, so a disk under it would read as the empty stand-in
        case "$d" in /home|/home/*|/root|/root/*) protect_home=read-only ;; esac
    done
    for s in $SERVICES; do ARGS="$ARGS --service $(q "$s")"; done

    cat > "$UNIT" <<UNIT
[Unit]
Description=Pentadeck stats agent (read-only system stats as JSON on :$PORT)
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=$PY -I $DIR/pentadeck_agent.py $ARGS
DynamicUser=yes
Restart=always
RestartSec=5
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=$protect_home
PrivateTmp=yes
MemoryMax=64M

[Install]
WantedBy=multi-user.target
UNIT

    systemctl daemon-reload
    systemctl enable pentadeck-agent.service >/dev/null 2>&1
    systemctl restart pentadeck-agent.service      # restart, not start: a re-run upgrades

    if [ -n "$OLD_PORT" ] && { [ "$OLD_PORT" != "$PORT" ] || [ "$OLD_LAN" != "$LAN" ]; }; then
        firewall_close "$OLD_PORT" "$OLD_LAN"
    fi
    firewall_open "$PORT" "$LAN"

    local ok="" ip
    for i in 1 2 3 4 5 6 7 8 9 10; do
        sleep 1
        if "$PY" -I -c 'import sys, urllib.request; urllib.request.urlopen(sys.argv[1], timeout=2).read()' \
                "http://127.0.0.1:$PORT/stats" 2>/dev/null; then ok=1; break; fi
    done
    echo
    if [ -z "$ok" ]; then
        systemctl --no-pager --lines=15 status pentadeck-agent.service || true
        die "the agent did not answer on http://127.0.0.1:$PORT/stats"
    fi
    ip="$(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for (i = 1; i < NF; i++) if ($i == "src") { print $(i + 1); exit }}')"
    [ -n "$ip" ] || ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
    echo "The agent is running as \"$NAME\", reporting: $DISKS"
    echo
    echo "In the device's settings page (Data card), add this source:"
    echo "    http://${ip:-<this-host-ip>}:$PORT/stats"
    echo
    echo "Remove it with: sudo bash uninstall-linux.sh"
}

main "$@"
