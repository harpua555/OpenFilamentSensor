# OpenFilamentSensor [SUNSET]

> [!WARNING]
> This project is no longer actively maintained or hosted. The original web distributor has been decommissioned. The source code remains open for community use and manual building.

This project uses a BigTreeTech filament motion sensor and an ESP32, in addition to patched OpenCentauri firmware, to provide Klipper-like detection. The firmware uses SDCP telemetry from the printer to determine expected filament flow conditions, compares it to the physical pulse sensor, and pauses the printer when hard jams or severe under‑extrusion (soft jams elsewhere in documentation) are detected. An HTML dashboard served by the ESP32 provides status, configuration, and OTA updates.

## Highlights

- **Dual-mode jam detection** – windowed tracking plus hysteresis for hard/soft snags  
- **Telemetry gap handling** – avoids false positives during communication drops  
- **Lite Web UI** – pure HTML/CSS/JS (~50 KB gzipped) served directly from LittleFS  
- **ElegantOTA built in** – visit `/update` for firmware uploads without serial cables  
- **Comprehensive simulator** – 20 scenario tests plus log replay to guard against regressions

## Repository layout

```
data/                # LittleFS content (lite UI + settings templates)
data_lite/           # Build output from webui_lite/ (generated)
src/                 # ESP32 firmware (Arduino framework)
test/                # Pulse simulator + fixtures
webui_lite/          # Single-page Lite UI source
```

## Installation Guide

### 1. Wiring Your Board

Connect your BigTreeTech Smart Filament Sensor (or generic motion/runout sensor) to your ESP32 board.

**Common Connections:**

- **VCC:** Connect to 3.3V or 5V (Check your sensor's voltage requirements; ESP32 inputs are 3.3V tolerant, but best to use 3.3V supply if sensor supports it).
- **GND:** Connect to GND.

**Signal Pin Connections:**

| Board Type | Runout Signal Pin | Motion Signal Pin |
| :--- | :--- | :--- |
| **Generic ESP32** | GPIO 14 | GPIO 27 |
| **Generic ESP32-S3** | GPIO 12 | GPIO 13 |
| **Seeed XIAO ESP32-S3** | GPIO 5 | GPIO 6 |
| **Seeed XIAO ESP32-C3** | GPIO 3 | GPIO 2 |
| **ESP32-C3 SuperMini** | GPIO 3 | GPIO 2 |
| **Seeed XIAO ESP32-C6** | GPIO 21 | GPIO 2 |
| **ESP32-C6 DevKitC-1** | GPIO 2 | GPIO 3 |
| **ESP32-C6 DevKitM-1** | GPIO 2 | GPIO 3 |

*Note: Pins can be customized in `platformio.ini` or by rebuilding firmware if needed.*

### ~~2. First-Time Installation (DECOMMISSIONED)~~

~~The easiest way to install the firmware was using the Web Distributor. This service is no longer hosted.~~

### 2. Manual Installation

To install the firmware for the first time, you must build it from source. Follow the [Manual Build Instructions](#manual-build-instructions) section below to build and flash via USB.

### 3. OTA Updates (Over-the-Air)

Once your device is up and running, you can update it wirelessly.

1. **Get Update Files:**
    - Build them manually (see [Manual Build Instructions](#manual-build-instructions) below).
    - The files will be generated in `.pio/build/<env>/firmware.bin` and `.pio/build/<env>/littlefs.bin`.
2. **Access Device UI:**
    - Connect to your ESP32 (IP or <http://OFS.local>).
3. **Upload:**
    - Navigate to the **Update** tab in the Web UI.
    - Drag and drop the `firmware.bin` and `littlefs.bin` files. The device will upload the firmware, reboot, then upload the littlefs.
    - The update process should only take about 30 seconds, and will display a message once both files are uploaded.
    - If there are any issues with OTA updates, the device can always be reflashed via USB using the manual build instructions below.

## Manual Build Instructions

To build and flash the firmware manually, follow these steps.

### Prerequisites

- PlatformIO Core (`pip install platformio` or via VS Code extension)
- Python 3.10+
- Node.js 18+ (for Lite UI build tooling)
- ESP32‑S3 DevKitC‑1 (default) or other microcontrollers (via alternate envs)

### Option A: Standard Build (recommended for experienced users)

Requires: Python 3.10+, Node.js 18+, PlatformIO Core

```bash
# Build Lite UI + firmware and flash via USB
python tools/build_and_flash.py

# Build without flashing for OTA updating only (no upload) – results land in `data/` and `.pio/`
python tools/build_and_flash.py --local

# Target another board (see platformio.ini for env names)
python tools/build_and_flash.py --env <board>

# Reuse existing node_modules when rebuilding the UI
python tools/build_and_flash.py --skip-npm-install
```

### Option B: Portable Environment (no global dependencies)

Requires: Only Python 3.10+ and Node.js 18+

This creates an isolated build environment in `tools/.venv/` and `tools/.platformio/` without modifying your global Python or PlatformIO installations.

```bash
# One-time setup (installs PlatformIO and ESP32 toolchain locally)
python tools/setup_local_env.py
```

### Option C: Pure PlatformIO (Advanced)

If you prefer not to use the automated Python scripts, you can build and flash using the PlatformIO CLI directly:

1. **Build the Web UI:**

    ```bash
    cd webui_lite
    npm install
    node build.js
    ```

2. **Prepare Filesystem:**

    ```bash
    # Copy build artifacts to data folder
    mkdir -p data/lite
    cp -rv data_lite/* data/lite/
    ```

3. **Build and Flash:**

    ```bash
    # Replace <board> with your environment name (e.g., esp32s3)
    pio run -e <board> -t upload -t uploadfs
    ```

    *Note: Using this method bypasses the automatic version stamping and secret merging features of the Python wrapper script.*

### Stress mode (optional)

To accelerate loops and increase internal load for crash reproduction, build with:

```ini
# platformio.ini
# Add to an environment's build_flags (preserving existing flags):
build_flags =
    ${common.build_flags}
    -DSTRESS_MODE
```

## Contributing

- Run `python tools/build_and_flash.py --local` before opening a PR (verifies the build still succeeds).  
- Run the pulse simulator (`test/build_tests.sh`) whenever jam detection logic or fixture paths change.  
- Keep the Lite UI small—prefer CSS variables/utility functions over large JS dependencies.

## License

See [LICENSE.MD](LICENSE.MD).

## Credits

- OpenCentauri team for enabling the firmware patches required to access filament extrusion data
- jrowny for the initial idea and starting point
