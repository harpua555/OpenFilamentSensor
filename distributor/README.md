# Distributor

This static site flashes Centauri firmware via WebSerial and packages OTA downloads. It now pulls binaries directly from GitHub Releases instead of cloning the `build` branch.

## How it works
- On load, it fetches the latest GitHub Releases list for `harpua555/centauri-carbon-motion-detector` and populates a release selector (or a specific tag via `?tag=vX.Y.Z`).
- The dropdown marks the current/latest release; picking another release rebuilds the board list using only the boards with assets on GitHub Pages for that release (no local fallbacks).
- It maps release assets named `<env>-firmware_merged.bin`, `<env>-firmware.bin`, `<env>-littlefs.bin`, `<env>-bootloader.bin`, `<env>-partitions.bin` served from GitHub Pages to the boards defined in `boards.json`.
- A per-board manifest is generated in the browser pointing to the release URLs (used by WebSerial flashing).
- OTA downloads are zipped from the same release assets; you can optionally Wi‑Fi‑patch `littlefs.bin` before packaging.
- If release metadata is unavailable (rate limit/offline), it falls back to the local `distributor/firmware` tree.

## Running locally
```
cd distributor
python -m http.server 8000
```
Then open `http://localhost:8000`. Use `?tag=v0.6.1-alpha` (or omit to use the latest release).

## Expected release assets
For each PlatformIO env (e.g., `esp32s3`, `esp32c3`, `esp32`, `seeed_esp32c3`, `seeed_esp32s3`, `esp32c3supermini`), the Release must include:
- `<env>-firmware_merged.bin` (used for WebSerial flashing)
- `<env>-firmware.bin` (OTA app)
- `<env>-littlefs.bin` (OTA filesystem)

`bootloader.bin` and `partitions.bin` are optional but included when present. The release workflow already uploads these files from the tag build.

## Notes
- OTA README is generated on the fly inside the downloaded zip using the selected release tag/date.
- You can still keep the local `distributor/firmware` contents as a fallback or for offline demos, but GitHub Releases are the source of truth.
