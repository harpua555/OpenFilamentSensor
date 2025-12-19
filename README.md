# OpenFilamentSensor
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
## Quick Start Guide

### 1. Wiring Your Board

Connect your BigTreeTech Smart Filament Sensor (or generic motion/runout sensor) to your ESP32 board. 

**Common Connections:**
*   **VCC:** Connect to 3.3V or 5V (Check your sensor's voltage requirements; ESP32 inputs are 3.3V tolerant, but best to use 3.3V supply if sensor supports it).
*   **GND:** Connect to GND.

**Signal Pin Connections:**

| Board Type | Runout Signal Pin | Motion Signal Pin |
| :--- | :--- | :--- |
| **Generic ESP32 / ESP32-S3** | GPIO 12 | GPIO 13 |
| **Seeed XIAO ESP32-S3** | GPIO 5 | GPIO 6 |
| **Seeed XIAO ESP32-C3** | GPIO 3 | GPIO 2 |
| **ESP32-C3 SuperMini** | GPIO 3 | GPIO 2 |

*Note: Pins can be customized in `platformio.ini` or by rebuilding firmware if needed.*

### 2. First-Time Installation

The easiest way to install the firmware is using the Web Distributor.

1.  **Connect via USB:** Plug your ESP32 board into your computer.
2.  **Open Distributor:** Navigate to the distributor page at https://ofs.harpua555.dev
3.  **Select Board:** Choose your specific board model from the dropdown menu.
4.  **WiFi Setup (Optional):**
    *   Enter your WiFi SSID and Password in the "WiFi override" section.
    *   Click "Accept and provide credentials". This patches the firmware in your browser before flashing, so you don't have to connect to an AP later.
    *   If WiFi is not set at the time of patching, the ESP32 will braodcast a network named OFS.local.  Connect to this network, go to http://ofs.local in a web browser, and enter WiFi credentials in the setup tab (save settings!)
5.  **Flash:**
    *   Click **"Flash firmware"**.
    *   Select the USB serial port in the browser popup.
    *   Wait for the process to complete (Erase + Write).

### 3. OTA Updates (Over-the-Air)

Once your device is up and running, you can update it wirelessly.

1.  **Get Update Files:**
    *   Open the **Web Distributor**.
    *   Optionally complete WiFi setup again (see above)
    *   Instead of flashing, click **"Download OTA Files"**.
    *   This will download a `.zip` containing the latest `firmware.bin` and `littlefs.bin`. Extract this zip.
2.  **Access Device UI:**
    *   Connect to your ESP32 (IP or http://OFS.local).
3.  **Upload:**
    *   Navigate to the **Update** tab in the Web UI.
    *   Drag and drop the `firmware.bin` and 'littlefs.bin files. The device will upload the firmware, reboot, then upload the littlefs.
    *   The update process should only take about 30 seconds, and will display a message once both files are uploaded.
    *   If there are any issues with OTA updates, the device can always be reflashed via USB described above

## Requirements to Build Locally 

- PlatformIO Core (`pip install platformio` or via VS Code extension)
- Python 3.10+
- Node.js 18+ (for Lite UI build tooling)
- ESP32‑S3 DevKitC‑1 (default) or other microcontrollers (via alternate envs)

## Quick start

### Option A: Standard Build (recommended for experienced users)

Requires: Python 3.10+, Node.js 18+, PlatformIO Core

```bash
# Build Lite UI + firmware and flash via USB
python tools/build_and_flash.py

# Build without flashing for OTA updating only (no upload) – results land in and .pio/
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

## Contributing

- Run `python tools/build_and_flash.py --local` before opening a PR (verifies the build still succeeds).  
- Run the pulse simulator (`test/build_tests.sh`) whenever jam detection logic or fixture paths change.  
- Keep the Lite UI small—prefer CSS variables/utility functions over large JS dependencies.

## License

See [LICENSE.MD](LICENSE.MD).

## Credits

- OpenCentauri team for enabling the firmware patches required to access filament extrusion data
- jrowny for the initial idea and starting point
