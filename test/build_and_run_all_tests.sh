#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "╔════════════════════════════════════════════════════════════╗"
echo "║     Centauri Carbon Motion Detector Test Suite            ║"
echo "╚════════════════════════════════════════════════════════════╝"

# Check for g++
if ! command -v g++ &> /dev/null; then
    echo "ERROR: g++ not found"
    exit 1
fi

# Generate test settings if Python available
if command -v python3 &> /dev/null; then
    python3 generate_test_settings.py 2>/dev/null || true
elif command -v python &> /dev/null; then
    python generate_test_settings.py 2>/dev/null || true
fi

PASSED=0
FAILED=0

run_test() {
    local NAME=$1
    local FILE=$2
    echo ""
    echo "Building: $NAME"
    if g++ -std=c++11 -o "${NAME}_test" "$FILE" -I. -I.. 2>&1; then
        echo "Running: $NAME"
        if ./"${NAME}_test"; then
            echo "✓ $NAME PASSED"
            PASSED=$((PASSED + 1))
        else
            echo "✗ $NAME FAILED"
            FAILED=$((FAILED + 1))
        fi
    else
        echo "✗ Compilation failed for $NAME"
        FAILED=$((FAILED + 1))
    fi
}

run_test "FilamentMotionSensor" "test_filament_sensor.cpp"
run_test "JamDetector" "test_jam_detector.cpp"
run_test "SDCPProtocol" "test_sdcp_protocol.cpp"

echo ""
echo "Building: Pulse Simulator"
if g++ -std=c++11 -o pulse_simulator pulse_simulator.cpp -I. -I.. 2>&1; then
    echo "Running: Pulse Simulator"
    if ./pulse_simulator; then
        echo "✓ Pulse Simulator PASSED"
        PASSED=$((PASSED + 1))
    else
        echo "✗ Pulse Simulator FAILED"
        FAILED=$((FAILED + 1))
    fi
else
    echo "✗ Compilation failed"
    FAILED=$((FAILED + 1))
fi

echo ""
echo "═══════════════════════════════════════════════════════════"
echo "SUMMARY: Passed: $PASSED, Failed: $FAILED"
echo "═══════════════════════════════════════════════════════════"

if [ $FAILED -eq 0 ]; then
    echo "✓ ALL TESTS PASSED"
    exit 0
else
    echo "✗ SOME TESTS FAILED"
    exit 1
fi