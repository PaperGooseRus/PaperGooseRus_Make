@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

REM ============================================================
REM  Сборка GTK4-приложения через g++ + pkg-config
REM ============================================================

REM ---------------- Настройки (можно переопределить) ----------
if not defined MSYS2_ROOT  set "MSYS2_ROOT=C:\msys64"
if not defined UCRT_BIN   set "UCRT_BIN=%MSYS2_ROOT%\ucrt64\bin"
if not defined PKG_CONFIG  set "PKG_CONFIG=%UCRT_BIN%\pkg-config.exe"

if not defined OUTDIR      set "OUTDIR=build"
if not defined OUTEXE      set "OUTEXE=%OUTDIR%\main.exe"
if not defined SRC         set "SRC=pgr_app.cpp gui\gtk_app.cpp"
if not defined INCLUDES    set "INCLUDES=-Icore -Igui"
if not defined CXXFLAGS    set "CXXFLAGS=-Os -std=c++17 -Wall -Wextra -Werror"
if not defined RUN_AFTER   set "RUN_AFTER=1"
REM ------------------------------------------------------------

REM ================= Проверки окружения ======================
if not exist "%PKG_CONFIG%" (
    echo [ERROR] pkg-config not found: %PKG_CONFIG%
    echo Adjust MSYS2_ROOT in this script or install MSYS2 + GTK4.
    exit /b 1
)

REM Кладём ucrt64\bin в начало PATH — это нужно и для pkg-config,
REM и для g++, и для запуска main.exe (DLL ищутся в PATH).
set "PATH=%UCRT_BIN%;%PATH%"

where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ not found in PATH.
    exit /b 1
)

"%PKG_CONFIG%" --exists gtk4
if errorlevel 1 (
    echo [ERROR] gtk4 not found by pkg-config.
    echo Install: pacman -S mingw-w64-ucrt-x86_64-gtk4
    exit /b 1
)

for /f "delims=" %%v in ('pkg-config --modversion gtk4') do set "GTK_VER=%%v"
echo [INFO] GTK4 version: !GTK_VER!

REM ================= Собираем флаги GTK4 =====================
set "GTK_CFLAGS="
for /f "delims=" %%i in ('pkg-config --cflags gtk4') do set "GTK_CFLAGS=!GTK_CFLAGS! %%i"

set "GTK_LIBS="
for /f "delims=" %%i in ('pkg-config --libs gtk4') do set "GTK_LIBS=!GTK_LIBS! %%i"

if not defined GTK_CFLAGS (
    echo [ERROR] pkg-config --cflags gtk4 returned empty
    exit /b 1
)

echo [INFO] gtk4 cflags:!GTK_CFLAGS!
echo [INFO] gtk4 libs:  !GTK_LIBS!

REM ================= Сборка ==================================
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

echo.
echo [BUILD] g++ %INCLUDES% !GTK_CFLAGS! %SRC% %CXXFLAGS% !GTK_LIBS! -o "%OUTEXE%"
echo.
g++ %INCLUDES% !GTK_CFLAGS! %SRC% %CXXFLAGS% !GTK_LIBS! -o "%OUTEXE%"
if errorlevel 1 (
    echo [ERROR] Build failed
    exit /b 1
)

echo [OK] Built: %OUTEXE%

REM ================= Запуск =================================
if not "%RUN_AFTER%"=="0" (
    echo.
    echo [RUN] %OUTEXE%
    echo.
    "%OUTEXE%"
    exit /b !errorlevel!
)

endlocal
