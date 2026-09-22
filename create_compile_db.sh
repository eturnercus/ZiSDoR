#!/usr/bin/env sh

if test -d build_linux; then
		echo "Перегенерация build_linux..."
		rm -rf build_linux
else
		echo "Генерация build_linux..."
fi

cmake -B build_linux -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON


if test -d build_windows; then
		echo "Перегенерация build_windows..."
		rm -rf build_windows
else
		echo "Генерация build_windows..."
fi

cmake -B build_windows -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_TOOLCHAIN_FILE=mingw-w64.cmake 

