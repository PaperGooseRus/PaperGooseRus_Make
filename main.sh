#!/usr/bin/env bash
set -euo pipefail

# Сборка GTK4-приложения (Linux или MSYS2 UCRT64).
cd "$(dirname "$0")"

if ! command -v g++ >/dev/null 2>&1; then
    echo '[ERROR] g++ not found in PATH' >&2
    exit 1
fi
if ! command -v pkg-config >/dev/null 2>&1; then
    echo '[ERROR] pkg-config not found in PATH' >&2
    exit 1
fi
if ! pkg-config --exists gtk4; then
    echo '[ERROR] gtk4 not found by pkg-config' >&2
    echo 'MSYS2 UCRT64: pacman -S mingw-w64-ucrt-x86_64-gtk4' >&2
    exit 1
fi

echo "[INFO] GTK4 version: $(pkg-config --modversion gtk4)"
mkdir -p build

# Намеренное разбиение результата pkg-config на аргументы компилятора.
g++ \
    -Icore -Igui \
    $(pkg-config --cflags gtk4) \
    pgr_app.cpp gui/gtk_app.cpp \
    -Os -std=c++17 -Wall -Wextra -Werror \
    $(pkg-config --libs gtk4) \
    -o ./build/main.exe

echo '[OK] Built: build/main.exe'
./build/main.exe
