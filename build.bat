@echo off
REM ---------------------------------------------------------------------------
REM Builds Interact.dll (32-bit).
REM
REM Requires: Visual Studio Build Tools with the "Desktop development with C++"
REM workload. See BUILDING.md. Nothing else -- no submodules, no CMake.
REM ---------------------------------------------------------------------------

setlocal enabledelayedexpansion
cd /d "%~dp0"

REM --- locate Visual Studio -------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo.
    echo   Could not find vswhere.exe, so Visual Studio is probably not installed.
    echo   See BUILDING.md - you need the C++ build tools.
    echo.
    pause
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"

if not defined VSPATH (
    echo.
    echo   Visual Studio is installed, but the C++ tools are not.
    echo   Re-run the installer and tick "Desktop development with C++".
    echo.
    pause
    exit /b 1
)

REM vcvars32 = 32-bit target. The client is 32-bit; this must not be vcvars64.
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 (
    echo   Failed to initialise the 32-bit build environment.
    pause
    exit /b 1
)

REM --- build ----------------------------------------------------------------
echo Compiling Interact.dll (x86) ...

if exist Interact.dll del /q Interact.dll

REM /MT statically links the CRT, so the DLL has no VC++ redist dependency.
cl /nologo /LD /O2 /MT /EHsc /W3 /DWIN32 /D_WINDOWS ^
   dllmain.cpp Game.cpp ^
   /Fe:Interact.dll ^
   /link /MACHINE:X86 /SUBSYSTEM:WINDOWS user32.lib kernel32.lib

if not exist Interact.dll (
    echo.
    echo   BUILD FAILED - see the compiler errors above.
    echo.
    pause
    exit /b 1
)

del /q *.obj *.exp *.lib 2>nul

REM --- try to install it ----------------------------------------------------
REM Look for the game folder in the usual places relative to this script.
set "GAMEROOT="
for %%D in ("%~dp0.." "%~dp0..\.." "%~dp0..\..\..\..") do (
    if exist "%%~fD\WoW.exe" if not defined GAMEROOT set "GAMEROOT=%%~fD"
)

if defined GAMEROOT (
    copy /y Interact.dll "!GAMEROOT!\Interact.dll" >nul
    echo.
    echo   Built OK and copied to:
    echo     !GAMEROOT!
    echo.
) else (
    echo.
    echo   Built OK: %~dp0Interact.dll
    echo   Could not find WoW.exe automatically - copy Interact.dll next to it
    echo   yourself.
    echo.
)

pause
endlocal
