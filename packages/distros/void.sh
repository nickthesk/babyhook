#!/usr/bin/env bash
set -euo pipefail

run_as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        echo "This installer needs root privileges. Install sudo or run as root." >&2
        exit 1
    fi
}

run_as_root xbps-install -Sy \
    SDL2-devel \
    ca-certificates \
    cmake \
    firejail \
    gcc \
    gdb \
    git \
    glew-devel \
    iproute2 \
    make \
    mesa-dri-devel \
    net-tools \
    nodejs \
    libopenal \
    pkg-config \
    rsync \
    vulkan-headers \
    vulkan-loader-devel \
    wget \
    xauth \
    xorg-server-xvfb \
    xpra

if xbps-query -Rs '^execstack-' >/dev/null 2>&1; then
    run_as_root xbps-install -Sy execstack || true
else
    echo "execstack is not available in the configured Void repositories; build.sh will skip it."
fi
