#!/usr/bin/env python3
"""
Build the lightweight (HTML-only) Web UI, sync it into the data/ filesystem image, and
flash firmware + filesystem to an ESP32 using PlatformIO.

Usage (run from repo root):

  python tools/build_and_flash.py                    # Build Lite UI + firmware

Options:
  --env ENV            PlatformIO env to use (default: esp32-s3-dev)
  --skip-npm-install   Skip `npm install` in webui_lite/ (assumes deps already installed)
  --local              Only build Lite UI/filesystem; skip PlatformIO upload steps
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from typing import List, Optional


def run(cmd: List[str], cwd: Optional[str] = None) -> None:
    print(f"> {' '.join(cmd)} (cwd={cwd or os.getcwd()})")
    subprocess.run(cmd, cwd=cwd, check=True)


def ensure_executable(name: str) -> None:
    if shutil.which(name) is None:
        print(f"ERROR: `{name}` is not on PATH.")
        if name == "npm":
            print("Please install Node.js (which includes npm) and try again.")
        elif name in ("python", "python3"):
            print("Please ensure Python is installed and available as `python`.")
        else:
            print(f"Install `{name}` or update your PATH.")
        sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build Lite WebUI + firmware and flash to ESP32 via PlatformIO."
    )
    parser.add_argument(
        "--env",
        default="esp32-s3-dev",
        help="PlatformIO environment to use (default: esp32-s3-dev)",
    )
    parser.add_argument(
        "--skip-npm-install",
        action="store_true",
        help="Skip `npm install` in webui_lite/ (assumes dependencies already installed).",
    )
    parser.add_argument(
        "--local",
        action="store_true",
        help="Only build the Lite UI/filesystem; skip PlatformIO upload steps.",
    )
    args = parser.parse_args()

    # Resolve paths relative to this file (tools/ -> repo root)
    tools_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(tools_dir)
    webui_lite_dir = os.path.join(repo_root, "webui_lite")
    data_dir = os.path.join(repo_root, "data")
    data_lite_subdir = os.path.join(data_dir, "lite")

    # Ensure a base user_settings.json exists; this file should be safe to commit and can
    # leave SSID/password/IP blank. Per-run secrets are provided via
    # user_settings.secrets.json and merged only for the filesystem image.
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

    # Always build lightweight webui_lite so data/lite stays in sync
    print("\n=== Building Lightweight WebUI ===")
    if not os.path.exists(webui_lite_dir):
        print(f"WARNING: webui_lite/ directory not found at {webui_lite_dir}")
        print("Skipping lightweight UI build.")
    else:
        if not args.skip_npm_install and os.path.exists(os.path.join(webui_lite_dir, "package.json")):
            run([npm_cmd, "install"], cwd=webui_lite_dir)

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

    # Before uploading the filesystem, merge any local secrets into a temporary copy of
    # user_settings.json so you don't have to commit your SSID/password/IP. After the
    # upload, the original file contents are restored so the repo stays clean.
    secrets_path = os.path.join(data_dir, "user_settings.secrets.json")
    base_settings_text: Optional[str] = None
    if os.path.exists(secrets_path):
        try:
            with open(settings_path, "r", encoding="utf-8") as f:
                base_settings_text = f.read()
                base_settings = json.loads(base_settings_text or "{}")
        except Exception as e:  # pragma: no cover
            print(f"WARNING: Failed to read base user_settings.json: {e}")
            base_settings = {}

        try:
            with open(secrets_path, "r", encoding="utf-8") as f:
                secrets = json.load(f)
        except Exception as e:  # pragma: no cover
            print(f"WARNING: Failed to read user_settings.secrets.json: {e}")
            secrets = {}

        for key in ("ssid", "passwd", "elegooip"):
            if key in secrets:
                base_settings[key] = secrets[key]

        # Write merged settings for the build only
        with open(settings_path, "w", encoding="utf-8") as f:
            json.dump(base_settings, f, indent=2)
            f.write("\n")
        print("Merged secrets into data/user_settings.json for this build (not committed).")

    # Filesystem upload/build (uses merged settings if present)
    fs_target = "uploadfs" if not args.local else "buildfs"
    run([pio_cmd, "run", "-e", args.env, "-t", fs_target], cwd=repo_root)

    # Restore the original committed user_settings.json so secrets are not left in the
    # working tree. This keeps git status clean and avoids accidental commits.
    if base_settings_text is not None:
        with open(settings_path, "w", encoding="utf-8") as f:
            f.write(base_settings_text)
        print("Restored original data/user_settings.json after filesystem upload.")

    if args.local:
        run([pio_cmd, "run", "-e", args.env], cwd=repo_root)
        print("\nAll done. Build artifacts are ready in `data/` and `.pio/build`.")
    else:
        # Firmware upload (will build firmware first if needed)
        run([pio_cmd, "run", "-e", args.env, "-t", "upload"], cwd=repo_root)
        print("\nAll done. Firmware and filesystem have been flashed.")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        print(f"\nCommand failed with exit code {exc.returncode}: {exc.cmd}")
        sys.exit(exc.returncode)
