#!/bin/bash
cd ..
if [ ! -f "autoprofile/.venv/bin/activate" ]; then
    echo "Virtual environment not found. Please run install.sh first."
    exit 1
fi
source autoprofile/.venv/bin/activate
export PYTHONPATH=.
python3 -m autoprofile.cli web


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
