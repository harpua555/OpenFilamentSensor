# Centauri Carbon Motion Detector - Unit Test Suite

## Overview

This test suite provides comprehensive unit testing for the Centauri Carbon Motion Detector firmware, focusing on the new and modified components introduced in the current development branch.

## Test Coverage

### 1. FilamentMotionSensor Tests (`test_filament_sensor.cpp`)

Tests the windowed tracking algorithm for filament motion detection.

**Test Cases (14 tests):**
- Initial state validation
- Reset behavior
- Expected position updates
- Sensor pulse tracking and accumulation
- Deficit calculation (expected vs actual)
- Flow ratio calculation (0%, 50%, 100%, 150%)
- Grace period timing
- Windowed tracking over time
- Multiple reset scenarios
- Windowed rate calculations
- Edge cases: zero expected with pulses, large time gaps
- Rapid and alternating updates

**Key Features Tested:**
- Initialization state management
- Windowed sample tracking
- Deficit and flow ratio calculations
- Time-based grace periods
- Sample pruning and window management

### 2. JamDetector Tests (`test_jam_detector.cpp`)

Tests the jam detection logic including grace periods, hard/soft jam detection, and state transitions.

**Test Cases (11 tests):**
- Initial state after construction
- Start grace period timing and transitions
- Hard jam detection and accumulation
- Soft jam detection with sustained under-extrusion
- Detection mode: HARD_ONLY (ignores soft jams)
- Detection mode: SOFT_ONLY (ignores hard jams)
- Resume grace period behavior
- Jam recovery with good flow
- Edge case: zero expected distance
- Jam latched state and pause request handling
- Pass ratio calculation with various thresholds

**Key Features Tested:**
- Grace state machine (IDLE, START_GRACE, RESUME_GRACE, ACTIVE, JAMMED)
- Hard jam detection (near-zero flow)
- Soft jam detection (sustained under-extrusion)
- Detection mode switching
- Accumulation timers and thresholds
- Recovery behavior
- Pause request flag management

### 3. SDCPProtocol Tests (`test_sdcp_protocol.cpp`)

Tests the SDCP protocol message building and parsing utilities.

**Test Cases (10 tests):**
- Build command message with all fields
- Build command message without mainboard ID
- Read extrusion value with normal key
- Read extrusion value with hex-encoded key
- Fallback to hex key when normal key missing
- Handle missing keys
- Handle null values
- Handle nullptr hex key parameter
- Build different command types (PAUSE, STOP, CONTINUE)
- Machine status mask encoding

**Key Features Tested:**
- JSON message structure building
- Command code handling
- Request ID and mainboard ID handling
- Topic field generation
- CurrentStatus array encoding
- Extrusion value parsing (normal and hex-encoded keys)
- Error handling for missing/null values

### 4. Pulse Simulator Tests (`pulse_simulator.cpp`)

Integration tests for the complete jam detection system (existing tests).

**Test Cases (15+ tests):**
- Normal healthy printing (no false positives)
- Hard jam detection timing
- Soft jam detection timing
- Sparse infill handling
- Retraction handling
- Ironing/low-flow patterns
- Transient spike resistance
- Minimum movement threshold
- Grace period duration
- Hard snag mid-print
- Complex flow sequences
- Real log replay from fixtures

## Building and Running Tests

### Linux/Mac

```bash
cd test

# Run all tests
./build_and_run_all_tests.sh

# Or run individual test suites
./build_tests.sh  # Original pulse simulator
g++ -std=c++11 -o test_jam_detector test_jam_detector.cpp -I. -I..
./test_jam_detector
```

### Windows

```bash
cd test

# Run all tests
build_and_run_all_tests.bat

# Or run individual test suites
build_tests.bat  # Original pulse simulator
g++ -std=c++11 -o test_jam_detector.exe test_jam_detector.cpp -I. -I..
test_jam_detector.exe
```

## Test Architecture

### Mock Environment

All tests use a minimal Arduino mock environment that provides:
- `millis()` function with controllable time
- Mock String class for SDCPProtocol tests
- Mock logger and settings manager for dependency injection

### Test Structure

Each test file follows this pattern:

1. **Mock declarations** - Minimal Arduino environment
2. **Include statements** - Include actual implementation files
3. **Test utilities** - Color codes, test result tracking
4. **Test functions** - Individual test cases with clear names
5. **Main runner** - Executes all tests and reports results

### Test Output

Tests produce colored console output:
- 🟢 Green `[PASS]` for passing tests
- 🔴 Red `[FAIL]` for failing tests
- Summary statistics at the end

## Adding New Tests

To add a new test to an existing suite:

```cpp
void testNewFeature() {
    printTestHeader("Test N: New Feature");
    
    // Setup
    MyClass instance;
    
    // Execute
    bool result = instance.doSomething();
    
    // Verify
    recordTest("Feature works correctly", result);
}

// Add to main():
testNewFeature();
```

To create a new test suite:

1. Create `test_mymodule.cpp` following the existing pattern
2. Add build/run steps to `build_and_run_all_tests.sh`
3. Document in this README

## Configuration

### Test Settings Generation

The `generate_test_settings.py` script reads `data/user_settings.json` and generates `generated_test_settings.h` with test configuration macros:

- `TEST_MM_PER_PULSE` - Sensor calibration
- `TEST_RATIO_THRESHOLD` - Soft jam threshold
- `TEST_HARD_JAM_MM` - Hard jam window
- `TEST_SOFT_JAM_TIME_MS` - Soft jam duration
- `TEST_HARD_JAM_TIME_MS` - Hard jam duration
- `TEST_GRACE_PERIOD_MS` - Grace period after moves

This ensures tests use the same settings as the actual firmware.

## CI/CD Integration

These tests can be integrated into CI/CD pipelines:

```yaml
# Example GitHub Actions step
- name: Run Unit Tests
  run: |
    cd test
    ./build_and_run_all_tests.sh
```

Exit codes:
- `0` - All tests passed
- `1` - One or more tests failed

## Test Philosophy

### Unit Tests Focus On:
- Pure logic without hardware dependencies
- Edge cases and boundary conditions
- State transitions and timing
- Error handling and recovery

### Integration Tests (Pulse Simulator) Focus On:
- Complete system behavior
- Real-world scenarios
- Timing-dependent interactions
- Realistic print simulations

## Dependencies

**Required:**
- g++ compiler with C++11 support
- Standard C++ library

**Optional:**
- Python 3 (for test settings generation)
- ArduinoJson library (for SDCPProtocol tests)

## Troubleshooting

### Compilation Errors

If you encounter compilation errors:

1. Ensure g++ is installed and supports C++11
2. Check that include paths are correct (`-I. -I..`)
3. Verify mock Arduino.h is present in test directory

### Test Failures

If tests fail unexpectedly:

1. Check if `generated_test_settings.h` is up to date
2. Verify the source files haven't changed interfaces
3. Run tests individually to isolate the issue
4. Check for platform-specific floating-point precision issues

### Missing Dependencies

For SDCPProtocol tests, ArduinoJson must be available. If not:
- Install via PlatformIO: `pio lib install "ArduinoJson"`
- Or comment out SDCPProtocol tests in the build script

## Future Enhancements

Potential additions to the test suite:

- [ ] Logger class tests
- [ ] SettingsManager tests (with mock filesystem)
- [ ] SystemServices tests (with mock WiFi/NTP)
- [ ] ElegooCC state machine tests
- [ ] WebServer endpoint tests
- [ ] Performance/timing benchmarks
- [ ] Memory leak detection
- [ ] Code coverage reporting

## Contributing

When adding new features:

1. Write tests first (TDD approach recommended)
2. Ensure all existing tests still pass
3. Add documentation for new test cases
4. Update this README if adding new test suites
5. Maintain the existing code style and patterns

## License

Same as the main project (see root LICENSE file).