#!/bin/bash

echo "========================================"
echo "  Building and Running All Test Suites"
echo "========================================"
echo ""

EXIT_CODE=0

# Function to run a test and track results
run_test() {
    local test_name="$1"
    local test_command="$2"
    
    echo ""
    echo "========================================"
    echo "  Running: $test_name"
    echo "========================================"
    
    eval "$test_command"
    local result=$?
    
    if [ $result -eq 0 ]; then
        echo "✓ $test_name PASSED"
    else
        echo "✗ $test_name FAILED"
        EXIT_CODE=1
    fi
    
    return $result
}

# Generate test settings if Python is available
echo "Checking for Python..."
PYTHON_BIN=""
if command -v python &> /dev/null; then
    PYTHON_BIN="python"
elif command -v python3 &> /dev/null; then
    PYTHON_BIN="python3"
fi

if [ -n "$PYTHON_BIN" ]; then
    echo "Generating test settings from data/user_settings.json..."
    $PYTHON_BIN generate_test_settings.py
    if [ $? -ne 0 ]; then
        echo "Warning: Failed to generate test settings header"
    fi
else
    echo "Warning: Python not found; using existing generated_test_settings.h"
fi

# Check for g++
if ! command -v g++ &> /dev/null; then
    echo "ERROR: g++ not found. Please install g++."
    exit 1
fi

echo ""
echo "========================================"
echo "  C++ Unit Tests"
echo "========================================"

# Build and run pulse simulator tests (existing)
echo "Building pulse_simulator tests..."
g++ -std=c++11 -o pulse_simulator pulse_simulator.cpp -I. -I..
if [ $? -eq 0 ]; then
    run_test "Pulse Simulator" "./pulse_simulator"
else
    echo "✗ Failed to compile pulse_simulator"
    EXIT_CODE=1
fi

# Build and run JamDetector tests
echo ""
echo "Building JamDetector tests..."
g++ -std=c++11 -o test_jam_detector test_jam_detector.cpp -I. -I..
if [ $? -eq 0 ]; then
    run_test "JamDetector Unit Tests" "./test_jam_detector"
else
    echo "✗ Failed to compile test_jam_detector"
    EXIT_CODE=1
fi

# Build and run SDCPProtocol tests
echo ""
echo "Building SDCPProtocol tests..."
g++ -std=c++11 -o test_sdcp_protocol test_sdcp_protocol.cpp -I. -I..
if [ $? -eq 0 ]; then
    run_test "SDCPProtocol Unit Tests" "./test_sdcp_protocol"
else
    echo "✗ Failed to compile test_sdcp_protocol"
    EXIT_CODE=1
fi

# Run Python tests if available
if [ -n "$PYTHON_BIN" ]; then
    echo ""
    echo "========================================"
    echo "  Python Tool Tests"
    echo "========================================"
    
    if [ -f "test_tools.py" ]; then
        run_test "Python Tools" "$PYTHON_BIN test_tools.py"
    else
        echo "Note: test_tools.py not found, skipping"
    fi
fi

# Run JavaScript/Node tests if available
if command -v node &> /dev/null; then
    echo ""
    echo "========================================"
    echo "  JavaScript/Node Tests"
    echo "========================================"
    
    if [ -f "test_distributor.js" ]; then
        run_test "Distributor & WebUI" "node test_distributor.js"
    else
        echo "Note: test_distributor.js not found, skipping"
    fi
fi

# Summary
echo ""
echo "========================================"
echo "  Test Suite Summary"
echo "========================================"

if [ $EXIT_CODE -eq 0 ]; then
    echo "✓ ALL TESTS PASSED"
else
    echo "✗ SOME TESTS FAILED"
fi

echo ""
exit $EXIT_CODE