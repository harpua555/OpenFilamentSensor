# AI Agent Rules & Guidelines for Centauri Carbon Motion Detector

## 0. Core Rules for AI Agents
1.  **Build Process:** Never compile using any method other than `python tools/build_and_flash.py --local` unless specifically needed and approved.
2.  **Testing:** Always run `test/build_and_run_all_tests.sh` to confirm implemented changes still pass tests.
3.  **Git Operations:** Never `git push`, `git rebase`, or `git reset`.

## 1. Project Overview
This is an embedded C++ project for ESP32 (PlatformIO) acting as a smart filament motion sensor. It includes a lightweight Web UI and Python tooling.

## 2. Technology Stack & Constraints
- **Firmware:** C++17, Arduino Framework, PlatformIO.
  - Target: ESP32-S3 (primary), ESP32 (legacy).
  - Code Style: Modular design (`.cpp`/`.h` in `src/`).
- **Web UI (`webui_lite/`):**
  - **Strictly Vanilla HTML/CSS/JS**. No frameworks (React, Vue, etc.).
  - Assets are gzipped and embedded into the firmware via `webui_lite/build.js`.
- **Tooling:** Python 3.10+ (`tools/`), Shell scripts (`test/`).

## 3. Development Workflow

### A. Building Firmware
- **Primary Command:** `python tools/build_and_flash.py`
- **Build Only (No Flash):** `python tools/build_and_flash.py --local`
- **Do NOT** rely solely on `pio run` if the Web UI needs updating, as the python script handles asset packing.

### B. Testing (CRITICAL)
- **Host-Based Tests:** The project relies heavily on host-side testing (running on the development machine, not the ESP32).
- **Run All Tests:** `cd test/ && ./build_and_run_all_tests.sh`
- **Smoke Test:** `cd test/ && ./build_and_run_all_tests.sh --quick`
- **Rule:** ALWAYS run tests before finalizing changes to `src/` logic (JamDetector, SDCPProtocol, etc.).

### C. Web UI Changes
1. Modify files in `webui_lite/`.
2. Run the build script to pack assets: `node webui_lite/build.js`.
3. Rebuild firmware to embed the new assets.

## 4. Coding Conventions
- **Secrets:** Never commit credentials.
- **Logging:** Use the `Logger` class, not `Serial.print` directly, to ensure logs are handled correctly by the system.
- **File Structure:**
  - `src/`: Firmware source code.
  - `include/`: Global headers (though mostly local headers are used in `src/`).
  - `test/`: Host-based C++ tests and Python test scripts.
  - `tools/`: Build and utility scripts.

## 5. Environment
- The project uses `platformio.ini` for dependency management.
- Python dependencies are in `tools/requirements.txt`.