#!/bin/sh
# Build and run the tests. Run from MSYS2 MINGW64 with GTK 4 installed.
#
#   tests/run.sh              the current source
#   tests/run.sh --original   the same tests against the first commit: the
#                             render test must still pass, and app_test and
#                             dist_test must fail every "regression:" check
#                             and nothing else
#
# app_test and dist_test open real windows; they close by themselves.
set -eu
cd "$(dirname "$0")/.."

out=tests/build
mkdir -p "$out"
CFLAGS="-Wall -Wextra -Werror"
GTK=$(pkg-config --cflags --libs gtk4)

if [ "${1:-}" = "--original" ]; then
    first=$(git rev-list --max-parents=0 HEAD)
    rm -rf "$out/original"
    mkdir -p "$out/original"
    git archive "$first" | tar -x -C "$out/original"

    echo "== render_test, original"
    gcc $CFLAGS -I"$out/original" -DCLOCK_C='"clock.c"' tests/render_test.c \
        -o "$out/render_original.exe" $GTK -lm
    "$out/render_original.exe" tests/golden

    echo "== app_test, original"
    gcc $CFLAGS -I"$out/original" -DCLOCK_C='"clock.c"' tests/app_test.c \
        -o "$out/app_original.exe" $GTK -lm
    "$out/app_original.exe" > "$out/original.txt" 2>/dev/null || true

    echo "== dist_test, original (its own build.sh)"
    (cd "$out/original" && ./build.sh > build.log 2>&1)
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/dist_test.ps1 \
        -Bundle "$(cygpath -w "$out/original/dist")" >> "$out/original.txt" || true
    cat "$out/original.txt"

    expected=$(grep -c '  regression:' "$out/original.txt" || true)
    failed_reg=$(grep -c '^FAIL  regression:' "$out/original.txt" || true)
    failed_other=$(grep '^FAIL  ' "$out/original.txt" | grep -vc '  regression:' || true)
    echo
    if [ "$expected" -gt 0 ] && [ "$failed_reg" -eq "$expected" ] && [ "$failed_other" -eq 0 ]; then
        echo "original: fails exactly its $expected regression checks, as it should"
    else
        echo "original: $failed_reg of $expected regression checks failed, and $failed_other others"
        exit 1
    fi
    exit 0
fi

echo "== face_test"
gcc $CFLAGS tests/face_test.c src/face.c -o "$out/face_test.exe"
"$out/face_test.exe"

echo "== render_test"
gcc $CFLAGS -Isrc -DCLOCK_C='"clock.c"' tests/render_test.c src/face.c \
    -o "$out/render_test.exe" $GTK -lm
"$out/render_test.exe" tests/golden

echo "== app_test"
gcc $CFLAGS -Isrc -DCLOCK_C='"clock.c"' tests/app_test.c src/face.c \
    -o "$out/app_test.exe" $GTK -lm
"$out/app_test.exe"

echo "== dist_test"
./build.sh "$out/dist" > "$out/build.log"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/dist_test.ps1 \
    -Bundle "$(cygpath -w "$out/dist")"
