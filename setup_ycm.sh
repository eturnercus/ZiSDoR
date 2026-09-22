#!/bin/bash
# Скрипт для настройки автокомплита (Объединенная база Linux & MinGW)
# Спасибо нейродеду за победу.

BUILD_LINUX="build_linux"
BUILD_WIN="build_win"
FINAL_DB="compile_commands.json"

echo "--- Configuring for Linux (Native) ---"
mkdir -p $BUILD_LINUX
cmake -S . -B $BUILD_LINUX -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "--- Configuring for Windows (MinGW) ---"
mkdir -p $BUILD_WIN
cmake -S . -B $BUILD_WIN -DCMAKE_TOOLCHAIN_FILE=mingw-w64.cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

if [ -f "$BUILD_LINUX/compile_commands.json" ] && [ -f "$BUILD_WIN/compile_commands.json" ]; then
    echo "--- Merging databases into $FINAL_DB ---"

    # Извлекаем содержимое массивов из JSON файлов.
    # Используем sed для удаления начальной '[' и конечной ']', а также запятой в конце последней записи.
    # Это позволяет просто склеить записи через запятую.

    # Извлекаем записи из Linux DB (убираем [ и ])
    sed 's/^\[//; s/]$//' "$BUILD_LINUX/compile_commands.json" > temp_linux.json
    # Извлекаем записи из Win DB (убираем [ и ])
    sed 's/^\[//; s/]$//' "$BUILD_WIN/compile_commands.json" > temp_win.json

    # Склеиваем их в один массив
    echo "[" > $FINAL_DB
    cat temp_linux.json >> $FINAL_DB
    echo "," >> $FINAL_DB
    cat temp_win.json >> $FINAL_DB
    echo "]" >> $FINAL_DB

    # Очистка временных файлов
    rm temp_linux.json temp_win.json

    echo "--------------------------------------------------"
    echo "Success! Combined database generated: $FINAL_DB"
else
    echo "Error: One or more compile_commands.json were not generated."
    exit 1
fi
