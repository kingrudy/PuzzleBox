@echo off
setlocal enabledelayedexpansion

rem Build and upload one Chronolab Puzzlebox node.
rem
rem Usage:
rem   build-upload.bat <environment> [COMx]
rem
rem   environment: main_controller | c3_sidecar | s3_display | round_display_2424
rem   COMx:        optional serial port override, e.g. COM13
rem
rem If you omit COMx, this script asks PlatformIO which serial ports it can
rem see and picks the first non-Bluetooth one (see detect-port.ps1). That's
rem right as long as exactly one board is plugged in — with more than one,
rem pass the port explicitly.

set "PIO=platformio"
where %PIO% >nul 2>nul
if errorlevel 1 (
    set "PIO=C:\Users\bwkin\AppData\Roaming\Python\Python314\Scripts\platformio.exe"
)
if not exist "%PIO%" (
    where platformio.exe >nul 2>nul
    if errorlevel 1 (
        echo Could not find platformio. Install it or edit the PIO path in this script.
        exit /b 1
    )
)

set "ENV=%~1"
set "PORT=%~2"

if "%ENV%"=="" goto usage

if "%PORT%"=="" (
    echo No port given, asking PlatformIO which serial ports it can see...
    for /f "usebackq delims=" %%P in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0detect-port.ps1" -PioPath "%PIO%"`) do set "PORT=%%P"
)

if "%PORT%"=="" (
    echo Could not auto-detect a serial port.
    echo.
    echo Either nothing is plugged in, only Bluetooth virtual ports are present,
    echo or more than one board is connected. Check Windows Device Manager for
    echo a "Silicon Labs CP210x", "CH340", or "USB Serial" entry and pass its
    echo port explicitly:
    echo   build-upload.bat %ENV% COMx
    exit /b 1
)

echo Building and uploading %ENV% to %PORT% ...
"%PIO%" run -e %ENV% -t upload --upload-port %PORT%
if errorlevel 1 (
    echo.
    echo Upload failed. If it stopped at "Connecting..." with no response:
    echo   1. Hold BOOT, tap EN/RESET once while still holding BOOT, then
    echo      release BOOT after a couple more seconds.
    echo   2. Try a different USB cable - some are charge-only, no data lines.
    echo   3. Try a different USB port on the PC.
    echo   4. Confirm this is actually the board you meant to flash - COMx
    echo      auto-detect just grabs the first non-Bluetooth port it sees.
    exit /b 1
)
exit /b 0

:usage
echo Usage: build-upload.bat ^<environment^> [COMx]
echo.
echo   environment: main_controller ^| c3_sidecar ^| s3_display ^| round_display_2424
echo   COMx:        optional serial port override, e.g. COM13
echo.
echo Omit COMx to auto-detect the first non-Bluetooth serial port.
exit /b 1
