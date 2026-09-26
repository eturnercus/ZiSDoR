#!/usr/bin/env bash
# Сборка Windows-версии из Linux компилятором MinGW-w64. Результат: dist/GDZLauncher-windows-x64.zip
# Зависимости (Ubuntu/Debian): sudo apt install g++-mingw-w64-x86-64 cmake zip
set -euo pipefail
cd "$(dirname "$0")/.."

# Заголовки WebView2 скачиваются с nuget.org. Без доступа к нему укажите распакованный пакет
# Microsoft.Web.WebView2 в переменной MSWebView2_ROOT.
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release \
      -DGDZ_API_URL="${GDZ_API_URL:-}" -DGDZ_SERVER_ADDRESS="${GDZ_SERVER_ADDRESS:-}" \
      ${MSWebView2_ROOT:+-DMSWebView2_ROOT="$MSWebView2_ROOT"}
cmake --build build-win --parallel

rm -rf dist/windows && mkdir -p dist/windows/GDZLauncher
cp build-win/build/Updater.exe build-win/build/launcher.dll dist/windows/GDZLauncher/
rm -f dist/GDZLauncher-windows-x64.zip
(cd dist/windows && zip -qr ../GDZLauncher-windows-x64.zip GDZLauncher)
echo "Готово: dist/GDZLauncher-windows-x64.zip (запуск: GDZLauncher\\Updater.exe)"
