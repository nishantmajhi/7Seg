#!/bin/bash
# build.sh - Compile the clock and bundle it with the GTK runtime it needs,
# so the folder runs on a Windows machine without MSYS2.
#
#   ./build.sh            builds into dist/
#   ./build.sh FOLDER     builds into FOLDER
#
# Run from MSYS2 MINGW64. The folder is deleted and rebuilt, so it must be
# new, empty, or a previous build (one with clock.exe in it).

cd "$(dirname "$0")"
out="${1:-dist}"

if [ -e "$out" ] && [ -n "$(ls -A "$out" 2>/dev/null)" ] && [ ! -f "$out/clock.exe" ]; then
    echo "Refusing to replace '$out': it is not empty and is not a previous build."
    exit 1
fi

echo "Creating distribution folder $out..."
rm -rf "$out"
mkdir -p "$out"

echo "Compiling src/clock.c..."
gcc -mwindows src/clock.c src/face.c -o "$out/clock.exe" $(pkg-config --cflags --libs gtk4) -lm
if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi
echo "Compilation successful."

echo "Copying DLL dependencies..."
for dll in $(ldd "$out/clock.exe" | grep mingw64 | awk '{print $3}'); do
    echo "  Copying $(basename $dll)"
    cp "$dll" "$out/"
done

# GApplication starts a session bus with gdbus.exe; without it, a warning.
echo "Copying gdbus.exe (to prevent D-Bus warning)..."
if [ -f /mingw64/bin/gdbus.exe ]; then
    cp /mingw64/bin/gdbus.exe "$out/"
    echo "  gdbus.exe copied."
else
    echo "  Warning: gdbus.exe not found in /mingw64/bin/"
fi

echo "Copying GTK runtime data..."
mkdir -p "$out/lib/gdk-pixbuf-2.0/2.10.0/loaders"
mkdir -p "$out/share/icons/hicolor"
mkdir -p "$out/share/glib-2.0/schemas"
mkdir -p "$out/share/themes"

# Copy pixbuf loaders
if [ -d /mingw64/lib/gdk-pixbuf-2.0/2.10.0/loaders ]; then
    cp -r /mingw64/lib/gdk-pixbuf-2.0/2.10.0/loaders/* "$out/lib/gdk-pixbuf-2.0/2.10.0/loaders/"
fi

# Copy icons (Adwaita is the default theme)
if [ -d /mingw64/share/icons/Adwaita ]; then
    cp -r /mingw64/share/icons/Adwaita "$out/share/icons/"
fi

# The window finds its icon here because GLib looks in the share folder
# beside its own DLL, which is beside clock.exe.
echo "Copying app icon..."
mkdir -p "$out/share/icons/hicolor/256x256/apps"
if [ -f icon.png ]; then
    cp icon.png "$out/share/icons/hicolor/256x256/apps/com.example.ledclock.png"
    echo "  icon.png copied."
else
    echo "  Warning: icon.png not found. Place a 256x256 PNG named icon.png next to build.sh"
fi
gtk-update-icon-cache.exe "$out/share/icons/hicolor/" 2>/dev/null || true

# Copy schemas
if [ -d /mingw64/share/glib-2.0/schemas ]; then
    cp /mingw64/share/glib-2.0/schemas/*.gschema.xml "$out/share/glib-2.0/schemas/" 2>/dev/null
    glib-compile-schemas.exe "$out/share/glib-2.0/schemas/"
fi

# Copy theme (optional)
if [ -d /mingw64/share/themes/default ]; then
    cp -r /mingw64/share/themes/default "$out/share/themes/"
fi

echo "Done. The clock is $out/clock.exe."
