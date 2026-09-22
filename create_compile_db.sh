#!/usr/bin/env sh

if test -d compiledb_linux; then
		echo "Перегенерация compiledb_linux..."
		rm -rf compiledb_linux
else
		echo "Генерация compiledb_linux..."
fi

cmake -B compiledb_linux -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON


if test -d compiledb_windows; then
		echo "Перегенерация compiledb_windows..."
		rm -rf compiledb_windows
else
		echo "Генерация compiledb_windows..."
fi

cmake -B compiledb_windows -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_TOOLCHAIN_FILE=mingw-w64.cmake 

