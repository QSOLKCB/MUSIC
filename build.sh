#!/bin/sh
set -eu
music_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cmake -S "$music_root" -B "$music_root/build" -DCMAKE_BUILD_TYPE=Release -DMUSIC_BUILD_GUI=ON
cmake --build "$music_root/build" --config Release --parallel 4
ctest --test-dir "$music_root/build" -C Release --output-on-failure
printf '\nBuilt MUSIC. Launch with ./music\n'
