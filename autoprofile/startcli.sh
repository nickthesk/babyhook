#!/bin/bash
cd ..
if [ ! -f "autoprofile/.venv/bin/activate" ]; then
    echo "Virtual environment not found. Please run install.sh first."
    exit 1
fi
source autoprofile/.venv/bin/activate
export PYTHONPATH=.
python3 -m autoprofile.cli tui
