#!/bin/sh
# Redraw tests/golden/ from the first commit's clock.c, which is the look the
# render test holds the current code to. Run from MSYS2 MINGW64.
set -eu
cd "$(dirname "$0")/.."

first=$(git rev-list --max-parents=0 HEAD)
orig=tests/build/original
mkdir -p "$orig" tests/golden
git show "$first:clock.c" > "$orig/clock.c"

gcc -Wall -Wextra -I"$orig" -DCLOCK_C='"clock.c"' tests/render_test.c \
    -o tests/build/render_golden.exe $(pkg-config --cflags --libs gtk4) -lm
tests/build/render_golden.exe tests/golden --write
