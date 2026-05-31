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

run_as_root emerge --ask=n --noreplace \
    dev-build/cmake \
    dev-debug/gdb \
    dev-util/pkgconf \
    dev-util/vulkan-headers \
    dev-vcs/git \
    media-libs/glew \
    media-libs/libsdl2 \
    media-libs/openal \
    net-misc/xpra \
    net-libs/nodejs \
    sys-devel/gcc \
    sys-apps/firejail \
    sys-apps/iproute2 \
    sys-apps/net-tools \
    net-misc/rsync \
    virtual/opengl \
    wget \
    x11-apps/xauth \
    x11-base/xorg-server

if emerge --search dev-util/execstack >/dev/null 2>&1; then
    run_as_root emerge --ask=n --noreplace dev-util/execstack || true
else
    echo "execstack is not available in the configured Gentoo repositories; build.sh will skip it."
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
