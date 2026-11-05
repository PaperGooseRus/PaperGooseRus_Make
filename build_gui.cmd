@echo off
setlocal
if exist gui\gtk_app.cpp (
  echo [WARNING] Legacy gui\gtk_app.cpp found. GTK4 implementation is header-only.
  echo [WARNING] Remove the old file to avoid accidental compilation.
)
rem Сборка графического приложения GTK4 в терминале MSYS2 UCRT64 или CMD с настроенным PATH.
where pkg-config >nul 2>&1
if errorlevel 1 (
  echo [ERROR] pkg-config was not found in PATH.
  exit /b 1
)
pkg-config --exists gtk4
if errorlevel 1 (
  echo [ERROR] GTK4 development libraries were not found.
  exit /b 1
)
for /f "delims=" %%A in ('pkg-config --cflags gtk4') do set "GTK_CFLAGS=%%A"
for /f "delims=" %%A in ('pkg-config --libs gtk4') do set "GTK_LIBS=%%A"
if not exist build mkdir build
g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread -DPGR_WITH_GTK4 %GTK_CFLAGS% main.cpp -o build\pgr_gui.exe %GTK_LIBS%
if errorlevel 1 exit /b 1
echo [OK] build\pgr_gui.exe
exit /b 0
