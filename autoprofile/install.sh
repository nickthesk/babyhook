#!/bin/bash
cd ..
echo "Installing dependencies..."
python3 -m venv autoprofile/.venv
source autoprofile/.venv/bin/activate
python3 -m pip install --upgrade pip
pip install -r autoprofile/requirements.txt
echo "Done!"
