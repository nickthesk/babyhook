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

if command -v dnf >/dev/null 2>&1; then
    pkg_manager=(dnf)
elif command -v yum >/dev/null 2>&1; then
    pkg_manager=(yum)
else
    echo "dnf or yum is required for Fedora/RHEL dependency installation." >&2
    exit 1
fi

run_as_root "${pkg_manager[@]}" -y install \
    SDL2-devel \
    ca-certificates \
    cmake \
    firejail \
    gcc \
    gcc-c++ \
    gdb \
    git \
    glew-devel \
    glew-static \
    iproute \
    make \
    mesa-libGL-devel \
    net-tools \
    nodejs \
    npm \
    openal-soft \
    pkgconf-pkg-config \
    rsync \
    vulkan-headers \
    vulkan-loader-devel \
    wget \
    xorg-x11-drv-dummy \
    xorg-x11-server-Xvfb \
    xorg-x11-xauth \
    xpra

if "${pkg_manager[@]}" list --available execstack >/dev/null 2>&1; then
    run_as_root "${pkg_manager[@]}" -y install execstack
else
    echo "execstack package is not available on this repository; build.sh will skip it."
fi
