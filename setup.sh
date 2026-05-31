#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

if [ "$(uname -s)" != "Linux" ]; then
    echo "./setup.sh must run on Linux." >&2
    exit 1
fi

if [ "$(id -u)" -eq 0 ]; then
    echo "Run setup as your normal Linux user, not root." >&2
    exit 1
fi

echo "Installing dependencies, building Cat, and preparing the bundled botpanel..."
bash "$script_dir/botpanel/update" "$@"

echo
echo "Setup complete."
echo "Start the botpanel with ./botpanel/start"


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
