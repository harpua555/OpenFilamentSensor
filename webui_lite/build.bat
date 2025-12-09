@echo off
REM Build script for lightweight WebUI (Windows)

echo Building lightweight WebUI...
node build.js

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build successful!
) else (
    echo.
    echo Build failed!
    exit /b 1
)
