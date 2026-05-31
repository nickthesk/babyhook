#!/usr/bin/env bash
set -euo pipefail

npm install
./node_modules/.bin/browserify script.js -o public/bundle.js
