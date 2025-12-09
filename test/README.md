# Pulse Simulator - Unit Tests

Comprehensive unit testing for the FilamentMotionSensor without requiring hardware.

## Detection Goals

- **Hard jams** trigger when the sensor sees less than 10% of the expected filament for five continuous seconds while extrusion commands are still advancing.
- **Soft jams** trigger when the deficit keeps growing while actual flow stays below 25% of the expected filament for ten seconds, letting the detector ignore look-ahead bursts and very low flow moves.
- **Grace periods** reset after print start, retractions, or telemetry gaps so the system only evaluates jams once the printer has committed to extrusion and a few millimetres have been requested.

## Features

- **Test 1: Normal Healthy Print** - Verifies no false positives during 30 seconds of continuous printing
- **Test 2: Hard Jam Detection** - Complete blockage (no sensor pulses) detected in ~2 seconds
- **Test 3: Soft Jam Detection** - Partial clog (20% flow rate) detected in ~3 seconds
- **Test 4: Sparse Infill** - Travel moves with minimal extrusion don't cause false positives
- **Test 5: Retraction Handling** - Grace period applies correctly after retractions
- **Test 6: Ironing / Low-Flow Handling** - Micro movements and ironing patterns do not trigger jams
- **Test 7: Transient Spike Resistance** - Single bad ratio spike doesn't trigger a jam (hysteresis)
- **Test 8: Minimum Movement Threshold** - Tiny movements below 1mm threshold don't cause issues
- **Test 9: Grace Period Duration** - 500ms grace period protects against SDCP look-ahead
- **Test 10: Normal Print with Hard Snag** - Jam detection still trips after a long healthy period followed by a blockage
- **Test 11: Complex Flow Sequence** - Long print with travel, retractions, and ironing remains jam-free

## Building and Running

### Windows

```batch
cd test
build_tests.bat
```

### Linux/Mac

```bash
cd test
chmod +x build_tests.sh
./build_tests.sh
```

### Visualizing Flow (optional)

After the simulator generates a CSV log you can open `test/render/index.html` to see how expected vs actual filament behaves:

1. Run the simulator with logging enabled:

   ```bash
   ./pulse_simulator --log render/filament_log.csv
   ```

2. Serve the render folder (e.g., `python3 -m http.server`) and open `http://localhost:8000`.
3. Load `render/filament_log.csv`, then use the player controls (Play/Pause/Step) to animate the flow.

The CSV now contains a row for every simulated check (one per second), so the renderer can show every test as flowing filament and the dropdown lets you isolate a single scenario.

See `render/README.md` for more details on the renderer and CSV format.

### Manual Compilation

```bash
g++ -std=c++11 -o pulse_simulator pulse_simulator.cpp -I..
./pulse_simulator
```

## Configuration

Test parameters are defined at the top of `pulse_simulator.cpp`:

```cpp
const float MM_PER_PULSE = 2.88f;           // Sensor calibration
const int CHECK_INTERVAL_MS = 1000;          // Jam check frequency
const float RATIO_THRESHOLD = 0.25f;         // 25% passing threshold (soft jam)
const float HARD_JAM_MM = 5.0f;              // Hard jam threshold
const int SOFT_JAM_TIME_MS = 10000;           // Soft jam duration
const int HARD_JAM_TIME_MS = 5000;            // Hard jam duration
const int GRACE_PERIOD_MS = 500;             // Grace period duration
```

## Understanding Test Output

Each test shows detailed sensor state:

```
[Test Name] exp=24.60mm act=24.60mm deficit=0.00mm ratio=1.00 [OK]
```

- **exp**: Expected extrusion (from SDCP telemetry)
- **act**: Actual sensor measurement
- **deficit**: How much expected exceeds actual
- **ratio**: Flow ratio (0.0 = 0% passing, 1.0 = 100% passing)
- **[JAM]** or **[OK]**: Current jam detection state

## Adding New Tests

To add a new test scenario:

1. Create a new test function following the pattern:
```cpp
void testMyScenario() {
    printTestHeader("Test X: My Scenario");

    FilamentMotionSensor sensor;
    sensor.setTrackingMode(TRACKING_MODE_WINDOWED, 5000, 0.3f);
    sensor.reset();
    _mockMillis = 0;

    // Your test logic...

    recordTest("Test description", passed);
}
```

2. Call it from `main()`:
```cpp
testMyScenario();
```

## Test Scenarios Simulated

### Normal Printing
- Continuous extrusion at 50mm/s
- 100% sensor flow rate
- Expected: No false positives

### Hard Jam
- Extrusion commands continue
- Sensor pulses stop completely
- Expected: Jam detected in 2 seconds

### Soft Jam
- Extrusion commands continue
- Sensor shows only 20% flow rate
- Expected: Jam detected in 3 seconds

### Sparse Infill
- 3 seconds normal printing
- 10 seconds travel (no telemetry updates)
- 3 seconds resume
- Expected: No false positives, grace period applies on resume

### Retractions
- Normal printing → retraction → resume
- Expected: Grace period applies after retraction

### Transient Spikes
- Single 1-second bad ratio spike
- Returns to normal
- Expected: Hysteresis prevents false positive

## Exit Codes

- `0`: All tests passed
- `1`: One or more tests failed
