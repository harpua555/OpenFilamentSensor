# Testing Overview - Centauri Carbon Motion Detector

## Quick Start

```bash
cd test
./build_and_run_all_tests.sh
```

This will run all test suites (C++, Python, JavaScript) and report results.

## What Was Tested

### ✅ Comprehensive Unit Tests Created For:

#### New C++ Components (Branch Changes)
- **JamDetector** (`src/JamDetector.cpp`)
  - 12 test cases covering grace periods, hard/soft jam detection, recovery, edge cases
  - Tests rate-based detection algorithm
  - Validates all detection modes (BOTH, HARD_ONLY, SOFT_ONLY)
  
- **SDCPProtocol** (`src/SDCPProtocol.cpp`)
  - 5 test cases for protocol utilities
  - Tests message building and parsing
  - Validates hex key fallback mechanisms

#### Python Tools (New)
- **board_config.py** - Board configuration validation
- **extract_log_data.py** - Log extraction functionality
- **Settings validation** - JSON structure and type checking
- **Fixture validation** - Test data integrity

#### JavaScript/Web Components (New)
- **distributor/app.js** - Server application structure
- **distributor/wifiPatcher.js** - WiFi configuration patching
- **webui_lite/lite_ota.js** - OTA update logic
- **webui_lite/dev-server.js** - Development server
- **JSON configuration files** - Validation of boards.json, manifest.json

### ✅ Already Tested (Existing)
- **FilamentMotionSensor** - Comprehensive tests via `pulse_simulator.cpp`
  - Normal printing scenarios
  - Jam detection (hard and soft)
  - Sparse infill handling
  - Grace periods
  - Real log replay

### ⚠️ Requires Integration Testing
Some components have hardware or network dependencies that require integration testing:

- **SystemServices** - Requires WiFi and NTP access
- **ElegooCC** - Requires actual printer connection via SDCP
- **Logger** - File system operations
- **SettingsManager** - File system operations  
- **WebServer** - Network operations

These components are mocked in unit tests but should be validated in integration tests with actual hardware.

## Test Structure