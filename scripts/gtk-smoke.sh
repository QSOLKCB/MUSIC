#!/bin/sh
set -eu
mkdir -p artifacts
GSK_RENDERER=cairo GTK_A11Y=none ./build/music --smoke >artifacts/gtk-smoke.log 2>&1 &
music_pid=$!
sleep 2
import -window root artifacts/music-preview.png
wait "$music_pid"
cat artifacts/gtk-smoke.log
if grep -E 'CRITICAL|ERROR|FAIL' artifacts/gtk-smoke.log; then exit 1; fi
grep -q 'GTK/audio smoke: PASS' artifacts/gtk-smoke.log
