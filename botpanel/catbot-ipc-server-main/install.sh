#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"

if [ "$(id -u)" -ne 0 ]; then
    echo "MUST RUN AS SUDO: sudo ./install.sh" >&2
    exit 1
fi

make -C "$script_dir" REPO_ROOT="$repo_root" clean
make -C "$script_dir" REPO_ROOT="$repo_root" -j"$(nproc --all)"
make -C "$script_dir" REPO_ROOT="$repo_root" install
chmod -R u=rwX,go=rX /opt/cathook/ipc


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
