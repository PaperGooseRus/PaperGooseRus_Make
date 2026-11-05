#!/usr/bin/env bash
set -euo pipefail
# GTK4 UCRT64 / Linux: использовать окружение со всеми пакетами gtk4.
cd "$(dirname "$0")"
if [[ -f gui/gtk_app.cpp ]]; then
  printf "[WARNING] Legacy gui/gtk_app.cpp remains. GTK4 is header-only; remove this obsolete file.\n" >&2
fi
mkdir -p build
g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread -DPGR_WITH_GTK4 \
  $(pkg-config --cflags gtk4) main.cpp -o build/pgr_gui \
  $(pkg-config --libs gtk4)
printf '[OK] build/pgr_gui\n'
