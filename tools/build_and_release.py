#!/usr/bin/env python3
"""
Build and Release Script for Centauri Carbon Motion Detector
Builds firmware with safe defaults and optionally copies clean firmware to distributor directory.

Usage (run from repo root):

 python tools/build_and_release.py                                 # Standard release build
 python tools/build_and_release.py --env esp32-s3-dev              # Specific board
 python tools/build_and_release.py --version release                 # Release version increment
 python tools/build_and_release.py --version skip --env esp32-dev       # Build only, no version update

Features:
- Always uses safe defaults: --local --ignore-secrets --increment-version
- Version management: skip/build/ver/release (default=ver)
- Distributor copy: Only copies firmware_merged.bin when --ignore-secrets used
- Safety: Prevents distributor copy if secrets were merged into firmware
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile
from contextlib import contextmanager
from datetime import datetime
from typing import Iterator, List, Optional

# Import shared functions from build_and_flash.py
SECRET_FILENAME = "secrets.json"

# Board to chip family mapping for CHIP_FAMILY environment variable
BOARD_TO_CHIP_FAMILY = {
    "esp32-dev": "ESP32",
    "esp32-build": "ESP32",
    "esp32-s3-dev": "ESP32-S3",
    "esp32-s3-build": "ESP32-S3",
    "seeed_xiao_esp32s3-dev": "ESP32-S3",
    "seeed_xiao_esp32s3-build": "ESP32-S3",
    "seeed_xiao_esp32c3-dev": "ESP32-C3",
    "seeed_xiao_esp32c3-build": "ESP32-C3",
    "esp32-c3-supermini-dev": "ESP32-C3",
    "esp32-c3-supermini-ota": "ESP32-C3",
    "esp32c3supermini": "ESP32-C3",
}

# Board to distributor directory mapping
BOARD_TO_DISTRIBUTOR_DIR = {
    "esp32-dev": "esp32",
    "esp32-build": "esp32",
    "esp32-s3-dev": "esp32s3",
    "esp32-s3-build": "esp32s3",
    "seeed_xiao_esp32s3-dev": "seeed_xaio_esp32s3",
    "seeed_xiao_esp32s3-build": "seeed_xaio_esp32s3",
    "seeed_xiao_esp32c3-dev": "seeed_xaio_esp32c3",
    "seeed_xiao_esp32c3-build": "seeed_xaio_esp32c3",
    "esp32-c3-supermini-dev": "esp32c3supermini",
    "esp32-c3-supermini-ota": "esp32c3supermini",
    "esp32c3supermini": "esp32c3supermini",
}


def get_chip_family_for_board(board_env: str) -> str:
    """
    Get the chip family for a given PlatformIO environment name.
    Returns the appropriate chip family string or empty string if unknown.
    """
    return BOARD_TO_CHIP_FAMILY.get(board_env, "")


def get_distributor_dir_for_board(board_env: str) -> str:
    """
    Get the distributor firmware directory for a given PlatformIO environment.
    Returns the appropriate directory name or empty string if unknown.
    """
    return BOARD_TO_DISTRIBUTOR_DIR.get(board_env, "")


def run(cmd: List[str], cwd: Optional[str] = None, env: Optional[str] = None) -> None:
    """Run command with optional environment variable."""
    print(f"> {' '.join(cmd)} (cwd={cwd or os.getcwd()})")

    # Set up environment for subprocess
    env_dict = os.environ.copy()
    if env:
        env_dict['CHIP_FAMILY'] = env

    subprocess.run(cmd, cwd=cwd, check=True, env=env_dict)


def run_with_chip_family(cmd: List[str], board_env: str, cwd: Optional[str] = None) -> None:
    """
    Run a command with CHIP_FAMILY environment variable set based on board configuration.
    """
    chip_family = get_chip_family_for_board(board_env)
    run(cmd, cwd=cwd, env=chip_family)


def ensure_executable(name: str) -> None:
    """Check if required executable exists."""
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
    """Read version from .version file. Returns (major, minor, build)."""
    version_path = os.path.join(repo_root, "data/.version")
    if not os.path.exists(version_path):
        # Initialize with 0.0.0 if file doesn't exist
        return (0, 0, 0)

    try:
        with open(version_path, "r", encoding="utf-8") as f:
            version_str = f.read().strip()
            parts = version_str.split(".")
            if len(parts) == 3:
                return (int(parts[0]), int(parts[1]), int(parts[2]))
    except (ValueError, IOError):
        pass

    # Default to 0.0.0 on any error
    return (0, 0, 0)


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


def copy_to_distributor(repo_root: str, board_env: str, ignore_secrets: bool) -> None:
    """
    Copy firmware_merged.bin to distributor directory if secrets were ignored.
    Only copies when ignore_secrets is True to ensure clean firmware.
    """
    if not ignore_secrets:
        print("\n=== Skipping Distributor Copy ===")
        print("Distributor copy skipped because secrets were merged into firmware.")
        print("Use --ignore-secrets to create clean distributor firmware.")
        return

    # Get paths
    build_dir = os.path.join(repo_root, ".pio", "build", board_env)
    firmware_src = os.path.join(build_dir, "firmware_merged.bin")

    if not os.path.exists(firmware_src):
        print(f"\nWARNING: firmware_merged.bin not found at {firmware_src}")
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

    # Copy firmware
    try:
        shutil.copy2(firmware_src, firmware_dst)
        file_size = os.path.getsize(firmware_dst)
        print(f"\n=== Distributor Copy ===")
        print(f"Copied firmware to: {firmware_dst}")
        print(f"File size: {file_size:,} bytes")
        print("Clean firmware ready for distribution.")
    except Exception as e:
        print(f"\nERROR: Failed to copy firmware to distributor: {e}")


def build_firmware(repo_root: str, board_env: str, ignore_secrets: bool, version_action: Optional[str], version_type: str) -> None:
    """
    Build firmware using build_and_flash.py functionality.
    This replicates the core build process with our specific flags.
    """
    # Resolve paths
    tools_dir = os.path.join(repo_root, "tools")
    webui_lite_dir = os.path.join(repo_root, "webui_lite")
    data_dir = os.path.join(repo_root, "data")
    secret_file_paths = [os.path.join(data_dir, SECRET_FILENAME)]

    # Ensure a base user_settings.json exists
    settings_path = os.path.join(data_dir, "user_settings.json")
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
    if version_action:
        increment_version(repo_root, version_type)

    print(f"\n=== Building Firmware for {board_env} ===")
    print(f"Chip family: {get_chip_family_for_board(board_env)}")

    # Build firmware
    build_cmd = [pio_cmd, "run", "-e", board_env]
    run_with_chip_family(build_cmd, board_env, cwd=repo_root)

    # Build filesystem
    print(f"\n=== Building Filesystem ===")
    fs_cmd = [pio_cmd, "run", "-e", board_env, "-t", "buildfs"]
    run_with_chip_family(fs_cmd, board_env, cwd=repo_root)

    print(f"\n=== Build Complete ===")
    print(f"Firmware and filesystem built successfully for {board_env}")


def main() -> None:
    """Main build and release function."""
    parser = argparse.ArgumentParser(
        description="Build firmware with safe defaults and optionally copy to distributor directory."
    )
    parser.add_argument(
        "--env",
        default="esp32-s3-dev",
        help="PlatformIO environment to use (default: esp32-s3-dev)",
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

    args = parser.parse_args()

    # Resolve paths
    tools_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(tools_dir)

    # Validate environment
    chip_family = get_chip_family_for_board(args.env)
    if not chip_family:
        print(f"ERROR: Unknown board environment '{args.env}'")
        print("Supported environments:")
        for env in sorted(BOARD_TO_CHIP_FAMILY.keys()):
            print(f"  {env}")
        sys.exit(1)

    distributor_dir = get_distributor_dir_for_board(args.env)
    if not distributor_dir:
        print(f"ERROR: No distributor mapping for environment '{args.env}'")
        sys.exit(1)

    # Build firmware
    build_firmware(repo_root, args.env, True, args.version, args.version_type)

    # Copy to distributor (always use --ignore-secrets behavior)
    copy_to_distributor(repo_root, args.env, True)

    print("\n=== Release Complete ===")
    print(f"Board: {args.env}")
    print(f"Chip family: {chip_family}")
    print(f"Distributor directory: {distributor_dir}")


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