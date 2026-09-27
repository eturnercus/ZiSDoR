#!/usr/bin/env bash
# Упаковывает собранный бинарник Linux в AppImage.
#   scripts/build-appimage.sh [путь к launcher] [файл результата]
# По умолчанию: build/build/launcher -> dist/GDZLauncher-x86_64.AppImage
# appimagetool скачивается автоматически (или берётся из переменной APPIMAGETOOL).
set -euo pipefail
cd "$(dirname "$0")/.."

BIN="${1:-build/build/launcher}"
OUT="${2:-dist/GDZLauncher-x86_64.AppImage}"
[ -x "$BIN" ] || { echo "Нет собранного бинарника: $BIN (сначала scripts/build-linux.sh)" >&2; exit 1; }

TOOL="${APPIMAGETOOL:-build/appimagetool-x86_64.AppImage}"
if [ ! -x "$TOOL" ]; then
	mkdir -p "$(dirname "$TOOL")"
	curl -fsSL -o "$TOOL" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
	chmod +x "$TOOL"
fi

APPDIR="$(mktemp -d)/GDZLauncher.AppDir"
mkdir -p "$APPDIR/usr/bin"
install -m 755 "$BIN" "$APPDIR/usr/bin/launcher"
install -m 755 packaging/linux/AppRun "$APPDIR/AppRun"
install -m 644 packaging/linux/gdzlauncher.desktop "$APPDIR/gdzlauncher.desktop"
install -m 644 packaging/gdzlauncher.png "$APPDIR/gdzlauncher.png"
ln -s gdzlauncher.png "$APPDIR/.DirIcon"

mkdir -p "$(dirname "$OUT")"
# APPIMAGE_EXTRACT_AND_RUN: appimagetool работает и там, где нет FUSE (контейнеры, CI).
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$TOOL" --no-appstream "$APPDIR" "$OUT"
rm -rf "$(dirname "$APPDIR")"
chmod +x "$OUT"
echo "Готово: $OUT"
