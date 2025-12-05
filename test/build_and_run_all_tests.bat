@echo off
setlocal enabledelayedexpansion

echo.
echo ╔════════════════════════════════════════════════════════════╗
echo ║     Centauri Carbon Motion Detector Test Suite            ║
echo ║                  Build and Run All Tests                   ║
echo ╚════════════════════════════════════════════════════════════╝
echo.

REM Check for g++
where g++ >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: g++ not found. Please install MinGW or similar.
    exit /b 1
)

echo [✓] Found g++

REM Generate test settings
echo.
echo Generating test settings...
python generate_test_settings.py
if %ERRORLEVEL% NEQ 0 (
    echo Warning: Failed to generate test settings
)

set TOTAL_TESTS=0
set PASSED_TESTS=0
set FAILED_TESTS=0

REM Test FilamentMotionSensor
echo.
echo ═══════════════════════════════════════════════════════════
echo Building: FilamentMotionSensor
echo ═══════════════════════════════════════════════════════════
set /a TOTAL_TESTS+=1

g++ -std=c++11 -o test_filament_sensor.exe test_filament_sensor.cpp -I. -I..
if %ERRORLEVEL% EQU 0 (
    echo [✓] Compilation successful
    echo.
    echo Running: FilamentMotionSensor
    test_filament_sensor.exe
    if !ERRORLEVEL! EQU 0 (
        echo [✓] FilamentMotionSensor PASSED
        set /a PASSED_TESTS+=1
    ) else (
        echo [✗] FilamentMotionSensor FAILED
        set /a FAILED_TESTS+=1
    )
) else (
    echo [✗] Compilation failed
    set /a FAILED_TESTS+=1
)

REM Test JamDetector
echo.
echo ═══════════════════════════════════════════════════════════
echo Building: JamDetector
echo ═══════════════════════════════════════════════════════════
set /a TOTAL_TESTS+=1

g++ -std=c++11 -o test_jam_detector.exe test_jam_detector.cpp -I. -I..
if %ERRORLEVEL% EQU 0 (
    echo [✓] Compilation successful
    echo.
    echo Running: JamDetector
    test_jam_detector.exe
    if !ERRORLEVEL! EQU 0 (
        echo [✓] JamDetector PASSED
        set /a PASSED_TESTS+=1
    ) else (
        echo [✗] JamDetector FAILED
        set /a FAILED_TESTS+=1
    )
) else (
    echo [✗] Compilation failed
    set /a FAILED_TESTS+=1
)

REM Test SDCPProtocol
echo.
echo ═══════════════════════════════════════════════════════════
echo Building: SDCPProtocol
echo ═══════════════════════════════════════════════════════════
set /a TOTAL_TESTS+=1

g++ -std=c++11 -o test_sdcp_protocol.exe test_sdcp_protocol.cpp -I. -I..
if %ERRORLEVEL% EQU 0 (
    echo [✓] Compilation successful
    echo.
    echo Running: SDCPProtocol
    test_sdcp_protocol.exe
    if !ERRORLEVEL! EQU 0 (
        echo [✓] SDCPProtocol PASSED
        set /a PASSED_TESTS+=1
    ) else (
        echo [✗] SDCPProtocol FAILED
        set /a FAILED_TESTS+=1
    )
) else (
    echo [✗] Compilation failed
    set /a FAILED_TESTS+=1
)

REM Test Pulse Simulator
echo.
echo ═══════════════════════════════════════════════════════════
echo Building: Pulse Simulator (Integration)
echo ═══════════════════════════════════════════════════════════
set /a TOTAL_TESTS+=1

g++ -std=c++11 -o pulse_simulator.exe pulse_simulator.cpp -I. -I..
if %ERRORLEVEL% EQU 0 (
    echo [✓] Compilation successful
    echo.
    echo Running: Pulse Simulator
    pulse_simulator.exe
    if !ERRORLEVEL! EQU 0 (
        echo [✓] Pulse Simulator PASSED
        set /a PASSED_TESTS+=1
    ) else (
        echo [✗] Pulse Simulator FAILED
        set /a FAILED_TESTS+=1
    )
) else (
    echo [✗] Compilation failed
    set /a FAILED_TESTS+=1
)

REM Print summary
echo.
echo ╔════════════════════════════════════════════════════════════╗
echo ║                    FINAL TEST SUMMARY                      ║
echo ╚════════════════════════════════════════════════════════════╝
echo.
echo Total test suites: %TOTAL_TESTS%
echo Passed: %PASSED_TESTS%
echo Failed: %FAILED_TESTS%
echo.

if %FAILED_TESTS% EQU 0 (
    echo ╔════════════════════════════════════════════════════════════╗
    echo ║              ✓ ALL TESTS PASSED ✓                         ║
    echo ╚════════════════════════════════════════════════════════════╝
    exit /b 0
) else (
    echo ╔════════════════════════════════════════════════════════════╗
    echo ║              ✗ SOME TESTS FAILED ✗                        ║
    echo ╚════════════════════════════════════════════════════════════╝
    exit /b 1
)