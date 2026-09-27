#!/usr/bin/env bash
# Сборка Linux-версии. Результат: dist/GDZLauncher-x86_64.AppImage (и обычный бинарник build/build/launcher).
# Зависимости (Ubuntu/Debian):
#   sudo apt install build-essential cmake pkg-config file libgtk-3-dev libwebkit2gtk-4.1-dev libcurl4-openssl-dev
# Адрес API и сервер можно передать переменными окружения GDZ_API_URL и GDZ_SERVER_ADDRESS.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DGDZ_API_URL="${GDZ_API_URL:-}" -DGDZ_SERVER_ADDRESS="${GDZ_SERVER_ADDRESS:-}"
cmake --build build --parallel
strip build/build/launcher

bash scripts/build-appimage.sh build/build/launcher dist/GDZLauncher-x86_64.AppImage
