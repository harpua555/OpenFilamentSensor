# GitHub Workflows & Distributor Artifacts

This project uses two focused GitHub Actions workflows:

1. **Pull Request CI (`.github/workflows/ci.yml`)**
   - Triggers only on `pull_request` branches `main`/`dev` or via manual dispatch.
   - Steps:
     - Installs `g++` on Ubuntu and compiles the pulse simulator test harness (`test/build_tests.sh`).
     - Sets up Python (3.11) and Node (v20) and installs PlatformIO (plus `pyyaml`).
     - Runs `tools/build_and_release.py --env all --version skip` so every supported board builds with the default clean settings (no secrets merged) without touching secrets or flashing hardware.
   - Outcome: verifies the simulator tests plus a clean build (the script keeps secrets out of those artifacts by default) for every PlatformIO environment defined in `tools/build-targets.yml`, ensuring PRs validate all boards without producing releases.

2. **Release Workflow (`.github/workflows/release.yml`)**
   - Manually dispatched via the Actions UI.
   - Inputs:
     - `version-action`: choose `build`, `ver`, or `release` to bump build/minor/major versions.
     - `firmware-label`: text injected into each binary’s metadata.
     - `prerelease`: flag to mark the GitHub Release as prerelease.
   - Steps:
     - Installs Python/Node/PlatformIO (with `pyyaml`).
     - Runs `tools/build_and_release.py --env all --ignore-secrets --version <version-action> --firmware-label <firmware-label>` with the chosen version action and label; this script builds firmware/filesystem for every board sequentially and copies clean `firmware_merged.bin` plus OTA artifacts into `distributor/firmware/<board>` (see below). Secrets stay out of these binaries thanks to the explicit `--ignore-secrets` flag.
     - Collects `.pio/build/<env>` binaries (`firmware.bin`, `firmware_merged.bin`, `littlefs.bin`, `bootloader.bin`, `partitions.bin`) into `release-assets/`, listing them and generating `checksums.txt`.
     - Reads `data/.version` for the release tag, captures the latest commit message, and publishes a GitHub Release via `softprops/action-gh-release@v2`, attaching `release-assets/*` and referencing the checksum file.

## Expected GitHub Artifacts

| Artifact | Description | Source |
 | --- | --- | --- |
 | `release-assets/<env>-firmware.bin` | Raw application binary (used for OTA updates or custom flashing). | `.pio/build/<env>/firmware.bin` |
 | `release-assets/<env>-firmware_merged.bin` | Combined binary suitable for single-shot flashing (contains bootloader/partitions). | `.pio/build/<env>/firmware_merged.bin` |
 | `release-assets/<env>-littlefs.bin` | LittleFS filesystem image containing the web UI content. | `.pio/build/<env>/littlefs.bin` |
 | `release-assets/<env>-bootloader.bin` | Bootloader copy from each board build (when present). | `.pio/build/<env>/bootloader.bin` |
 | `release-assets/<env>-partitions.bin` | Board-specific partition table image. | `.pio/build/<env>/partitions.bin` |
 | `release-assets/checksums.txt` | SHA256 digest for every `.bin` file emitted in `release-assets/`. | `sha256sum release-assets/*.bin` |

> The release workflow uploads `release-assets/*` to the GitHub Release and uses `checksums.txt` for integrity verification.

### How Distributor Files Align

The release build script also keeps `distributor/firmware/` populated:

| PlatformIO env | Distributor path |
| --- | --- |
| `esp32s3` | `distributor/firmware/esp32s3/firmware_merged.bin` + OTA folder |
| `seeed_esp32c3` | `distributor/firmware/seeed_esp32c3/` |
| `esp32` | `distributor/firmware/esp32/` |
| `esp32c3` | `distributor/firmware/esp32c3/` |
| `seeed_esp32s3` | `distributor/firmware/seeed_esp32s3/` |
| `esp32c3supermini` | `distributor/firmware/esp32c3supermini/` |

Each board directory gets:
- `firmware_merged.bin` for self-contained flashing.
- An `OTA/` subdirectory containing `firmware.bin`, `littlefs.bin`, and a generated `OTA_readme.md` (the README is currently populated with placeholder release information and release notes).

The `distributor/assets/versioning` file records the last released version/date/status and board notes; `tools/build_and_release.py` updates this file during clean builds (secrets are not merged) using `data/.version`.

### Alignment Summary

- **CI Flow** validates builds/tests for every board but does not update the distributor or create release tags/artifacts.
- **Release Flow** produces the exact binaries copied under `distributor/firmware/` and also publishes `release-assets/*` attachments, ensuring the GitHub Release and distributor content stay in sync with `data/.version` and `distributor/assets/versioning`.
- To keep distributor artifacts current, rerun the release workflow (or locally `python tools/build_and_release.py --env all --version skip`) before pushing new release tags.
