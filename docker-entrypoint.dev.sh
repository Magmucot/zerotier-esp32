#!/bin/sh
set -eu

export PATH="/root/.local/bin:$PATH"

# Load ESP-IDF environment from the Espressif base image.
# export.sh is sourced under relaxed shell mode because strict mode (-e/-u)
# can otherwise make the script exit early.
if [ -f /opt/esp/idf/export.sh ]; then
    set +eu
    . /opt/esp/idf/export.sh >/dev/null 2>&1
    set -eu
fi

INSTALL_CLI_IF_MISSING="${INSTALL_CLI_IF_MISSING:-1}"
CHECK_ESP_IDF_TOOLS="${CHECK_ESP_IDF_TOOLS:-1}"
INSTALL_MISSING_ESP_IDF_TOOLS="${INSTALL_MISSING_ESP_IDF_TOOLS:-1}"

CODEX_NPM_PACKAGE="${CODEX_NPM_PACKAGE:-@openai/codex}"

# zt-alpine lives behind sdwan/br-zt-linux. 10.20.0.0/24 must not be
# owned locally by a QEMU TAP interface in zt-dev.
SETUP_ZT_ALPINE_ROUTE="${SETUP_ZT_ALPINE_ROUTE:-1}"
ZT_ALPINE_SUBNET="${ZT_ALPINE_SUBNET:-10.20.0.0/24}"
ZT_ALPINE_TEST_IP="${ZT_ALPINE_TEST_IP:-10.20.0.20}"
ZT_ALPINE_GATEWAY="${ZT_ALPINE_GATEWAY:-192.168.100.250}"
ZT_ALPINE_ROUTE_DEV="${ZT_ALPINE_ROUTE_DEV:-}"

mkdir -p /root/.local/bin
mkdir -p /run/sshd

ssh-keygen -A >/dev/null 2>&1 || true

if ! pgrep -x sshd >/dev/null 2>&1; then
    /usr/sbin/sshd
fi

route_dev_for_gateway() {
    _gateway="$1"

    ip route get "$_gateway" 2>/dev/null | awk '
        {
            for (i = 1; i <= NF; i++) {
                if ($i == "dev") {
                    print $(i + 1)
                    exit
                }
            }
        }'
}

cleanup_stale_zt_alpine_tap_route() {
    # A stale/old QEMU setup used tap0=10.20.0.1/24. That makes zt-dev
    # treat zt-alpine as local-link and breaks routing after reboot/restart.
    if ip link show tap0 >/dev/null 2>&1; then
        if ip -4 addr show dev tap0 2>/dev/null | grep -q 'inet 10\.20\.0\.1/24'; then
            echo "Removing stale tap0 address for ${ZT_ALPINE_SUBNET}."
            ip addr del 10.20.0.1/24 dev tap0 2>/dev/null || true
        fi
    fi

    if ip route show "${ZT_ALPINE_SUBNET}" 2>/dev/null | grep -q ' dev tap0'; then
        echo "Removing stale tap0 route for ${ZT_ALPINE_SUBNET}."
        ip route del "${ZT_ALPINE_SUBNET}" dev tap0 2>/dev/null || true
    fi

    if command -v pkill >/dev/null 2>&1; then
        pkill -f 'dnsmasq .*--interface=tap0 .*--dhcp-range=10\.20\.0\.' 2>/dev/null || true
    fi
}

setup_zt_alpine_route() {
    if [ "${SETUP_ZT_ALPINE_ROUTE}" != "1" ]; then
        echo "zt-alpine route setup disabled."
        return 0
    fi

    if ! command -v ip >/dev/null 2>&1; then
        echo "WARNING: ip command missing; skipping zt-alpine route setup."
        return 0
    fi

    cleanup_stale_zt_alpine_tap_route

    _route_dev="${ZT_ALPINE_ROUTE_DEV}"
    if [ -z "$_route_dev" ]; then
        _route_dev="$(route_dev_for_gateway "${ZT_ALPINE_GATEWAY}" || true)"
    fi
    if [ -z "$_route_dev" ]; then
        for _candidate in eth1 eth0; do
            if ip link show "$_candidate" >/dev/null 2>&1; then
                _route_dev="$_candidate"
                break
            fi
        done
    fi

    if [ -z "$_route_dev" ]; then
        echo "WARNING: could not determine route device for ${ZT_ALPINE_GATEWAY}; skipping zt-alpine route setup."
        return 0
    fi

    echo "Installing zt-alpine route: ${ZT_ALPINE_SUBNET} via ${ZT_ALPINE_GATEWAY} dev ${_route_dev}."
    ip route replace "${ZT_ALPINE_SUBNET}" via "${ZT_ALPINE_GATEWAY}" dev "$_route_dev" onlink || true

    echo "zt-alpine route decision:"
    ip route get "${ZT_ALPINE_TEST_IP}" || true
}

setup_qemu_net() {
    if [ "${SETUP_QEMU_NET:-1}" != "1" ]; then
        echo "QEMU network setup disabled."
        return 0
    fi

    if ! command -v ip >/dev/null 2>&1; then
        echo "WARNING: ip command missing; skipping QEMU network setup."
        return 0
    fi

    if [ ! -e /dev/net/tun ]; then
        echo "WARNING: /dev/net/tun missing; skipping QEMU network setup."
        return 0
    fi

    echo "Setting up QEMU TAP/LAN bridge network..."

    # Create IP-less L2 bridge for QEMU
    ip link add br-eth2 type bridge 2>/dev/null || true
    ip link set br-eth2 up

    # Remove Docker-assigned IP from eth2, it must be an IP-less L2 port
    ip addr flush dev eth2 2>/dev/null || true

    # Attach eth2 (macvlan endpoint on sdwan eth2) to br-eth2
    ip link set eth2 master br-eth2 2>/dev/null || true
    ip link set eth2 up || true

    # Disable multicast/broadcast flooding on eth2 port to prevent interrupt storms
    bridge link set dev eth2 mcast_flood off 2>/dev/null || true
    bridge link set dev eth2 bcast_flood off 2>/dev/null || true

    # Create tap-eth2 and attach to br-eth2
    ip tuntap add dev tap-eth2 mode tap 2>/dev/null || true
    ip link set tap-eth2 master br-eth2 2>/dev/null || true
    ip link set tap-eth2 up || true

    # Start dnsmasq with MAC reservation for QEMU
    # QEMU MAC 02:12:34:56:78:9a always receives 192.168.100.245
    #
    # rm -f /tmp/dnsmasq-qemu.pid
    # dnsmasq --interface=br-eth2 \
            # --bind-interfaces \
            # --dhcp-range=192.168.100.245,192.168.100.245,12h \
            # --dhcp-host=02:12:34:56:78:9a,192.168.100.245 \
            # --dhcp-option=3,192.168.100.254 \
            # --dhcp-option=6,192.168.100.254 \
            # --no-resolv \
            # --pid-file=/tmp/dnsmasq-qemu.pid \
            # --log-facility=/tmp/dnsmasq-qemu.log || true

    echo "QEMU bridge state:"
    ip -br addr show br-eth2 || true
    bridge link show br-eth2 || true
}

install_claude_if_missing() {
    if command -v claude >/dev/null 2>&1; then
        echo "Claude already installed."
    elif [ "$INSTALL_CLI_IF_MISSING" = "1" ]; then
        echo "Installing Claude into mounted /root..."
        if curl -fsSL https://claude.ai/install.sh | bash; then
            echo "Claude installed."
        else
            echo "WARNING: Claude install failed; continuing startup."
        fi
    else
        echo "Claude missing; auto-install disabled."
    fi

    if [ -x /root/.local/bin/claude ]; then
        ln -sf /root/.local/bin/claude /usr/local/bin/claude
    fi
}

install_codex_if_missing() {
    npm config set prefix /root/.local >/dev/null 2>&1 || true
    export PATH="/root/.local/bin:$PATH"

    if command -v codex >/dev/null 2>&1; then
        echo "Codex already installed."
    elif [ "$INSTALL_CLI_IF_MISSING" = "1" ]; then
        echo "Installing Codex into mounted /root/.local..."
        if npm install -g "$CODEX_NPM_PACKAGE@latest"; then
            echo "Codex installed."
        else
            echo "WARNING: Codex install failed; continuing startup."
        fi
        npm cache clean --force >/dev/null 2>&1 || true
    else
        echo "Codex missing; auto-install disabled."
    fi

    if [ -x /root/.local/bin/codex ]; then
        ln -sf /root/.local/bin/codex /usr/local/bin/codex
    fi
}

ensure_esp_idf_tools() {
    if [ "$CHECK_ESP_IDF_TOOLS" != "1" ]; then
        echo "ESP-IDF tool check disabled."
        return 0
    fi

    if [ -z "${IDF_PATH:-}" ]; then
        echo "ESP-IDF IDF_PATH is not set; skipping ESP-IDF tool check."
        return 0
    fi

    if [ ! -f "$IDF_PATH/tools/idf_tools.py" ]; then
        echo "ESP-IDF tools script not found at $IDF_PATH/tools/idf_tools.py; skipping ESP-IDF tool check."
        return 0
    fi

    echo "ESP-IDF version:"
    idf.py --version || true

    echo "ESP-IDF tools path: ${IDF_TOOLS_PATH:-$HOME/.espressif}"

    echo "Checking ESP-IDF required tools..."
    if python "$IDF_PATH/tools/idf_tools.py" check >/tmp/idf-tools-check.log 2>&1; then
        echo "ESP-IDF required tools OK."
    else
        cat /tmp/idf-tools-check.log || true

        if [ "$INSTALL_MISSING_ESP_IDF_TOOLS" = "1" ]; then
            echo "Installing missing ESP-IDF required tools..."
            python "$IDF_PATH/tools/idf_tools.py" install required || true
        else
            echo "Missing ESP-IDF tools detected, but auto-install is disabled."
        fi
    fi
}

#install_claude_if_missing
#install_codex_if_missing
#ensure_esp_idf_tools

setup_qemu_net
setup_zt_alpine_route

/root/share-services/webdav/start-webdav.sh || true

# Route replies to 192.168.9.0/24 via LAN/macvlan interface
if ip link show eth0 >/dev/null 2>&1 && ip addr show dev eth0 | grep -q '192\.168\.100\.251'; then
  ip route replace 192.168.9.0/24 via 192.168.100.254 dev eth0 onlink || true
fi

exec "$@"
