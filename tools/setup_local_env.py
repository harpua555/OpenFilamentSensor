#!/usr/bin/env python3
"""
Create a portable, self-contained build environment for ESP32 firmware.

This script creates:
- Python virtual environment in tools/.venv/
- PlatformIO installation in the venv (not global)
- ESP32 platform toolchain in tools/.platformio/ (not ~/.platformio)

Everything is self-contained and can be deleted to clean up completely.

Usage:
    python tools/setup_local_env.py
"""

import os
import subprocess
import sys
import venv
from pathlib import Path


def main():
    # Paths
    tools_dir = Path(__file__).parent
    repo_root = tools_dir.parent
    venv_dir = tools_dir / ".venv"
    pio_home = tools_dir / ".platformio"

    print("=" * 60)
    print("Setting up portable build environment")
    print("=" * 60)
    print(f"Virtual environment: {venv_dir}")
    print(f"PlatformIO home:     {pio_home}")
    print()
    print("This will NOT modify your global Python or PlatformIO installations.")
    print()

    # Step 1: Create Python virtual environment
    if not venv_dir.exists():
        print("[1/4] Creating Python virtual environment...")
        try:
            venv.create(venv_dir, with_pip=True)
            print("      ✓ Virtual environment created")
        except Exception as e:
            print(f"      ✗ Failed to create virtual environment: {e}")
            sys.exit(1)
    else:
        print("[1/4] Virtual environment already exists")

    # Determine venv executables
    if os.name == "nt":  # Windows
        venv_python = venv_dir / "Scripts" / "python.exe"
        venv_pip = venv_dir / "Scripts" / "pip.exe"
    else:  # Linux/Mac
        venv_python = venv_dir / "bin" / "python"
        venv_pip = venv_dir / "bin" / "pip"

    if not venv_python.exists():
        print(f"      ✗ Virtual environment seems incomplete (missing {venv_python})")
        print(f"      Try deleting {venv_dir} and running this script again")
        sys.exit(1)

    # Step 2: Upgrade pip
    print("[2/4] Upgrading pip in virtual environment...")
    try:
        subprocess.run(
            [str(venv_python), "-m", "pip", "install", "--upgrade", "pip"],
            check=True,
            capture_output=True,
        )
        print("      ✓ pip upgraded")
    except subprocess.CalledProcessError as e:
        print(f"      ✗ Failed to upgrade pip: {e}")
        print(e.stderr.decode())
        sys.exit(1)

    # Step 3: Install PlatformIO in venv
    print("[3/4] Installing PlatformIO in virtual environment...")
    try:
        result = subprocess.run(
            [str(venv_pip), "install", "platformio"],
            check=True,
            capture_output=True,
            text=True,
        )
        if "already satisfied" in result.stdout.lower():
            print("      ✓ PlatformIO already installed")
        else:
            print("      ✓ PlatformIO installed")
    except subprocess.CalledProcessError as e:
        print(f"      ✗ Failed to install PlatformIO: {e}")
        print(e.stderr)
        sys.exit(1)

    # Step 4: Install ESP32 platform in local .platformio directory
    print("[4/4] Installing ESP32 platform (this may take a few minutes)...")
    env = os.environ.copy()
    env["PLATFORMIO_CORE_DIR"] = str(pio_home)

    # Determine pio executable in venv
    if os.name == "nt":
        pio_cmd = venv_dir / "Scripts" / "pio.exe"
    else:
        pio_cmd = venv_dir / "bin" / "pio"

    if not pio_cmd.exists():
        print(f"      ✗ PlatformIO executable not found at {pio_cmd}")
        sys.exit(1)

    try:
        # Check if platform is already installed
        check_result = subprocess.run(
            [str(pio_cmd), "platform", "show", "espressif32"],
            env=env,
            capture_output=True,
            text=True,
        )

        if check_result.returncode == 0:
            print("      ✓ ESP32 platform already installed")
        else:
            # Platform not installed, install it
            subprocess.run(
                [str(pio_cmd), "platform", "install", "espressif32"],
                env=env,
                check=True,
            )
            print("      ✓ ESP32 platform installed")
    except subprocess.CalledProcessError as e:
        print(f"      ✗ Failed to install ESP32 platform: {e}")
        sys.exit(1)

    print()
    print("=" * 60)
    print("✅ Portable environment ready!")
    print("=" * 60)
    print()
    print("To build firmware using this environment:")
    print(f"  python tools/build_local.py")
    print()
    print("To specify a board:")
    print(f"  python tools/build_local.py --env seeed_esp32c3")
    print()
    print("To clean up everything:")
    if os.name == "nt":
        print(f'  rmdir /s /q "{venv_dir}" "{pio_home}"')
    else:
        print(f'  rm -rf "{venv_dir}" "{pio_home}"')
    print()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
        sys.exit(1)
    except Exception as e:
        print(f"\n\nUnexpected error: {e}")
        import traceback

        traceback.print_exc()
        sys.exit(1)
