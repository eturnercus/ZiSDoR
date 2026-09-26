#!/usr/bin/env bash
# Сборка Linux-версии. Результат: dist/GDZLauncher-linux-x86_64.tar.gz
# Зависимости (Ubuntu/Debian):
#   sudo apt install build-essential cmake pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev libcurl4-openssl-dev
# Адрес API и сервер можно передать переменными окружения GDZ_API_URL и GDZ_SERVER_ADDRESS.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DGDZ_API_URL="${GDZ_API_URL:-}" -DGDZ_SERVER_ADDRESS="${GDZ_SERVER_ADDRESS:-}"
cmake --build build --parallel

rm -rf dist/linux && mkdir -p dist/linux/GDZLauncher
cp build/build/launcher dist/linux/GDZLauncher/
strip dist/linux/GDZLauncher/launcher
tar -C dist/linux -czf dist/GDZLauncher-linux-x86_64.tar.gz GDZLauncher
echo "Готово: dist/GDZLauncher-linux-x86_64.tar.gz (запуск: ./GDZLauncher/launcher)"
