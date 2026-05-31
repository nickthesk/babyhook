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

run_as_root zypper --non-interactive install --no-recommends \
    SDL2-devel \
    ca-certificates \
    cmake \
    firejail \
    gcc-c++ \
    gdb \
    git \
    glew-devel \
    iproute2 \
    libGL-devel \
    make \
    net-tools \
    nodejs \
    npm \
    openal-soft \
    pkgconf-pkg-config \
    rsync \
    vulkan-devel \
    vulkan-headers \
    wget \
    xauth \
    xorg-x11-server \
    xorg-x11-server-extra \
    xpra

if zypper --non-interactive search --match-exact execstack | grep -q 'execstack'; then
    run_as_root zypper --non-interactive install --no-recommends execstack
else
    echo "execstack is not available in the configured zypper repositories; build.sh will skip it."
fi


# Fix permissions at the end of the script, but only if we are the main script
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
    _fix_script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
    _fix_repo_root="$_fix_script_dir"
    while [ ! -d "$_fix_repo_root/.git" ] && [ "$_fix_repo_root" != "/" ]; do
        _fix_repo_root="$(dirname -- "$_fix_repo_root")"
    done

    if [ -f "$_fix_repo_root/botpanel/fix_permissions" ]; then
        bash "$_fix_repo_root/botpanel/fix_permissions" --once
    fi
fi
