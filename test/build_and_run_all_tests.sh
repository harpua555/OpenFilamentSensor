#!/bin/bash

# =============================================================================
#  Centauri Carbon Motion Detector - Test Suite Runner
# =============================================================================
#
# Usage:
#   ./build_and_run_all_tests.sh [options]
#
# Options:
#   --quick        Run only pulse_simulator (fast smoke test)
#   --filter NAME  Run only tests matching NAME
#   --verbose      Show full compilation output
#   --no-python    Skip Python tests
#   --no-node      Skip Node/JavaScript tests
#   --help         Show this help message
#
# =============================================================================

# set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Parse arguments
QUICK_MODE=false
FILTER=""
VERBOSE=false
SKIP_PYTHON=false
SKIP_NODE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --quick)
            QUICK_MODE=true
            shift
            ;;
        --filter)
            FILTER="$2"
            shift 2
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        --no-python)
            SKIP_PYTHON=true
            shift
            ;;
        --no-node)
            SKIP_NODE=true
            shift
            ;;
        --help)
            head -25 "$0" | tail -20
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

echo ""
echo "========================================"
echo "  Building and Running All Test Suites"
echo "========================================"
echo ""

if [ "$QUICK_MODE" = true ]; then
    echo -e "${YELLOW}Quick mode: Running pulse_simulator only${NC}"
    echo ""
fi

if [ -n "$FILTER" ]; then
    echo -e "${YELLOW}Filter: Running tests matching '$FILTER'${NC}"
    echo ""
fi

EXIT_CODE=0
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Function to run a test and track results
run_test() {
    local test_name="$1"
    local test_command="$2"

    # Check filter
    if [ -n "$FILTER" ]; then
        if [[ ! "$test_name" =~ $FILTER ]]; then
            return 0
        fi
    fi

    echo ""
    echo "========================================"
    echo -e "  ${CYAN}Running: $test_name${NC}"
    echo "========================================"

    TESTS_RUN=$((TESTS_RUN + 1))

    if eval "$test_command"; then
        echo -e "${GREEN}✓ $test_name PASSED${NC}"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        echo -e "${RED}✗ $test_name FAILED${NC}"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        EXIT_CODE=1
        return 1
    fi
}

# Function to compile a test
compile_test() {
    local output_name="$1"
    shift
    local source_files=("$@")

    # Attempt to find PlatformIO's core directories.
    # Default to standard user location if not overridden.
    : "${PLATFORMIO_HOME:="$HOME/.platformio"}"

    local PIO_CORE_PATH="${PLATFORMIO_HOME}/packages/framework-arduinoespressif32"
    local ARDUINO_CORE_INC_PATH="${PIO_CORE_PATH}/cores/esp32"
    local ARDUINO_VARIANT_INC_PATH="${PIO_CORE_PATH}/variants/esp32"

    local PIO_LIBS_PATH="../.pio/libdeps"
    local WEBSOCKETS_INC_PATH
    WEBSOCKETS_INC_PATH=$(find "${PIO_LIBS_PATH}" -name "WebSocketsClient.h" -print -quit | xargs dirname 2>/dev/null || echo "${PIO_LIBS_PATH}/esp32s3/WebSockets/src")
    
    # Include paths: prefer local stubs before optionally installed PlatformIO headers
    local INCLUDE_PATHS="-I. -I./mocks -I${WEBSOCKETS_INC_PATH} -I${ARDUINO_CORE_INC_PATH} -I${ARDUINO_VARIANT_INC_PATH} -I../src -I.."

    # Add extra source files based on the test name
    # Note: Many tests include .cpp files directly via #include, so only add
    # extra sources when the test genuinely needs separate compilation
    local extra_sources=()
    # test_jam_detector and test_additional_edge_cases include sources directly
    # and use mocks, so they don't need extra sources compiled
    if [ "$output_name" = "test_jam_detector" ] || [ "$output_name" = "test_additional_edge_cases" ]; then
        : # No extra sources - tests include what they need directly
    elif [ -f "../src/${output_name#test_}.cpp" ]; then
         extra_sources+=("../src/${output_name#test_}.cpp")
    fi
    
    # Combine main source file with extra sources
    local all_sources=("${source_files[@]}" "${extra_sources[@]}")

    if [ "$VERBOSE" = true ]; then
        g++ -std=c++17 -Wno-redefined-macros -o "$output_name" "${all_sources[@]}" $INCLUDE_PATHS
    else
        g++ -std=c++17 -Wno-redefined-macros -o "$output_name" "${all_sources[@]}" $INCLUDE_PATHS 2>&1 | head -30
    fi
    
    # Use PIPESTATUS to get the exit code of g++, not head
    return ${PIPESTATUS[0]}
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
    $PYTHON_BIN generate_test_settings.py || echo -e "${YELLOW}Warning: Failed to generate test settings header${NC}"
else
    echo -e "${YELLOW}Warning: Python not found; using existing generated_test_settings.h${NC}"
fi

# Check for g++
if ! command -v g++ &> /dev/null; then
    echo -e "${RED}ERROR: g++ not found. Please install g++.${NC}"
    exit 1
fi

echo ""
echo "========================================"
echo "  C++ Unit Tests"
echo "========================================"

# Define all C++ tests
declare -a CPP_TESTS=(
    "pulse_simulator:Pulse Simulator"
    "test_jam_detector:JamDetector Unit Tests"
    "test_sdcp_protocol:SDCPProtocol Unit Tests"
    "test_additional_edge_cases:Additional Edge Cases"
    "test_filament_motion_sensor:FilamentMotionSensor Unit Tests"
    "test_elegoo_cc:ElegooCC Unit Tests"
    "test_settings_manager:SettingsManager Unit Tests"
    "test_logger:Logger Unit Tests"
    "test_integration:Integration Tests"
)

# In quick mode, only run pulse_simulator
if [ "$QUICK_MODE" = true ]; then
    CPP_TESTS=("pulse_simulator:Pulse Simulator")
fi

for test_entry in "${CPP_TESTS[@]}"; do
    IFS=':' read -r test_file test_name <<< "$test_entry"

    # Check filter
    if [ -n "$FILTER" ]; then
        if [[ ! "$test_name" =~ $FILTER ]] && [[ ! "$test_file" =~ $FILTER ]]; then
            continue
        fi
    fi

    source_file="${test_file}.cpp"

    if [ ! -f "$source_file" ]; then
        echo -e "${YELLOW}Note: $source_file not found, skipping${NC}"
        continue
    fi

    echo ""
    echo "Building $test_name..."

    if compile_test "$test_file" "$source_file"; then
        run_test "$test_name" "./$test_file"
    else
        echo -e "${RED}✗ Failed to compile $test_file${NC}"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        TESTS_RUN=$((TESTS_RUN + 1))
        EXIT_CODE=1
    fi
done

# Run Python tests if available and not skipped
if [ "$SKIP_PYTHON" = false ] && [ -n "$PYTHON_BIN" ] && [ "$QUICK_MODE" = false ]; then
    echo ""
    echo "========================================"
    echo "  Python Tool Tests"
    echo "========================================"

    if [ -f "test_tools.py" ]; then
        run_test "Python Tools" "$PYTHON_BIN test_tools.py"
    else
        echo -e "${YELLOW}Note: test_tools.py not found, skipping${NC}"
    fi
fi

# Run JavaScript/Node tests if available and not skipped
if [ "$SKIP_NODE" = false ] && command -v node &> /dev/null && [ "$QUICK_MODE" = false ]; then
    echo ""
    echo "========================================"
    echo "  JavaScript/Node Tests"
    echo "========================================"

    if [ -f "test_distributor.js" ]; then
        run_test "Distributor & WebUI" "node test_distributor.js"
    else
        echo -e "${YELLOW}Note: test_distributor.js not found, skipping${NC}"
    fi
fi

# Summary
echo ""
echo "========================================"
echo "  Test Suite Summary"
echo "========================================"
echo ""
echo -e "  Tests Run:    ${CYAN}$TESTS_RUN${NC}"
echo -e "  ${GREEN}Passed:       $TESTS_PASSED${NC}"
if [ $TESTS_FAILED -gt 0 ]; then
    echo -e "  ${RED}Failed:       $TESTS_FAILED${NC}"
fi
echo ""

if [ $EXIT_CODE -eq 0 ]; then
    echo -e "${GREEN}✓ ALL TESTS PASSED${NC}"
else
    echo -e "${RED}✗ SOME TESTS FAILED${NC}"
fi

echo ""
exit $EXIT_CODE
