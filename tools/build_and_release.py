#!/usr/bin/env python3
"""
Build and Release Script for Centauri Carbon Motion Detector
Builds firmware with safe defaults and optionally copies clean firmware to distributor directory.

Usage (run from repo root):

 python tools/build_and_release.py                                 # Standard release build
 python tools/build_and_release.py --env esp32s3              # Specific board
 python tools/build_and_release.py --version release                 # Release version increment
 python tools/build_and_release.py --env all                        # Build release for every supported board
 python tools/build_and_release.py --version skip --env esp32       # Build only, no version update

Features:
- Always ignores secrets by default while still allowing a deliberate merge via --merge-secrets (default=ignore)
- Version management: skip/build/ver/release (default=ver)
- Distributor copy: Only copies firmware_merged.bin when --ignore-secrets used
- Safety: Prevents distributor copy if secrets were merged into firmware
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path
from typing import Iterator, List, Optional

# Import shared functions from build_and_flash.py
SECRET_FILENAME = "secrets.json"

# Add tools directory to path to import from build_and_flash
tools_dir = str(Path(__file__).parent)
sys.path.append(tools_dir)

# Import shared board configuration
from board_config import compose_chip_family_label, validate_board_environment, get_supported_boards

# Import the timestamp and version creation functions from build_and_flash.py
try:
    from build_and_flash import create_build_version, create_build_timestamp, read_version_file
    print("Successfully imported build utility functions from build_and_flash.py")
except ImportError as e:
    print(f"WARNING: Could not import from build_and_flash.py: {e}")
    print("Falling back to local implementations")

    # Fallback implementations
    def read_version_file(repo_root: str) -> tuple[int, int, int]:
        """
        Read version from .version file. Returns (major, minor, build), ignoring any suffix after X.Y.Z.
        Accepts optional leading text (including a leading 'v').
        """
        version_path = os.path.join(repo_root, "data/.version")
        if not os.path.exists(version_path):
            return (0, 0, 0)

        try:
            with open(version_path, "r", encoding="utf-8") as f:
                version_str = f.read().strip()
                match = re.search(r"(\d+)\.(\d+)\.(\d+)", version_str)
                if match:
                    return (int(match.group(1)), int(match.group(2)), int(match.group(3)))
        except (ValueError, IOError):
            pass

        return (0, 0, 0)

    def read_version_string(repo_root: str) -> str:
        """
        Read the raw version string, stripping any leading text (including a leading 'v') but preserving suffixes.
        Falls back to '0.0.0' on error.
        """
        version_path = os.path.join(repo_root, "data/.version")
        try:
            with open(version_path, "r", encoding="utf-8") as f:
                raw = f.read().strip()
        except (IOError, OSError):
            return "0.0.0"

        if not raw:
            return "0.0.0"

        match = re.search(r"(?:v\s*)?(\d+\.\d+\.\d+)(.*)", raw, re.IGNORECASE)
        if match:
            base, suffix = match.group(1), match.group(2)
            return f"{base}{suffix}"

        return raw

    def create_build_version(data_dir: str, repo_root: str) -> str:
        """Create build_version.txt with current version from .version file, preserving suffixes."""
        version_str = read_version_string(repo_root)
        version_path = os.path.join(data_dir, "build_version.txt")
        with open(version_path, "w", encoding="utf-8") as f:
            f.write(version_str)
        print(f"Created build version file: v{version_str}")
        return version_path

    def create_build_timestamp(data_dir: str) -> str:
        """Create build_timestamp.txt with format MMDDYYHHMMSS for filesystem identification."""
        now = datetime.now()
        thumbprint = now.strftime("%m%d%y%H%M%S")
        timestamp_path = os.path.join(data_dir, "build_timestamp.txt")
        with open(timestamp_path, "w", encoding="utf-8") as f:
            f.write(thumbprint)
        print(f"Created filesystem build thumbprint: {thumbprint}")
        return timestamp_path

# Board to chip family mapping is now handled by tools/board_config.py

# Board to distributor directory mapping
BOARD_TO_DISTRIBUTOR_DIR = {
    "esp32s3": "esp32s3",
    "esp32c3": "esp32c3",
    "esp32": "esp32",
    "seeed_esp32s3": "seeed_esp32s3",
    "seeed_esp32c3": "seeed_esp32c3",
    "esp32c3supermini": "esp32c3supermini",
}

ALL_ENVIRONMENT = "all"


def get_distributor_dir_for_board(board_env: str) -> str:
    """
    Map a PlatformIO board environment name to its distributor firmware directory.
    
    Returns:
        The distributor directory name for the given `board_env`, or an empty string if there is no mapping.
    """
    return BOARD_TO_DISTRIBUTOR_DIR.get(board_env, "")


def resolve_target_environments(env_option: str) -> list[str]:
    """
    Return the list of board environments to build for the provided `--env` flag.
    """
    if env_option.lower() == ALL_ENVIRONMENT:
        return get_supported_boards()
    return [env_option]


@contextmanager
def temporarily_hide_files(paths: List[str]):
    """Temporarily move sensitive files out of data/ so LittleFS skips them."""
    backups = []
    staging_dir: Optional[str] = None
    try:
        for path in paths:
            if os.path.exists(path):
                if staging_dir is None:
                    staging_dir = tempfile.mkdtemp(prefix="fs_secrets_")
                backup = os.path.join(staging_dir, os.path.basename(path))
                shutil.move(path, backup)
                backups.append((path, backup))
        if backups:
            print("Excluding secret files from filesystem image for this build.")
        yield
    finally:
        for original, backup in reversed(backups):
            if os.path.exists(backup):
                shutil.move(backup, original)
        if staging_dir and os.path.isdir(staging_dir):
            shutil.rmtree(staging_dir, ignore_errors=True)


@contextmanager
def temporarily_merge_secrets(settings_path: str, secrets_path: str, ignore: bool) -> Iterator[bool]:
    """
    Temporarily merge values from a secrets file into a user settings JSON file for the duration of a context, restoring the original file afterwards.
    
    Parameters:
        settings_path (str): Path to the user settings JSON file to be modified.
        secrets_path (str): Path to the secrets JSON file supplying values to merge.
        ignore (bool): If True, do not merge secrets and immediately yield control.
    
    Returns:
        Iterator[bool]: `True` if secrets were merged into the settings for the context, `False` if merging was skipped due to `ignore`.
    
    Raises:
        FileNotFoundError: If `ignore` is False and `secrets_path` does not exist.
    """
    if ignore:
        yield False
        return

    if not os.path.exists(secrets_path):
        raise FileNotFoundError(
            f"Required secrets file not found at {secrets_path}. "
            "Create it or pass --ignore-secrets."
        )

    try:
        with open(settings_path, "r", encoding="utf-8") as f:
            original_text = f.read()
            base_settings = json.loads(original_text or "{}")
    except FileNotFoundError:
        original_text = ""
        base_settings = {}
    except Exception as e:  # pragma: no cover
        print(f"WARNING: Failed to read base user_settings.json: {e}")
        original_text = ""
        base_settings = {}

    try:
        with open(secrets_path, "r", encoding="utf-8") as f:
            secrets = json.load(f)
    except Exception as e:  # pragma: no cover
        print(f"WARNING: Failed to read {SECRET_FILENAME}: {e}")
        secrets = {}

    for key in ("ssid", "passwd", "elegooip"):
        if key in secrets:
            base_settings[key] = secrets[key]

    with open(settings_path, "w", encoding="utf-8") as f:
        json.dump(base_settings, f, indent=2)
        f.write("\n")
    print("Merged secrets into data/user_settings.json for this build.")

    try:
        yield True
    finally:
        with open(settings_path, "w", encoding="utf-8") as f:
            f.write(original_text)
        print("Restored original data/user_settings.json without secrets after build.")


def run(cmd: List[str], cwd: Optional[str] = None, extra_env: Optional[dict[str, str]] = None) -> None:
    """
    Execute a command, printing it and running it with an optional working directory and additional environment variables.
    
    Parameters:
        cmd (List[str]): Command and arguments to execute.
        cwd (Optional[str]): Working directory for the command; current directory used if None.
        extra_env (Optional[dict[str, str]]): Environment variables to merge into the subprocess environment.
    
    Raises:
        subprocess.CalledProcessError: If the command exits with a non-zero status.
    """
    print(f"> {' '.join(cmd)} (cwd={cwd or os.getcwd()})")

    # Set up environment for subprocess
    env_dict = os.environ.copy()
    if extra_env:
        env_dict.update(extra_env)

    subprocess.run(cmd, cwd=cwd, check=True, env=env_dict)


def run_with_build_env(
    cmd: List[str],
    board_env: str,
    build_env: dict[str, str],
    cwd: Optional[str] = None,
) -> None:
    """
    Execute a command with CHIP_FAMILY and FIRMWARE_VERSION provided to the subprocess environment.
    
    Parameters:
        cmd (List[str]): Command and arguments to execute.
        board_env (str): Logical build environment name used for logging.
        build_env (dict[str, str]): Environment variables to merge into the subprocess environment. May include keys "CHIP_FAMILY" and "FIRMWARE_VERSION" whose values will be exposed to the process.
        cwd (Optional[str]): Working directory for the command; if None the current working directory is used.
    """
    chip_family = build_env.get("CHIP_FAMILY", "")
    firmware_label = build_env.get("FIRMWARE_VERSION", "")
    print(
        f"Environment '{board_env}' -> CHIP_FAMILY='{chip_family}', "
        f"FIRMWARE_VERSION='{firmware_label}'"
    )
    run(cmd, cwd=cwd, extra_env=build_env)


def ensure_executable(name: str) -> None:
    """
    Verify that the given executable is available on the system PATH and terminate the process with a user-facing error message if it is not.
    
    If the executable is missing, prints an error and brief installation guidance (special-cased for `npm` and `python`/`python3`) and exits the program.
    
    Parameters:
        name (str): The executable name to check (e.g., "pio", "npm", "python").
    """
    if shutil.which(name) is None:
        print(f"ERROR: `{name}` is not on PATH.")
        if name == "npm":
            print("Please install Node.js (which includes npm) and try again.")
        elif name in ("python", "python3"):
            print("Please ensure Python is installed and available as `python`.")
        else:
            print(f"Install `{name}` or update your PATH.")
        sys.exit(1)


def read_version_file(repo_root: str) -> tuple[int, int, int]:
    """
    Read version from .version file. Returns (major, minor, build), ignoring any suffix after X.Y.Z.
    Accepts optional leading text (including a leading 'v').
    """
    version_path = os.path.join(repo_root, "data/.version")
    if not os.path.exists(version_path):
        # Initialize with 0.0.0 if file doesn't exist
        return (0, 0, 0)

    try:
        with open(version_path, "r", encoding="utf-8") as f:
            version_str = f.read().strip()
            match = re.search(r"(\d+)\.(\d+)\.(\d+)", version_str)
            if match:
                return (int(match.group(1)), int(match.group(2)), int(match.group(3)))
    except (ValueError, IOError):
        pass

    # Default to 0.0.0 on any error
    return (0, 0, 0)


def read_version_string(repo_root: str) -> str:
    """
    Read the raw version string, stripping any leading text (including a leading 'v') but preserving suffixes.
    Falls back to '0.0.0' on error.
    """
    version_path = os.path.join(repo_root, "data/.version")
    try:
        with open(version_path, "r", encoding="utf-8") as f:
            raw = f.read().strip()
    except (IOError, OSError):
        return "0.0.0"

    if not raw:
        return "0.0.0"

    match = re.search(r"(?:v\s*)?(\d+\.\d+\.\d+)(.*)", raw, re.IGNORECASE)
    if match:
        base, suffix = match.group(1), match.group(2)
        return f"{base}{suffix}"

    return raw


def write_version_file(repo_root: str, major: int, minor: int, build: int) -> str:
    """Write version to .version file. Returns version string."""
    version_path = os.path.join(repo_root, "data/.version")
    version_str = f"{major}.{minor}.{build}"
    with open(version_path, "w", encoding="utf-8") as f:
        f.write(version_str)
    return version_str


def increment_version(repo_root: str, increment_type: Optional[str]) -> Optional[str]:
    """
    Increment version based on type:
    - 'build': increment build number (0.0.0 -> 0.0.1)
    - 'ver': increment minor, reset build (0.0.12 -> 0.1.0)
    - 'release': increment major, reset minor and build (0.1.12 -> 1.0.0)
    Returns new version string or None if no increment requested.
    """
    if not increment_type:
        return None

    major, minor, build = read_version_file(repo_root)

    if increment_type == "build":
        build += 1
    elif increment_type == "ver":
        minor += 1
        build = 0
    elif increment_type == "release":
        major += 1
        minor = 0
        build = 0
    else:
        return None

    version_str = write_version_file(repo_root, major, minor, build)
    print(f"Version incremented to: v{version_str}")
    return version_str


def get_version_string(repo_root: str) -> str:
    """Return the current version string from .version as 'X.Y.Z'."""
    major, minor, build = read_version_file(repo_root)
    return f"{major}.{minor}.{build}"


def load_versioning_file(versioning_path: str) -> Optional[dict]:
    """
    Read distributor/assets/versioning JSON. Returns a dict or None on parse error.
    Creates a default structure if the file is missing.
    """
    if not os.path.exists(versioning_path):
        return {"version": None, "build_date": None, "release_notes": [], "boards": {}}

    try:
        with open(versioning_path, "r", encoding="utf-8") as f:
            return json.load(f)
    except json.JSONDecodeError as exc:  # pragma: no cover - defensive
        print(f"WARNING: Unable to parse versioning file ({versioning_path}): {exc}")
    except OSError as exc:  # pragma: no cover - defensive
        print(f"WARNING: Unable to read versioning file ({versioning_path}): {exc}")

    return None


def update_versioning_metadata(repo_root: str, version: str) -> None:
    """
    Update distributor/assets/versioning with the current version and build date.
    Preserves existing release notes and board notes.
    """
    versioning_path = os.path.join(repo_root, "distributor", "assets", "versioning")
    payload = load_versioning_file(versioning_path)
    if payload is None:
        print("Skipping distributor versioning update; unable to read current file.")
        return

    payload["version"] = version
    payload["build_date"] = datetime.now().strftime("%Y-%m-%d")

    try:
        with open(versioning_path, "w", encoding="utf-8") as f:
            json.dump(payload, f, indent=2)
            f.write("\n")
        print(f"Updated distributor versioning file: {version} @ {payload['build_date']}")
    except OSError as exc:  # pragma: no cover - defensive
        print(f"WARNING: Unable to write versioning file ({versioning_path}): {exc}")


def resolve_version_increment(version_action: Optional[str], version_type: str) -> Optional[str]:
    """
    Choose which version component to increment based on the provided CLI flags and fallback rules.
    
    Parameters:
        version_action (Optional[str]): Explicit action from CLI; expected values are "build", "ver", "release", or "skip".
        version_type (str): Default increment type when no explicit action is provided; expected values are "build", "ver", or "release".
    
    Returns:
        Optional[str]: `"build"`, `"ver"`, or `"release"` to indicate which component to increment, or `None` to skip incrementing.
    """
    if version_action == "skip":
        return None
    if version_action in ("build", "ver", "release"):
        return version_action
    if version_type in ("build", "ver", "release"):
        return version_type
    return None


def copy_to_distributor(repo_root: str, board_env: str, ignore_secrets: bool, chip_family_label: str) -> None:
    """
    Copy a clean firmware build into the distributor directory and create OTA artifacts and a README.
    
    Parameters:
    	repo_root (str): Path to the repository root where build and distributor directories reside.
    	board_env (str): PlatformIO board environment name used to locate build outputs.
    	ignore_secrets (bool): If True, copy a firmware image that does not contain merged secrets; if False, the copy is skipped.
    	chip_family_label (str): Human-readable chip family label used in the generated OTA README (e.g., "ESP32").
    """
    if not ignore_secrets:
        print("\n=== Skipping Distributor Copy ===")
        print("Distributor copy skipped because secrets were merged into firmware.")
        print("Use --ignore-secrets to create clean distributor firmware.")
        return

    # Get paths
    build_dir = os.path.join(repo_root, ".pio", "build", board_env)
    firmware_merged_src = os.path.join(build_dir, "firmware_merged.bin")
    firmware_src = os.path.join(build_dir, "firmware.bin")
    littlefs_src = os.path.join(build_dir, "littlefs.bin")

    if not os.path.exists(firmware_merged_src):
        print(f"\nWARNING: firmware_merged.bin not found at {firmware_merged_src}")
        print("Skipping distributor copy.")
        return

    # Get distributor directory
    chip_dir = get_distributor_dir_for_board(board_env)
    if not chip_dir:
        print(f"\nWARNING: Unknown distributor directory for board '{board_env}'")
        print("Skipping distributor copy.")
        return

    distributor_dir = os.path.join(repo_root, "distributor", "firmware", chip_dir)
    firmware_dst = os.path.join(distributor_dir, "firmware_merged.bin")

    # Create directory if it doesn't exist
    os.makedirs(distributor_dir, exist_ok=True)

    # Copy merged firmware
    try:
        shutil.copy2(firmware_merged_src, firmware_dst)
        file_size = os.path.getsize(firmware_dst)
        print(f"\n=== Distributor Copy ===")
        print(f"Copied firmware to: {firmware_dst}")
        print(f"File size: {file_size:,} bytes")
        print("Clean firmware ready for distribution.")
    except Exception as e:
        print(f"\nERROR: Failed to copy firmware to distributor: {e}")

    # Create OTA directory and copy individual files
    ota_dir = os.path.join(distributor_dir, "OTA")
    os.makedirs(ota_dir, exist_ok=True)

    # Copy firmware.bin (for OTA)
    firmware_ota_dst = os.path.join(ota_dir, "firmware.bin")
    try:
        shutil.copy2(firmware_src, firmware_ota_dst)
        print(f"Created OTA firmware: {firmware_ota_dst}")
    except Exception as e:
        print(f"\nERROR: Failed to copy OTA firmware: {e}")

    # Copy littlefs.bin (for OTA)
    if os.path.exists(littlefs_src):
        littlefs_ota_dst = os.path.join(ota_dir, "littlefs.bin")
        try:
            shutil.copy2(littlefs_src, littlefs_ota_dst)
            print(f"Created OTA filesystem: {littlefs_ota_dst}")
        except Exception as e:
            print(f"\nERROR: Failed to copy OTA filesystem: {e}")
    else:
        print(f"\nWARNING: littlefs.bin not found at {littlefs_src}")
        print("OTA filesystem will not be available.")

    # Generate OTA_readme.md
    ota_readme_dst = os.path.join(ota_dir, "OTA_readme.md")
    try:
        version = "1.3.0"  # TODO: Extract from build or git
        release_date = datetime.now().strftime("%Y-%m-%d")

        ota_readme_content = f"""# Centauri Carbon Motion Detector - OTA Update

## Version: {version}
## Board: {chip_dir} ({chip_family_label})
## Released: {release_date}

### Installation Instructions

#### New Installation:
1. Use firmware.bin with your preferred flashing tool (PlatformIO, esptool.py)
2. Ensure proper partition scheme for your board
3. First boot will automatically set up the web UI

#### OTA Update:
1. Access device web UI
2. Navigate to Update tab
3. Upload firmware.bin for application update, wait 10 seconds for reboot to complete
4. Upload littlefs.bin for filesystem update (if available)

### File Details
- **firmware.bin**: Main application binary
- **littlefs.bin**: Filesystem image with web UI and settings (including Wifi)

### Supported Hardware
- {chip_family_label}
- Board configuration: {chip_dir}

### Changes in This Version
- Filament motion detection improvements
- Enhanced web interface
- Bug fixes and optimizations

### Known Issues
- None reported

### Support
- GitHub https://github.com/harpua555/centauri-carbon-motion-detector/issues

---
Generated by build_and_release.py on {release_date}
"""

        with open(ota_readme_dst, 'w', encoding='utf-8') as f:
            f.write(ota_readme_content)
        print(f"Created OTA readme: {ota_readme_dst}")
        print(f"OTA directory ready: {ota_dir}")
    except Exception as e:
        print(f"\nERROR: Failed to create OTA readme: {e}")


def build_firmware(
    repo_root: str,
    board_env: str,
    ignore_secrets: bool,
    version_action: Optional[str],
    version_type: str,
    chip_family_label: str,
    firmware_label: str,
) -> None:
    """
    Build the firmware and filesystem for a specified board environment and prepare versioning metadata.
    
    Builds the WebUI (if present), runs PlatformIO to compile firmware for `board_env`, generates filesystem image with optional secret merging and temporary hiding of secret files, and updates distributor versioning metadata.
    
    Parameters:
        repo_root (str): Path to the repository root.
        board_env (str): PlatformIO environment name identifying the target board.
        ignore_secrets (bool): If True, secrets are not merged into the filesystem build and secret files may be copied to distributor outputs.
        version_action (Optional[str]): CLI-specified version action flag (e.g., 'increment'); used to decide version increment behavior.
        version_type (str): The type of version increment to apply when a version action is requested.
        chip_family_label (str): Label identifying the chip family to inject into the build environment (sets `CHIP_FAMILY`).
        firmware_label (str): Label used for the firmware version (sets `FIRMWARE_VERSION`).
    """
    # Resolve paths
    tools_dir = os.path.join(repo_root, "tools")
    webui_lite_dir = os.path.join(repo_root, "webui_lite")
    data_dir = os.path.join(repo_root, "data")
    secret_file_paths = [os.path.join(data_dir, SECRET_FILENAME)]
    settings_path = os.path.join(data_dir, "user_settings.json")
    secrets_path = os.path.join(data_dir, SECRET_FILENAME)

    # Ensure a base user_settings.json exists
    template_path = os.path.join(data_dir, "user_settings.template.json")
    if not os.path.exists(settings_path) and os.path.exists(template_path):
        shutil.copyfile(template_path, settings_path)
        print("Created data/user_settings.json from template (no secrets).")

    # Build WebUI
    npm_cmd = "npm.cmd" if os.name == "nt" else "npm"
    node_cmd = "node.exe" if os.name == "nt" else "node"
    ensure_executable(npm_cmd)
    ensure_executable(node_cmd)

    # Build lightweight webui
    print("\n=== Building Lightweight WebUI ===")
    if os.path.exists(webui_lite_dir):
        node_modules_dir = os.path.join(webui_lite_dir, "node_modules")
        if not os.path.exists(node_modules_dir):
            print("node_modules not found, running npm install...")
            subprocess.run([npm_cmd, "install"], cwd=webui_lite_dir, check=True)
        else:
            print("node_modules found, skipping npm install")

        subprocess.run([node_cmd, "build.js"], cwd=webui_lite_dir, check=True)

        # Sync artifacts to data/lite
        staging_output = os.path.join(repo_root, "data_lite")
        data_lite_subdir = os.path.join(data_dir, "lite")
        if os.path.exists(staging_output):
            if os.path.exists(data_lite_subdir):
                shutil.rmtree(data_lite_subdir)
            shutil.copytree(staging_output, data_lite_subdir)
            print(f"Lightweight UI deployed to {data_lite_subdir}")
        else:
            print(f"WARNING: Build output not found at {staging_output}")

    # Find PlatformIO command
    pio_cmd = shutil.which("pio") or shutil.which("platformio")
    if not pio_cmd:
        print("ERROR: Neither `pio` nor `platformio` is on PATH.")
        sys.exit(1)

    # Handle version increment
    increment_choice = resolve_version_increment(version_action, version_type)
    if increment_choice:
        increment_version(repo_root, increment_choice)

    current_version = get_version_string(repo_root)
    update_versioning_metadata(repo_root, current_version)

    build_env = {
        "CHIP_FAMILY": chip_family_label,
        "FIRMWARE_VERSION": firmware_label,
    }

    print(f"\n=== Building Firmware for {board_env} ===")
    print(f"Chip family: {chip_family_label}")
    print(f"Firmware label: {firmware_label}")

    # Build firmware
    build_cmd = [pio_cmd, "run", "-e", board_env]
    run_with_build_env(build_cmd, board_env, build_env, cwd=repo_root)

    # Build filesystem with timestamp and version info
    print(f"\n=== Building Filesystem ===")

    with temporarily_merge_secrets(settings_path, secrets_path, ignore_secrets):
        create_build_timestamp(data_dir)
        create_build_version(data_dir, repo_root)
        fs_cmd = [pio_cmd, "run", "-e", board_env, "-t", "buildfs"]
        with temporarily_hide_files(secret_file_paths):
            run_with_build_env(fs_cmd, board_env, build_env, cwd=repo_root)

    print(f"\n=== Build Complete ===")
    print(f"Firmware and filesystem built successfully for {board_env}")


def main() -> None:
    """Main build and release function."""
    parser = argparse.ArgumentParser(
        description="Build firmware with safe defaults and optionally copy to distributor directory."
    )
    parser.add_argument(
        "--env",
        default="esp32s3",
        help="PlatformIO environment to use (default: esp32s3). Use 'all' to build every supported board sequentially.",
    )
    parser.add_argument(
        "--version",
        choices=["skip", "build", "ver", "release"],
        default="ver",
        help="Version update action: skip=no update, build=build++, ver=version++, release=major++ (default: ver)",
    )
    parser.add_argument(
        "--version-type",
        choices=["build", "ver", "release"],
        default="ver",
        help="Type of version increment (default: ver)",
    )
    parser.add_argument(
        "--chip",
        default="ESP32",
        help="Chip prefix to combine with the board suffix (default: ESP32).",
    )
    parser.add_argument(
        "--firmware-label",
        default="alpha",
        help="Firmware version string to embed in the firmware binary (default: alpha).",
    )
    secrets_group = parser.add_mutually_exclusive_group()
    secrets_group.add_argument(
        "--ignore-secrets",
        dest="ignore_secrets",
        action="store_true",
        help="Skip merging data/secrets.json so release binaries stay clean (default).",
    )
    secrets_group.add_argument(
        "--merge-secrets",
        dest="ignore_secrets",
        action="store_false",
        help="Allow secrets to be merged into this build (use with caution).",
    )
    parser.set_defaults(ignore_secrets=True)

    args = parser.parse_args()

    # Resolve paths
    tools_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(tools_dir)

    selected_env = (args.env or "").strip()
    if not selected_env:
        print("ERROR: --env cannot be blank.")
        sys.exit(1)

    is_all_env = selected_env.lower() == ALL_ENVIRONMENT
    target_envs = resolve_target_environments(selected_env)

    if not target_envs:
        print("ERROR: No target environments resolved for this build.")
        sys.exit(1)

    if not is_all_env and not validate_board_environment(selected_env):
        print(f"ERROR: Unknown board environment '{selected_env}'")
        print("Supported environments:")
        for env in get_supported_boards():
            print(f"  {env}")
        sys.exit(1)

    firmware_label = args.firmware_label or "alpha"

    built_envs: list[str] = []
    first_board = True

    ignore_secrets = args.ignore_secrets
    for board_env in target_envs:
        if not validate_board_environment(board_env):
            print(f"ERROR: Unsupported board environment '{board_env}'")
            sys.exit(1)

        distributor_dir = get_distributor_dir_for_board(board_env)
        if not distributor_dir:
            print(f"ERROR: No distributor mapping for environment '{board_env}'")
            sys.exit(1)

        chip_family_label = compose_chip_family_label(board_env, args.chip)
        version_action = args.version if first_board else "skip"

        build_firmware(
            repo_root,
            board_env,
            ignore_secrets,
            version_action,
            args.version_type,
            chip_family_label,
            firmware_label,
        )

        copy_to_distributor(repo_root, board_env, ignore_secrets, chip_family_label)

        built_envs.append(board_env)
        first_board = False

    print("\n=== Release Complete ===")
    print(f"Built boards: {', '.join(built_envs)}")
    print(f"Version: {get_version_string(repo_root)}")
    print("Distributor firmware directories updated for each board above.")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        print(f"\nCommand failed with exit code {exc.returncode}: {exc.cmd}")
        sys.exit(exc.returncode)
    except KeyboardInterrupt:
        print("\nBuild interrupted by user")
        sys.exit(1)
    except Exception as exc:
        print(f"\nUnexpected error: {exc}")
        sys.exit(1)
