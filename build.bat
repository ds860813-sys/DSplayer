@echo off
rem Build DSplayer: build.bat [Debug^|Release]
setlocal

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found - is Visual Studio installed?
    exit /b 1
)

set "VSDIR="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -version 18.0 -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio 2026 with C++ tools not found.
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

msbuild "%~dp0Player.sln" /nologo /m /p:Configuration=%CONFIG% /p:Platform=x64
if errorlevel 1 exit /b 1

echo.
echo Built: %~dp0bin\x64\%CONFIG%\DSplayer.exe
