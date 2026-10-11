@echo off
rem Builds spike\plugin\out\T7RadiantBridge.dll with the latest Visual Studio (x64, release, static CRT).
setlocal enabledelayedexpansion
rem delayed expansion: the ")" of "Program Files (x86)" would close the for's parenthesis
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -property installationPath`) do set "VS=%%i"
if not defined VS (echo Visual Studio not found & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "HERE=%~dp0"
if not exist "%HERE%out" mkdir "%HERE%out"
cl /nologo /LD /EHsc /O2 /MT /std:c++17 /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /I"%HERE%..\..\third_party\mtplugins_sdk" "%HERE%T7RadiantBridge.cpp" /Fo"%HERE%out\\" /Fe"%HERE%out\T7RadiantBridge.dll" /link /NOLOGO
