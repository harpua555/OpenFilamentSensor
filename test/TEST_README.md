# Test Suite Documentation

This directory contains comprehensive unit tests for the Centauri Carbon Motion Detector project.

## Test Structure

### C++ Unit Tests

#### 1. `pulse_simulator.cpp` (Existing)
Tests the FilamentMotionSensor class with simulated print scenarios:
- Normal printing
- Hard jams (complete blockage)
- Soft jams (partial clogs/under-extrusion)
- Sparse infill
- Retractions
- Speed changes
- Grace periods
- Real log replay

#### 2. `test_jam_detector.cpp` (New)
Tests the JamDetector class in isolation:
- Reset and initialization
- Grace period handling (startup, resume)
- Hard jam detection
- Soft jam detection
- Jam recovery
- Detection modes (BOTH, HARD_ONLY, SOFT_ONLY)
- Rate-based detection
- Minimum thresholds
- Pause request handling
- Edge cases (zero movement, not printing)

#### 3. `test_sdcp_protocol.cpp` (New)
Tests the SDCP protocol helper utilities:
- Command message building
- Extrusion value reading (normal and hex keys)
- Fallback key handling
- Missing key handling
- Constant validation

### Python Tests

#### `test_tools.py` (New)
Tests Python tooling:
- Board configuration utilities
- Log extraction functionality
- Settings generation
- Build script integrity
- Test fixtures validation
- Data file integrity

### JavaScript/Node Tests

#### `test_distributor.js` (New)
Tests distributor and WebUI components:
- File existence checks
- JSON validation (boards.json, manifest.json)
- WiFi patcher structure
- App.js server logic
- HTML validity
- WebUI lite files
- OTA update logic
- Dev server functionality

## Running Tests

### Quick Start - Run All Tests
```bash
cd test
./build_and_run_all_tests.sh
```

### Individual Test Suites

#### C++ Tests Only
```bash
cd test
./build_tests.sh              # Original script (pulse_simulator only)
```

#### Build and Run Specific C++ Test
```bash
cd test
g++ -std=c++11 -o test_jam_detector test_jam_detector.cpp -I. -I..
./test_jam_detector

g++ -std=c++11 -o test_sdcp_protocol test_sdcp_protocol.cpp -I. -I..
./test_sdcp_protocol
```

#### Python Tests
```bash
cd test
python3 test_tools.py
```

#### JavaScript Tests
```bash
cd test
node test_distributor.js
```

## Test Requirements

### C++ Tests
- **Compiler**: g++ with C++11 support
- **Platform**: Linux, macOS, Windows (with MinGW/MSYS2)

### Python Tests
- **Python**: 3.6 or later
- **Dependencies**: None (uses standard library)

### JavaScript Tests
- **Node.js**: 12.0 or later
- **Dependencies**: None (uses built-in modules)

## Test Coverage

### New C++ Files (Branch Changes)
- ✅ `src/JamDetector.cpp` - Comprehensive unit tests
- ✅ `src/JamDetector.h` - Interface tested through JamDetector tests
- ✅ `src/SDCPProtocol.cpp` - Protocol utilities tested
- ✅ `src/SDCPProtocol.h` - Protocol interface tested
- ⚠️ `src/SystemServices.cpp` - Integration-level testing (WiFi, NTP)
- ⚠️ `src/SystemServices.h` - Integration-level testing

### Modified C++ Files
- ✅ `src/FilamentMotionSensor.cpp` - Existing pulse_simulator tests
- ✅ `src/FilamentMotionSensor.h` - Tested through pulse_simulator
- ⚠️ `src/Logger.cpp` - Mocked in tests, integration-level testing needed
- ⚠️ `src/Logger.h` - Mocked in tests
- ⚠️ `src/SettingsManager.cpp` - Mocked in tests, integration-level testing needed
- ⚠️ `src/SettingsManager.h` - Mocked in tests
- ⚠️ `src/ElegooCC.cpp` - Integration-level testing (hardware dependencies)
- ⚠️ `src/ElegooCC.h` - Integration-level testing
- ⚠️ `src/WebServer.cpp` - Integration-level testing (network dependencies)

### New Python Files
- ✅ `tools/board_config.py` - Module import tested
- ✅ `tools/extract_log_data.py` - Module import tested
- ✅ `tools/build_and_release.py` - Not directly tested (build script)
- ✅ `test/generate_test_settings.py` - Indirectly tested through settings validation

### New JavaScript Files
- ✅ `distributor/app.js` - Structure and patterns tested
- ✅ `distributor/wifiPatcher.js` - Structure and logic patterns tested
- ✅ `webui_lite/lite_ota.js` - Structure and patterns tested
- ✅ `webui_lite/dev-server.js` - Structure and patterns tested
- ✅ `webui_lite/build.js` - Not tested (build utility)

### Configuration and Data Files
- ✅ `distributor/boards.json` - JSON validation
- ✅ `distributor/firmware/*/manifest.json` - JSON validation
- ✅ `data/user_settings.json` - JSON validation and structure tests
- ✅ Test fixtures (`test/fixtures/logs_to_replay/*.txt`) - Existence validation

## Test Philosophy

### Unit Tests
Focus on testing individual components in isolation with mocked dependencies.
Target: Pure logic, algorithms, data transformations.

### Integration Tests
Test component interactions and external dependencies (WiFi, file system, hardware).
Note: Some modified files require integration testing with actual hardware.

### Validation Tests
For configuration files and static content, verify:
- Valid syntax (JSON, HTML, etc.)
- Required fields present
- Type correctness
- Structural integrity

## Adding New Tests

### For C++ Components
1. Create `test/test_<component>.cpp`
2. Include necessary mocks (Arduino.h, Logger, etc.)
3. Follow existing test patterns (assert, color output)
4. Add to `build_and_run_all_tests.sh`

### For Python Tools
1. Add test class to `test/test_tools.py`
2. Use unittest framework
3. Add appropriate skip decorators for unavailable components

### For JavaScript Components
1. Add test functions to `test/test_distributor.js`
2. Use Node.js built-in assert module
3. Handle missing files gracefully (skip, not fail)

## Continuous Integration

The test suite is designed to run in CI/CD pipelines:
- Exit code 0: All tests passed
- Exit code 1: One or more tests failed
- Colored output for readability
- Clear pass/fail indicators

## Known Limitations

### SystemServices
- Requires WiFi hardware and network access
- NTP synchronization needs internet connectivity
- Best tested through integration tests on target hardware

### ElegooCC
- Depends on SDCP protocol and network communication
- Requires actual printer connection
- Best tested through integration tests

### Logger & SettingsManager
- File system operations
- Best complemented with integration tests

### WebServer
- Network dependencies
- Best tested through integration tests or HTTP mocking frameworks

## Future Improvements

1. **Mock Frameworks**: Consider adding Catch2 or Google Test for C++
2. **Code Coverage**: Add coverage reporting (gcov/lcov)
3. **Integration Tests**: Develop hardware-in-the-loop test suite
4. **Automated Fixtures**: Generate test fixtures from real printer logs
5. **Performance Tests**: Add benchmarks for critical path code
6. **Fuzz Testing**: Add fuzzing for protocol parsing and file handling

## Contributing

When contributing tests:
1. Follow existing patterns and conventions
2. Use descriptive test names
3. Add comments explaining non-obvious test scenarios
4. Update this README with new test coverage
5. Ensure tests are deterministic and reproducible

## Support

For questions about tests:
1. Check this README first
2. Review existing test code for patterns
3. Consult the main project README
4. Open an issue on GitHub

---

Last Updated: 2024-12-05