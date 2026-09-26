#!/bin/bash
# Скрипт для настройки автокомплита YCM (база компиляции для Linux-сборки).
# Windows собирается только MSVC (см. CMakeLists.txt и CI), поэтому базы для MinGW больше нет.

set -e

BUILD_LINUX="build_linux"
FINAL_DB="compile_commands.json"

echo "--- Configuring for Linux (Native) ---"
mkdir -p "$BUILD_LINUX"
cmake -S . -B "$BUILD_LINUX" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

if [ -f "$BUILD_LINUX/compile_commands.json" ]; then
    cp "$BUILD_LINUX/compile_commands.json" "$FINAL_DB"

    echo "--------------------------------------------------"
    echo "Success! Compilation database generated: $FINAL_DB"
    echo "YCM will now use Linux flags. Files in src/windows are not covered (MSVC only)."
    echo "Restart Vim or run :YcmRestart"
else
    echo "Error: $BUILD_LINUX/compile_commands.json was not generated."
    exit 1
fi
