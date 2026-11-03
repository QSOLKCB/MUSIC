#!/usr/bin/env bash
set -euo pipefail
if [[ ${MSYSTEM:-} != UCRT64 ]]; then
  echo 'Run this script in the MSYS2 UCRT64 shell.' >&2
  exit 1
fi
cd -- "$(dirname -- "$0")"
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DMUSIC_BUILD_GUI=ON
cmake --build build-windows --parallel 4
ctest --test-dir build-windows --output-on-failure
python scripts/check-render.py build-windows/demo-reference.wav
python scripts/package-windows.py
python scripts/check-windows-package.py dist/MUSIC-windows-x64.zip
