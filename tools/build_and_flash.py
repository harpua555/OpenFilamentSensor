#!/usr/bin/env python3
"""
Build the lightweight (HTML-only) Web UI, sync it into the data/ filesystem image, and
flash firmware + filesystem to an ESP32 using PlatformIO.

Usage (run from repo root):

  python tools/build_and_flash.py                    # Build Lite UI + firmware

Options:
  --env ENV            PlatformIO env to use (default: esp32s3)
  --local              Only build Lite UI/filesystem; skip PlatformIO upload steps
  --ignore-secrets     Do not merge or package data/secrets.json
  --build-mode MODE    Build mode: (default) full build with merge, 'nofs' firmware only, 'nobin' filesystem only
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
from typing import Dict, Iterator, List, Optional

# Import shared board configuration
from board_config import compose_chip_family_label, get_supported_boards, validate_board_environment

SECRET_FILENAME = "secrets.json"


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
    Temporarily merge secrets from a secrets file into the given user settings file for the duration of a context.
    
    When `ignore` is True, this context does nothing and yields False. Otherwise, reads secrets from `secrets_path`, merges the keys "ssid", "passwd", and "elegooip" into the JSON at `settings_path`, writes the merged settings to disk, and yields True; on context exit the original contents of `settings_path` are restored. Raises FileNotFoundError if `secrets_path` does not exist and `ignore` is False.
    
    Parameters:
        settings_path (str): Path to the user settings JSON file that will be modified temporarily.
        secrets_path (str): Path to the secrets JSON file providing values to merge.
        ignore (bool): If True, skip merging and yield False.
    
    Returns:
        bool: `True` if secrets were merged for the context, `False` if merging was skipped due to `ignore`.
    
    Side effects:
        Overwrites `settings_path` during the context and restores its original contents on exit.
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


def run(cmd: List[str], cwd: Optional[str] = None, extra_env: Optional[Dict[str, str]] = None) -> None:
    """
    Execute a subprocess command after printing it and optionally extending the environment.
    
    Runs the given command in an optional working directory, merges `extra_env` into the current process environment for the subprocess, and raises subprocess.CalledProcessError if the command exits with a non-zero status.
    
    Parameters:
        cmd (List[str]): Command and arguments to execute.
        cwd (Optional[str]): Working directory for the subprocess; uses the current working directory if None.
        extra_env (Optional[Dict[str, str]]): Environment variables to add or override for the subprocess.
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
    build_env: Dict[str, str],
    cwd: Optional[str] = None,
) -> None:
    """
    Log the resolved CHIP_FAMILY and FIRMWARE_VERSION for a PlatformIO environment and run the given command with those environment variables applied.
    
    Parameters:
        cmd (List[str]): The command and arguments to execute.
        board_env (str): The PlatformIO environment name used for display/logging.
        build_env (Dict[str, str]): Environment mapping; expected to contain `CHIP_FAMILY` and `FIRMWARE_VERSION` which are injected into the subprocess environment.
        cwd (Optional[str]): Working directory to run the command in, or None to use the current directory.
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
    Ensure the given executable is available on the system PATH and exit with status 1 if it is not.
    
    Parameters:
        name (str): The command/executable name to check (e.g., 'npm', 'python').
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
    Read version from .version file. Returns (major, minor, build), ignoring any suffix after the base X.Y.Z.
    Accepts optional leading text (including a leading 'v') and optional suffixes like "-alpha".
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
    Read the raw version string from data/.version, stripping any leading text (including a leading 'v')
    but preserving any suffix after the numeric X.Y.Z pattern. Falls back to '0.0.0' on error.
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
    """
    Create build_version.txt with the normalized version string from .version, allowing suffixes like '-alpha'.
    """
    version_str = read_version_string(repo_root)
    version_path = os.path.join(data_dir, "build_version.txt")
    with open(version_path, "w", encoding="utf-8") as f:
        f.write(version_str)
    print(f"Created build version file: v{version_str}")
    return version_path


def create_build_timestamp(data_dir: str) -> str:
    """Create build_timestamp.txt with format MMDDYYHHMMSS for filesystem identification."""
    override = os.environ.get("BUILD_TIMESTAMP_OVERRIDE", "").strip()
    if override:
        if not re.fullmatch(r"\d{12}", override):
            raise ValueError(
                "BUILD_TIMESTAMP_OVERRIDE must be 12 digits in MMDDYYHHMMSS format."
            )
        thumbprint = override
    else:
        now = datetime.now()  # Use local time instead of UTC
        # Format: MMDDYYHHMMSS
        thumbprint = now.strftime("%m%d%y%H%M%S")
    timestamp_path = os.path.join(data_dir, "build_timestamp.txt")
    with open(timestamp_path, "w", encoding="utf-8") as f:
        f.write(thumbprint)
    print(f"Created filesystem build thumbprint: {thumbprint}")
    return timestamp_path


def main() -> None:
    """
    Build and optionally flash a lightweight Web UI and firmware for ESP32 using PlatformIO.
    
    Parses CLI options to control which artifacts to build: lightweight web UI/filesystem, firmware, or both;
    whether to merge per-run secrets into the filesystem image; and version increment behavior.
    Creates temporary build timestamp and version files for filesystem images and removes them after the run.
    Exits with non‑zero status for unsupported board environments or missing required tools.
    """
    parser = argparse.ArgumentParser(
        description="Build Lite WebUI + firmware and flash to ESP32 via PlatformIO."
    )
    parser.add_argument(
        "--env",
        default="esp32s3",
        help="PlatformIO environment to use (default: esp32s3)",
    )
    parser.add_argument(
        "--local",
        action="store_true",
        help="Only build the Lite UI/filesystem; skip PlatformIO upload steps.",
    )
    parser.add_argument(
        "--ignore-secrets",
        action="store_true",
        help="Skip merging data/secrets.json and omit secrets files from the filesystem image.",
    )
    parser.add_argument(
        "--build-mode",
        choices=["nofs", "nobin"],
        default=None,
        help="Build mode: 'nofs' = firmware only (no merge), 'nobin' = filesystem only (no merge), default = full build with merge",
    )
    parser.add_argument(
        "--chip",
        default="ESP32",
        help="Chip prefix to combine with the board suffix (default: ESP32).",
    )
    parser.add_argument(
        "--firmware-label",
        default="alpha",
        help="Firmware version string to embed into the firmware binary (default: alpha).",
    )

    args = parser.parse_args()

    if not validate_board_environment(args.env):
        from board_config import get_supported_boards
        print(f"ERROR: Unsupported board environment '{args.env}'")
        print("Supported environments:")
        for env_name in get_supported_boards():
            print(f"  {env_name}")
        sys.exit(1)

    firmware_label = args.firmware_label or "alpha"
    chip_family_label = compose_chip_family_label(args.env, args.chip)
    build_env = {
        "CHIP_FAMILY": chip_family_label,
        "FIRMWARE_VERSION": firmware_label,
    }

    # Resolve paths relative to this file (tools/ -> repo root)
    tools_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(tools_dir)
    webui_lite_dir = os.path.join(repo_root, "webui_lite")
    data_dir = os.path.join(repo_root, "data")
    data_lite_subdir = os.path.join(data_dir, "lite")
    secret_file_paths: List[str] = [os.path.join(data_dir, SECRET_FILENAME)]
    legacy_secret_paths = glob.glob(os.path.join(data_dir, "user_settings.secrets.json*"))
    if legacy_secret_paths:
        print("ERROR: Legacy secrets files detected in data/:")
        for path in legacy_secret_paths:
            print(f"  - {path}")
        print("Rename or move secrets into data/secrets.json and remove the old files.")
        sys.exit(1)

    # Ensure a base user_settings.json exists; this file should be safe to commit and can
    # leave SSID/password/IP blank. Per-run secrets are provided via
    # data/secrets.json and merged only for the filesystem image.
    settings_path = os.path.join(data_dir, "user_settings.json")
    template_path = os.path.join(data_dir, "user_settings.template.json")
    if not os.path.exists(settings_path) and os.path.exists(template_path):
        shutil.copyfile(template_path, settings_path)
        print("Created data/user_settings.json from template (no secrets).")

    # Build WebUI(s) and sync into data/
    npm_cmd = "npm.cmd" if os.name == "nt" else "npm"
    node_cmd = "node.exe" if os.name == "nt" else "node"
    ensure_executable(npm_cmd)
    ensure_executable(node_cmd)

    # Always build lightweight webui_lite so data/lite stays in sync (unless nofs mode)
    if args.build_mode != "nofs":
        print("\n=== Building Lightweight WebUI ===")
        if not os.path.exists(webui_lite_dir):
            print(f"WARNING: webui_lite/ directory not found at {webui_lite_dir}")
            print("Skipping lightweight UI build.")
        else:
            # Auto-detect if npm install is needed
            node_modules_dir = os.path.join(webui_lite_dir, "node_modules")
            if not os.path.exists(node_modules_dir):
                print("node_modules not found, running npm install...")
                run([npm_cmd, "install"], cwd=webui_lite_dir)
            else:
                print("node_modules found, skipping npm install")

            run([node_cmd, "build.js"], cwd=webui_lite_dir)

            # Ensure staging artifacts land in data/lite/
            staging_output = os.path.join(repo_root, "data_lite")
            if os.path.exists(staging_output):
                shutil.copytree(staging_output, data_lite_subdir, dirs_exist_ok=True)
                print(f"Lightweight UI deployed to {data_lite_subdir}")
            else:
                print(f"WARNING: Build output not found at {staging_output}")
                print("Lightweight UI build may have failed.")

    # Flash firmware + filesystem using PlatformIO CLI (pio or platformio)
    pio_cmd = shutil.which("pio") or shutil.which("platformio")
    if not pio_cmd:
        print(
            "ERROR: Neither `pio` nor `platformio` is on PATH.\n"
            "Install PlatformIO Core (pip install platformio) "
            "and/or add its Scripts directory to PATH."
        )
        sys.exit(1)

    secrets_path = secret_file_paths[0]

    if args.ignore_secrets:
        print("Skipping data/secrets.json merge (--ignore-secrets).")

    timestamp_path: Optional[str] = None
    version_path: Optional[str] = None
    try:
        # Build mode: nofs = firmware only (no merge)
        if args.build_mode == "nofs":
            print("\n=== Build Mode: Firmware Only (no merge) ===")
            if args.local:
                run_with_build_env([pio_cmd, "run", "-e", args.env], args.env, build_env, cwd=repo_root)
                print("\nAll done. Firmware binary ready at `.pio/build/{}/firmware.bin`.".format(args.env))
            else:
                run_with_build_env([pio_cmd, "run", "-e", args.env, "-t", "upload"], args.env, build_env, cwd=repo_root)
                print("\nAll done. Firmware has been flashed.")

        # Build mode: nobin = filesystem only (no merge)
        elif args.build_mode == "nobin":
            print("\n=== Build Mode: Filesystem Only (no merge) ===")
            with temporarily_merge_secrets(settings_path, secrets_path, args.ignore_secrets):
                timestamp_path = create_build_timestamp(data_dir)
                version_path = create_build_version(data_dir, repo_root)
                fs_target = "uploadfs" if not args.local else "buildfs"
                with temporarily_hide_files(secret_file_paths):
                    run_with_build_env([pio_cmd, "run", "-e", args.env, "-t", fs_target], args.env, build_env, cwd=repo_root)

            if args.local:
                print("\nAll done. Filesystem binary ready at `.pio/build/{}/littlefs.bin`.".format(args.env))
            else:
                print("\nAll done. Filesystem has been flashed.")

        # Default mode: full build with merge
        else:
            print("\n=== Build Mode: Full Build (with merge) ===")
            with temporarily_merge_secrets(settings_path, secrets_path, args.ignore_secrets):
                # Create filesystem build timestamp and version before building filesystem
                timestamp_path = create_build_timestamp(data_dir)
                version_path = create_build_version(data_dir, repo_root)

                # Filesystem upload/build (uses merged settings if present)
                fs_target = "uploadfs" if not args.local else "buildfs"
                with temporarily_hide_files(secret_file_paths):
                    run_with_build_env([pio_cmd, "run", "-e", args.env, "-t", fs_target], args.env, build_env, cwd=repo_root)

                if args.local:
                    run_with_build_env([pio_cmd, "run", "-e", args.env], args.env, build_env, cwd=repo_root)
                    print("\nAll done. Build artifacts are ready in `data/` and `.pio/build`.")
                else:
                    # Firmware upload (will build firmware first if needed)
                    run_with_build_env([pio_cmd, "run", "-e", args.env, "-t", "upload"], args.env, build_env, cwd=repo_root)
                    print("\nAll done. Firmware and filesystem have been flashed.")
    except FileNotFoundError as exc:
        print(f"ERROR: {exc}")
        sys.exit(1)
    finally:
        if timestamp_path and os.path.exists(timestamp_path):
            os.remove(timestamp_path)
        if version_path and os.path.exists(version_path):
            os.remove(version_path)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        print(f"\nCommand failed with exit code {exc.returncode}: {exc.cmd}")
        sys.exit(exc.returncode)
