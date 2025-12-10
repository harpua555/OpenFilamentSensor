# GitHub Workflows & Distributor Artifacts

## PR CI and Build All Boards (`.github/workflows/build-and-CI.yml`)
- Triggers on PRs to `dev` or manual dispatch.
- Runs simulator/unit tests, then builds every PlatformIO env from `tools/build-targets.yml`.
- Uploads build artifacts for each env (not used by the distributor).

## Release Firmware (`.github/workflows/release.yml`)
- Triggers on `v*` tags, PRs to `build`/`main`, or manual dispatch.
- Matrix-builds all boards, creating:
  - `<env>-firmware.bin`
  - `<env>-firmware_merged.bin`
  - `<env>-littlefs.bin`
  - `<env>-bootloader.bin` (when present)
  - `<env>-partitions.bin` (when present)
- Collates all `.bin` files plus `checksums.txt` and uploads them as GitHub Release assets. The release version comes from the tag (`v0.6.1-alpha` → `0.6.1-alpha` embedded in `data/.version` and build metadata).

## Distributor Consumption (source of truth = GitHub Releases)
- The distributor UI now pulls binaries directly from the latest (or selected) GitHub Release rather than cloning the `build` branch.
- Expected asset names per board: `<env>-firmware_merged.bin` (WebSerial flashing), `<env>-firmware.bin` (OTA app), `<env>-littlefs.bin` (OTA filesystem). Bootloader/partitions are also consumed if present.
- You can force a specific release in the UI with `?tag=vX.Y.Z`; otherwise it uses the latest release. If the API is unavailable (rate limits/offline), it falls back to the local `distributor/firmware` tree.
