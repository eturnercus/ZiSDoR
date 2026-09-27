#!/usr/bin/env bash
# Сборка Windows-версии из Linux компилятором MinGW-w64. Результат: dist/GDZLauncher.exe (один файл).
# Зависимости (Ubuntu/Debian): sudo apt install g++-mingw-w64-x86-64 cmake
set -euo pipefail
cd "$(dirname "$0")/.."

# Заголовки WebView2 скачиваются с nuget.org. Без доступа к нему укажите распакованный пакет
# Microsoft.Web.WebView2 в переменной MSWebView2_ROOT.
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release \
      -DGDZ_API_URL="${GDZ_API_URL:-}" -DGDZ_SERVER_ADDRESS="${GDZ_SERVER_ADDRESS:-}" \
      ${MSWebView2_ROOT:+-DMSWebView2_ROOT="$MSWebView2_ROOT"}
cmake --build build-win --parallel

mkdir -p dist
cp build-win/build/GDZLauncher.exe dist/GDZLauncher.exe
echo "Готово: dist/GDZLauncher.exe"
