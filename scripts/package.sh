#!/usr/bin/env bash
# Build a release folder for the current OS. The result is used by GitHub
# releases and by the Steam depot templates in steam/.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD_DIR:-build}"
OS="$(uname -s)"
case "$OS" in
    Linux*)  PLATFORM=linux ;;
    Darwin*) PLATFORM=macos ;;
    MINGW*|MSYS*|CYGWIN*) PLATFORM=windows ;;
    *) echo "Unsupported OS: $OS" >&2; exit 1 ;;
esac

if [ ! -d "$BUILD_DIR" ]; then
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

OUT="dist/$PLATFORM"
rm -rf "$OUT"
mkdir -p "$OUT/assets/fonts" "$OUT/maps"

case "$PLATFORM" in
    windows)
        cp "$BUILD_DIR/arena2004.exe" "$OUT/"
        [ -f "$BUILD_DIR/arena2004_lobby.exe" ] && cp "$BUILD_DIR/arena2004_lobby.exe" "$OUT/"
        cp "$BUILD_DIR"/*.dll "$OUT/" 2>/dev/null || true
        ;;
    *)
        cp "$BUILD_DIR/arena2004" "$OUT/"
        [ -f "$BUILD_DIR/arena2004_lobby" ] && cp "$BUILD_DIR/arena2004_lobby" "$OUT/"
        ;;
esac

cp -r assets/* "$OUT/assets/" 2>/dev/null || true
cp maps/*.map "$OUT/maps/" 2>/dev/null || true
cp README.md LICENSE "$OUT/" 2>/dev/null || true
cp steam/steam_appid.txt.example "$OUT/steam_appid.txt.example" 2>/dev/null || true

echo "Release folder: $OUT"
echo "Steam depot maps this folder; GitHub ZIP:"
( cd dist && zip -qr "arena2004-$PLATFORM.zip" "$PLATFORM" && echo "  dist/arena2004-$PLATFORM.zip" )
