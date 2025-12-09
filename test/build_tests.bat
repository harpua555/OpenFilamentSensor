@echo off
echo Building pulse simulator tests...

REM Check if g++ is available
where g++ >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: g++ not found. Please install MinGW or MSYS2.
    echo Download from: https://www.msys2.org/
    exit /b 1
)

REM Generate settings header from user_settings
echo Generating test settings from ..\data\user_settings.json...
where python >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    python generate_test_settings.py
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: Failed to generate test settings header
        exit /b %ERRORLEVEL%
    )
) else (
    echo Warning: python not found; using existing generated_test_settings.h
)

REM Compile test suite (include test dir first for Arduino.h mock)
g++ -std=c++11 -o pulse_simulator.exe pulse_simulator.cpp -I. -I..
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Compilation failed
    exit /b 1
)

echo Build successful!
echo.
echo Running tests...
echo.

REM Run tests
pulse_simulator.exe
set TEST_RESULT=%ERRORLEVEL%

echo.
if %TEST_RESULT% EQU 0 (
    echo All tests passed!
) else (
    echo Some tests failed. See output above.
)

exit /b %TEST_RESULT%
