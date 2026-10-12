@echo off
rem Builds build\Release\T7RadiantBridge.dll and runs the tests, with the CMake that ships with Visual Studio.
setlocal enabledelayedexpansion
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -property installationPath`) do set "VS=%%i"
if not defined VS (echo Visual Studio not found & exit /b 1)
set "CMAKE=!VS!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "CTEST=!VS!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
if not exist "!CMAKE!" set "CMAKE=cmake" & set "CTEST=ctest"
cd /d "%~dp0"
"!CMAKE!" -S . -B build -A x64 || exit /b 1
"!CMAKE!" --build build --config Release || exit /b 1
"!CTEST!" --test-dir build -C Release --output-on-failure || exit /b 1
