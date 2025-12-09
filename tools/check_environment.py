#!/usr/bin/env python3
"""
Verify that all required build tools are installed and properly configured.

This script checks:
- Python version (3.10+)
- Node.js version (18+)
- npm availability
- PlatformIO installation
- ESP32 platform installation
- ESP32 toolchain integrity (detect corrupted FreeRTOS headers)

Usage:
    python tools/check_environment.py
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path


class Colors:
    """ANSI color codes (work on most terminals)"""

    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    RED = "\033[91m"
    BLUE = "\033[94m"
    RESET = "\033[0m"
    BOLD = "\033[1m"


def print_header(text):
    """Print a section header"""
    print(f"\n{Colors.BOLD}{Colors.BLUE}{text}{Colors.RESET}")
    print("=" * len(text))


def print_check(name, status, message=""):
    """Print a check result"""
    if status == "ok":
        icon = f"{Colors.GREEN}✓{Colors.RESET}"
        status_text = f"{Colors.GREEN}OK{Colors.RESET}"
    elif status == "warn":
        icon = f"{Colors.YELLOW}⚠{Colors.RESET}"
        status_text = f"{Colors.YELLOW}WARNING{Colors.RESET}"
    else:  # error
        icon = f"{Colors.RED}✗{Colors.RESET}"
        status_text = f"{Colors.RED}MISSING{Colors.RESET}"

    print(f"{icon} {name:30s} {status_text}")
    if message:
        print(f"  {message}")


def check_python():
    """Check Python version"""
    version = sys.version_info
    version_str = f"{version.major}.{version.minor}.{version.micro}"

    if version.major >= 3 and version.minor >= 10:
        print_check("Python", "ok", f"Version {version_str}")
        return True
    else:
        print_check(
            "Python",
            "error",
            f"Version {version_str} found, but 3.10+ required\n"
            "  Download: https://www.python.org/downloads/",
        )
        return False


def check_node():
    """Check Node.js version"""
    node_cmd = "node.exe" if os.name == "nt" else "node"
    node_path = shutil.which(node_cmd)

    if not node_path:
        print_check(
            "Node.js",
            "error",
            "Not found in PATH\n"
            "  Download: https://nodejs.org/ (LTS version recommended)",
        )
        return False

    try:
        result = subprocess.run(
            [node_cmd, "--version"], capture_output=True, text=True, check=True
        )
        version_str = result.stdout.strip().lstrip("v")
        major_version = int(version_str.split(".")[0])

        if major_version >= 18:
            print_check("Node.js", "ok", f"Version {version_str}")
            return True
        else:
            print_check(
                "Node.js",
                "warn",
                f"Version {version_str} found, but 18+ recommended\n"
                "  Download: https://nodejs.org/",
            )
            return True  # Still works, just warn
    except Exception as e:
        print_check("Node.js", "error", f"Error checking version: {e}")
        return False


def check_npm():
    """Check npm availability"""
    npm_cmd = "npm.cmd" if os.name == "nt" else "npm"
    npm_path = shutil.which(npm_cmd)

    if not npm_path:
        print_check(
            "npm",
            "error",
            "Not found in PATH\n"
            "  npm is usually installed with Node.js",
        )
        return False

    try:
        result = subprocess.run(
            [npm_cmd, "--version"], capture_output=True, text=True, check=True
        )
        version_str = result.stdout.strip()
        print_check("npm", "ok", f"Version {version_str}")
        return True
    except Exception as e:
        print_check("npm", "error", f"Error checking version: {e}")
        return False


def check_platformio():
    """Check PlatformIO installation"""
    pio_cmd = shutil.which("pio") or shutil.which("platformio")

    if not pio_cmd:
        print_check(
            "PlatformIO Core",
            "error",
            "Not found in PATH\n"
            "  Install: pip install platformio\n"
            "  Or use portable environment: python tools/setup_local_env.py",
        )
        return False, None

    try:
        result = subprocess.run(
            [pio_cmd, "--version"], capture_output=True, text=True, check=True
        )
        version_str = result.stdout.strip().split()[1]  # "PlatformIO Core, version X.Y.Z"
        print_check("PlatformIO Core", "ok", f"Version {version_str}")
        return True, pio_cmd
    except Exception as e:
        print_check("PlatformIO Core", "error", f"Error checking version: {e}")
        return False, None


def check_esp32_platform(pio_cmd):
    """Check if ESP32 platform is installed"""
    if not pio_cmd:
        print_check("ESP32 Platform", "error", "PlatformIO not available")
        return False

    try:
        result = subprocess.run(
            [pio_cmd, "platform", "show", "espressif32"],
            capture_output=True,
            text=True,
            check=True,
        )
        # Extract version from output
        for line in result.stdout.split("\n"):
            if "Version" in line or "version" in line:
                version_info = line.split()[-1]
                print_check("ESP32 Platform", "ok", f"Installed (version {version_info})")
                return True
        print_check("ESP32 Platform", "ok", "Installed")
        return True
    except subprocess.CalledProcessError:
        print_check(
            "ESP32 Platform",
            "error",
            "Not installed\n"
            "  Install: pio platform install espressif32\n"
            "  Or use portable environment: python tools/setup_local_env.py",
        )
        return False


def check_esp32_toolchain(pio_cmd):
    """Check ESP32 toolchain integrity by looking for FreeRTOS headers"""
    if not pio_cmd:
        print_check("ESP32 Toolchain", "error", "PlatformIO not available")
        return False

    # Determine PlatformIO home directory
    pio_home = os.environ.get("PLATFORMIO_CORE_DIR")
    if not pio_home:
        if os.name == "nt":
            pio_home = Path.home() / ".platformio"
        else:
            pio_home = Path.home() / ".platformio"
    else:
        pio_home = Path(pio_home)

    # Check for FreeRTOS header
    freertos_header = (
        pio_home
        / "packages"
        / "framework-arduinoespressif32"
        / "cores"
        / "esp32"
        / "freertos"
        / "FreeRTOS.h"
    )

    if freertos_header.exists():
        print_check("ESP32 Toolchain", "ok", "FreeRTOS headers found")
        return True
    else:
        # Toolchain might be there but corrupted
        framework_dir = pio_home / "packages" / "framework-arduinoespressif32"
        if framework_dir.exists():
            print_check(
                "ESP32 Toolchain",
                "error",
                "Framework installed but FreeRTOS headers missing (corrupted)\n"
                f"  Expected: {freertos_header}\n"
                "  Fix: pio platform uninstall espressif32 && pio platform install espressif32",
            )
        else:
            print_check(
                "ESP32 Toolchain",
                "error",
                "Framework not installed\n"
                "  Install: pio platform install espressif32",
            )
        return False


def main():
    """Run all environment checks"""
    print(f"{Colors.BOLD}Centauri Carbon Motion Detector - Environment Checker{Colors.RESET}")
    print("Verifying build dependencies...")

    all_ok = True

    # Core tools
    print_header("Core Tools")
    all_ok &= check_python()
    all_ok &= check_node()
    all_ok &= check_npm()

    # PlatformIO
    print_header("PlatformIO")
    pio_ok, pio_cmd = check_platformio()
    all_ok &= pio_ok

    if pio_ok:
        all_ok &= check_esp32_platform(pio_cmd)
        all_ok &= check_esp32_toolchain(pio_cmd)

    # Summary
    print()
    print("=" * 60)
    if all_ok:
        print(f"{Colors.GREEN}{Colors.BOLD}✓ All checks passed!{Colors.RESET}")
        print()
        print("You're ready to build firmware:")
        print("  python tools/build_and_flash.py")
        print()
        return 0
    else:
        print(f"{Colors.RED}{Colors.BOLD}✗ Some checks failed{Colors.RESET}")
        print()
        print("Fix the issues above, or use the portable environment:")
        print("  python tools/setup_local_env.py")
        print("  python tools/build_local.py")
        print()
        return 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
        sys.exit(1)
