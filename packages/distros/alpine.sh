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

run_as_root apk add --no-cache \
    build-base \
    ca-certificates \
    cmake \
    firejail \
    gdb \
    git \
    glew-dev \
    iproute2 \
    make \
    mesa-dev \
    net-tools \
    nodejs \
    npm \
    openal-soft \
    pkgconf \
    rsync \
    sdl2-dev \
    vulkan-headers \
    vulkan-loader-dev \
    wget \
    xauth \
    xpra \
    xvfb

if apk search --exact execstack | grep -q '^execstack'; then
    run_as_root apk add --no-cache execstack
else
    echo "execstack is not available in the configured Alpine repositories; build.sh will skip it."
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
