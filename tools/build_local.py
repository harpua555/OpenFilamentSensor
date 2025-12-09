#!/usr/bin/env python3
"""
Build firmware using the portable local environment.

This wrapper runs build_and_flash.py using the isolated Python virtual environment
and PlatformIO installation created by setup_local_env.py.

Usage:
    python tools/build_local.py [OPTIONS]

All options are passed through to build_and_flash.py:
    --env ENV            PlatformIO environment (e.g., esp32s3, seeed_esp32c3)
    --local              Build only (don't upload to hardware)
    --ignore-secrets     Don't merge secrets files
    --build-mode MODE    'nofs' = firmware only (no merge), 'nobin' = filesystem only (no merge), default = full build with merge
"""

import os
import subprocess
import sys
from pathlib import Path


def main():
    # Paths
    tools_dir = Path(__file__).parent
    repo_root = tools_dir.parent
    venv_dir = tools_dir / ".venv"
    pio_home = tools_dir / ".platformio"

    # Check if portable environment exists
    if not venv_dir.exists():
        print("ERROR: Portable environment not set up.")
        print()
        print("Please run the setup script first:")
        print("  python tools/setup_local_env.py")
        print()
        sys.exit(1)

    # Determine venv Python executable
    if os.name == "nt":  # Windows
        venv_python = venv_dir / "Scripts" / "python.exe"
    else:  # Linux/Mac
        venv_python = venv_dir / "bin" / "python"

    if not venv_python.exists():
        print(f"ERROR: Python executable not found in virtual environment")
        print(f"Expected: {venv_python}")
        print()
        print("Try re-creating the environment:")
        if os.name == "nt":
            print(f'  rmdir /s /q "{venv_dir}" "{pio_home}"')
        else:
            print(f'  rm -rf "{venv_dir}" "{pio_home}"')
        print("  python tools/setup_local_env.py")
        print()
        sys.exit(1)

    # Set environment variable to use local PlatformIO home
    env = os.environ.copy()
    env["PLATFORMIO_CORE_DIR"] = str(pio_home)

    # Run build_and_flash.py with venv Python
    build_script = tools_dir / "build_and_flash.py"

    print(f"Using portable environment:")
    print(f"  Python:         {venv_python}")
    print(f"  PlatformIO:     {pio_home}")
    print()

    try:
        result = subprocess.run(
            [str(venv_python), str(build_script)] + sys.argv[1:], env=env
        )
        sys.exit(result.returncode)
    except KeyboardInterrupt:
        print("\n\nBuild interrupted by user")
        sys.exit(1)
    except Exception as e:
        print(f"\n\nFailed to run build script: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
