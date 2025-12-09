#!/bin/bash

echo "Building pulse simulator tests..."

# Check if g++ is available
if ! command -v g++ &> /dev/null; then
    echo "ERROR: g++ not found. Please install g++."
    exit 1
fi

# Compile test suite (include test dir first for Arduino.h mock)
g++ -std=c++11 -o pulse_simulator pulse_simulator.cpp -I. -I..
if [ $? -ne 0 ]; then
    echo "ERROR: Compilation failed"
    exit 1
fi

echo "Build successful!"
echo ""
echo "Running tests..."
echo ""

# Run tests
./pulse_simulator
TEST_RESULT=$?

echo ""
if [ $TEST_RESULT -eq 0 ]; then
    echo "All tests passed!"
else
    echo "Some tests failed. See output above."
fi

exit $TEST_RESULT
