#!/bin/bash

log_path=logs/main.log

if [ $EUID != 0 ]; then
	echo "0"
	exit
fi

mkdir -p logs

node_path="$(command -v node || command -v nodejs || true)"
if [ -z "$node_path" ]; then
	echo "node or nodejs is required to run the web panel." >&2
	exit 1
fi

"$node_path" app.js >"$log_path" 2>&1


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
