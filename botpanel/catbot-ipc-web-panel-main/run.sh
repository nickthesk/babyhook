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
