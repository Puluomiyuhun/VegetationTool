@echo off
echo ========================================
echo  VegetationTool Build Script
echo ========================================

:: 检查VCPKG_ROOT
if "%VCPKG_ROOT%"=="" (
    echo [ERROR] Please set VCPKG_ROOT environment variable
    echo   Example: set VCPKG_ROOT=C:\vcpkg
    pause
    exit /b 1
)

echo [1/3] Installing dependencies via vcpkg...
%VCPKG_ROOT%\vcpkg.exe install --triplet x64-windows
if errorlevel 1 (
    echo [ERROR] vcpkg install failed
    pause
    exit /b 1
)

echo [2/3] Configuring CMake...
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if errorlevel 1 (
    echo [ERROR] CMake configure failed
    pause
    exit /b 1
)

echo [3/3] Building (Release)...
cmake --build build --config Release --parallel
if errorlevel 1 (
    echo [ERROR] Build failed
    pause
    exit /b 1
)

echo.
echo ========================================
echo  Build SUCCESS!
echo  Run: build\Release\VegetationTool.exe
echo ========================================
pause
