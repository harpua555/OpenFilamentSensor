#!/usr/bin/env python3
"""
Rebuild firmware.elf with user-provided build metadata (date/time, version, board).

Usage (run from repo root):
  python tools/rebuild_elf.py --env esp32s3 --firmware-label v0.8.2-pre1 \
      --build-date "Dec 28 2025" --build-time "12:03:00"
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from datetime import datetime
from typing import Optional

from board_config import compose_chip_family_label, validate_board_environment


def _format_build_date(value: str) -> str:
    """Accept 'Nov 27 2025' or '2025-11-27' and return 'Nov 27 2025'."""
    value = value.strip()
    for fmt in ("%b %d %Y", "%Y-%m-%d"):
        try:
            return datetime.strptime(value, fmt).strftime("%b %d %Y")
        except ValueError:
            continue
    raise ValueError("build-date must be like 'Nov 27 2025' or '2025-11-27'")


def _format_build_time(value: str) -> str:
    """Accept 'HH:MM:SS' or 'HH:MM' and return 'HH:MM:SS'."""
    value = value.strip()
    for fmt in ("%H:%M:%S", "%H:%M"):
        try:
            parsed = datetime.strptime(value, fmt)
            return parsed.strftime("%H:%M:%S")
        except ValueError:
            continue
    raise ValueError("build-time must be like '14:30:45' or '14:30'")


def _split_build_datetime(value: str) -> tuple[str, str]:
    """Parse ISO-like datetime and return (build_date, build_time)."""
    parsed = datetime.fromisoformat(value.strip().replace("Z", "+00:00"))
    return parsed.strftime("%b %d %Y"), parsed.strftime("%H:%M:%S")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Rebuild firmware.elf with custom build metadata."
    )
    parser.add_argument(
        "--env",
        default="esp32s3",
        help="PlatformIO environment to use (default: esp32s3)",
    )
    parser.add_argument(
        "--chip",
        default="ESP32",
        help="Chip prefix to combine with the board suffix (default: ESP32).",
    )
    parser.add_argument(
        "--chip-family",
        default="",
        help="Override CHIP_FAMILY label (default: derived from --chip and --env).",
    )
    parser.add_argument(
        "--firmware-label",
        default="alpha",
        help="Firmware version string to embed (default: alpha).",
    )
    parser.add_argument(
        "--build-date",
        default="",
        help="Build date like 'Nov 27 2025' or '2025-11-27'.",
    )
    parser.add_argument(
        "--build-time",
        default="",
        help="Build time like '14:30:45' or '14:30'.",
    )
    parser.add_argument(
        "--build-datetime",
        default="",
        help="ISO datetime like '2025-12-28T12:03:00' (overrides date/time).",
    )
    args = parser.parse_args()

    if not validate_board_environment(args.env):
        print(f"ERROR: Unsupported board environment '{args.env}'")
        sys.exit(1)

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    chip_family = args.chip_family.strip() or compose_chip_family_label(args.env, args.chip)
    firmware_label = (args.firmware_label or "alpha").strip()

    build_date: Optional[str] = None
    build_time: Optional[str] = None
    if args.build_datetime:
        try:
            build_date, build_time = _split_build_datetime(args.build_datetime)
        except ValueError as exc:
            print(f"ERROR: {exc}")
            sys.exit(1)
    else:
        if args.build_date:
            try:
                build_date = _format_build_date(args.build_date)
            except ValueError as exc:
                print(f"ERROR: {exc}")
                sys.exit(1)
        if args.build_time:
            try:
                build_time = _format_build_time(args.build_time)
            except ValueError as exc:
                print(f"ERROR: {exc}")
                sys.exit(1)

    pio_cmd = shutil.which("pio") or shutil.which("platformio")
    if not pio_cmd:
        print(
            "ERROR: Neither `pio` nor `platformio` is on PATH.\n"
            "Install PlatformIO Core (pip install platformio) "
            "and/or add its Scripts directory to PATH."
        )
        sys.exit(1)

    env = os.environ.copy()
    env["CHIP_FAMILY"] = chip_family
    env["FIRMWARE_VERSION"] = firmware_label
    if build_date:
        env["BUILD_DATE_OVERRIDE"] = build_date
    if build_time:
        env["BUILD_TIME_OVERRIDE"] = build_time

    print(f"Rebuilding ELF for env '{args.env}'")
    print(f"CHIP_FAMILY='{chip_family}'")
    print(f"FIRMWARE_VERSION='{firmware_label}'")
    if build_date or build_time:
        print(f"BUILD_DATE='{build_date or '(default)'}'")
        print(f"BUILD_TIME='{build_time or '(default)'}'")
    else:
        print("BUILD_DATE/BUILD_TIME: using current time")

    cmd = [pio_cmd, "run", "-e", args.env]
    print(f"> {' '.join(cmd)} (cwd={repo_root})")
    subprocess.run(cmd, cwd=repo_root, check=True, env=env)

    elf_path = os.path.join(repo_root, ".pio", "build", args.env, "firmware.elf")
    print(f"ELF output: {elf_path}")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        print(f"\nCommand failed with exit code {exc.returncode}: {exc.cmd}")
        sys.exit(exc.returncode)
