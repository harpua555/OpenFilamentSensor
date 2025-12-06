#!/usr/bin/env python3
"""
tools-launcher.py
Cross-platform TUI-style menu for running Centauri motion-detector tools.
Works on Windows, Linux, and WSL.
"""

import os
import sys
import subprocess
from pathlib import Path

# =========================
# 0) CONFIG
# =========================

BASE_DIR = Path(__file__).parent.resolve()

# Scripts (relative to BASE_DIR)
SCRIPTS = {
    "build": BASE_DIR / "build_and_flash.py",
    "build_local": BASE_DIR / "build_local.py",
    "build_release": BASE_DIR / "build_and_release.py",
    "elegoo_status": BASE_DIR / "elegoo_status_cli.py",
    "extract_log": BASE_DIR / "extract_log_data.py",
    "capture_logs": BASE_DIR / "capture_logs.py",
    "stream_logs": BASE_DIR / "stream_logs.py",
    "test_build_sh": BASE_DIR.parent / "test" / "build_tests.sh",
    "test_build_bat": BASE_DIR.parent / "test" / "build_tests.bat",
}

# Detect platform
IS_WINDOWS = sys.platform == "win32"
IS_WSL = "microsoft" in os.uname().release.lower() if hasattr(os, "uname") else False

# =========================
# 1) STATE (CURRENT SETTINGS)
# =========================

class Settings:
    # Build settings
    build_env = "esp32s3"
    build_local = False
    build_ignore_secrets = False
    build_mode = "full"  # full, nofs, nobin

    # Release settings
    release_env = "esp32s3"
    release_version = "ver"  # skip, build, ver, release

    # Capture logs settings
    capture_logs_ip = "192.168.0.153"

    # Elegoo status settings
    elegoo_ip = "192.168.0.153"
    elegoo_timeout = 5

    # Extract log settings
    log_file = "log.txt"
    output_file = "extracted_data.csv"


BUILD_ENV_OPTIONS = [
    "esp32s3",
    "esp32c3",
    "esp32",
    "seeed_esp32s3",
    "seeed_esp32c3",
]

RELEASE_ENV_OPTIONS = BUILD_ENV_OPTIONS + ["all"]

BUILD_MODE_OPTIONS = {
    "1": ("full (firmware + filesystem)", "full"),
    "2": ("nofs (firmware only)", "nofs"),
    "3": ("nobin (filesystem only)", "nobin"),
}

VERSION_OPTIONS = {
    "1": ("skip (no version update)", "skip"),
    "2": ("build (build++)", "build"),
    "3": ("ver (minor++, reset build)", "ver"),
    "4": ("release (major++, reset minor/build)", "release"),
}

# =========================
# 2) HELPERS
# =========================

def clear_screen():
    os.system("cls" if IS_WINDOWS else "clear")


def wait_for_enter():
    input("\nPress Enter to return to menu...")


def check_script(path: Path) -> bool:
    if not path.exists():
        print(f"\033[93mWarning: Script not found: {path}\033[0m")
        wait_for_enter()
        return False
    return True


def run_python_script(script: Path, args: list = None):
    """Run a Python script with optional arguments."""
    args = args or []
    cmd = [sys.executable, str(script)] + args
    print(f"\033[96mCommand: {' '.join(cmd)}\033[0m")
    print("=" * 50)
    print()
    subprocess.run(cmd)


def run_shell_script(script: Path, shell: str = "bash"):
    """Run a shell script."""
    cmd = [shell, str(script)]
    print(f"\033[96mCommand: {' '.join(cmd)}\033[0m")
    print("=" * 50)
    print()
    subprocess.run(cmd)


def run_powershell_script(script: Path, args: list = None):
    """Run a PowerShell script (for Windows or WSL with powershell.exe)."""
    args = args or []
    if IS_WINDOWS:
        cmd = ["powershell.exe", "-NoLogo", "-ExecutionPolicy", "Bypass", "-File", str(script)] + args
    else:
        # In WSL, try powershell.exe if available
        cmd = ["powershell.exe", "-NoLogo", "-ExecutionPolicy", "Bypass", "-File", str(script)] + args
    print(f"\033[96mCommand: {' '.join(cmd)}\033[0m")
    print("=" * 50)
    print()
    subprocess.run(cmd)


# =========================
# 3) BUILD & FLASH MENU
# =========================

def show_build_menu():
    while True:
        clear_screen()
        print("===============================")
        print("  Build & Flash Firmware")
        print("===============================")
        print()
        print(f" Script: {SCRIPTS['build']}")
        print()
        print(f" [1] Env              : {Settings.build_env}")
        print(f" [2] --local          : {'ON' if Settings.build_local else 'OFF'}")
        print(f" [3] --ignore-secrets : {'ON' if Settings.build_ignore_secrets else 'OFF'}")
        mode_label = {
            "full": "full (firmware + filesystem)",
            "nofs": "nofs (firmware only)",
            "nobin": "nobin (filesystem only)",
        }.get(Settings.build_mode, Settings.build_mode)
        print(f" [4] Build mode       : {mode_label}")
        print()
        print(" [R] Run with above settings")
        print(" [B] Back to main menu")
        print()

        choice = input("Select an option: ").strip().lower()

        if choice == "b":
            return
        elif choice == "r":
            invoke_build_command()
        elif choice == "1":
            change_build_env()
        elif choice == "2":
            Settings.build_local = not Settings.build_local
        elif choice == "3":
            Settings.build_ignore_secrets = not Settings.build_ignore_secrets
        elif choice == "4":
            change_build_mode()
        else:
            print("\033[91mInvalid option.\033[0m")
            import time; time.sleep(1)


def change_build_env():
    while True:
        clear_screen()
        print("Select PlatformIO env:")
        print()
        for i, env in enumerate(BUILD_ENV_OPTIONS, 1):
            print(f" [{i}] {env}")
        print(f" [{len(BUILD_ENV_OPTIONS) + 1}] <enter manually>")
        print(" [B] Back (no change)")
        print()
        print(f"Current env: {Settings.build_env}")

        choice = input("Choose env: ").strip().lower()

        if choice == "b":
            return

        try:
            idx = int(choice) - 1
            if 0 <= idx < len(BUILD_ENV_OPTIONS):
                Settings.build_env = BUILD_ENV_OPTIONS[idx]
                return
            elif idx == len(BUILD_ENV_OPTIONS):
                manual = input("Enter env name manually: ").strip()
                if manual:
                    Settings.build_env = manual
                return
        except ValueError:
            pass

        print("\033[91mInvalid option.\033[0m")
        import time; time.sleep(1)


def change_build_mode():
    while True:
        clear_screen()
        print("Select build mode:")
        print()
        for key, (label, _) in sorted(BUILD_MODE_OPTIONS.items()):
            print(f" [{key}] {label}")
        print(" [B] Back (no change)")
        print()
        mode_label = {
            "full": "full (firmware + filesystem)",
            "nofs": "nofs (firmware only)",
            "nobin": "nobin (filesystem only)",
        }.get(Settings.build_mode, Settings.build_mode)
        print(f"Current mode: {mode_label}")

        choice = input("Choose mode: ").strip()

        if choice.lower() == "b":
            return
        elif choice in BUILD_MODE_OPTIONS:
            Settings.build_mode = BUILD_MODE_OPTIONS[choice][1]
            return
        else:
            print("\033[91mInvalid option.\033[0m")
            import time; time.sleep(1)


def invoke_build_command():
    if not check_script(SCRIPTS["build"]):
        return

    args = []
    if Settings.build_env:
        args.extend(["--env", Settings.build_env])
    if Settings.build_local:
        args.append("--local")
    if Settings.build_ignore_secrets:
        args.append("--ignore-secrets")
    if Settings.build_mode and Settings.build_mode != "full":
        args.extend(["--build-mode", Settings.build_mode])

    clear_screen()
    print("\033[96mRunning build_and_flash.py with:\033[0m")
    print(f"  Env              : {Settings.build_env}")
    print(f"  --local          : {Settings.build_local}")
    print(f"  --ignore-secrets : {Settings.build_ignore_secrets}")
    print(f"  Build mode       : {Settings.build_mode}")
    print()

    run_python_script(SCRIPTS["build"], args)
    wait_for_enter()


def invoke_build_local_command():
    if not check_script(SCRIPTS["build_local"]):
        return

    clear_screen()
    print("\033[96mRunning build_local.py...\033[0m")
    print()
    run_python_script(SCRIPTS["build_local"])
    wait_for_enter()


# =========================
# 3b) BUILD & RELEASE MENU
# =========================

def show_release_menu():
    while True:
        clear_screen()
        print("===============================")
        print("  Build & Release Firmware")
        print("===============================")
        print()
        print(f" Script: {SCRIPTS['build_release']}")
        print()
        print(f" [1] Env              : {Settings.release_env}")
        version_label = {
            "skip": "skip (no version update)",
            "build": "build (build++)",
            "ver": "ver (minor++, reset build)",
            "release": "release (major++, reset minor/build)",
        }.get(Settings.release_version, Settings.release_version)
        print(f" [2] --version        : {version_label}")
        print()
        print(" [R] Run with above settings")
        print(" [B] Back to main menu")
        print()

        choice = input("Select an option: ").strip().lower()

        if choice == "b":
            return
        elif choice == "r":
            invoke_release_command()
        elif choice == "1":
            change_release_env()
        elif choice == "2":
            change_release_version()
        else:
            print("\033[91mInvalid option.\033[0m")
            import time; time.sleep(1)


def change_release_env():
    while True:
        clear_screen()
        print("Select PlatformIO env for release:")
        print()
        for i, env in enumerate(RELEASE_ENV_OPTIONS, 1):
            print(f" [{i}] {env}")
        print(f" [{len(RELEASE_ENV_OPTIONS) + 1}] <enter manually>")
        print(" [B] Back (no change)")
        print()
        print(f"Current env: {Settings.release_env}")

        choice = input("Choose env: ").strip().lower()

        if choice == "b":
            return

        try:
            idx = int(choice) - 1
            if 0 <= idx < len(RELEASE_ENV_OPTIONS):
                Settings.release_env = RELEASE_ENV_OPTIONS[idx]
                return
            elif idx == len(RELEASE_ENV_OPTIONS):
                manual = input("Enter env name manually: ").strip()
                if manual:
                    Settings.release_env = manual
                return
        except ValueError:
            pass

        print("\033[91mInvalid option.\033[0m")
        import time; time.sleep(1)


def change_release_version():
    while True:
        clear_screen()
        print("Select version increment type:")
        print()
        for key, (label, _) in sorted(VERSION_OPTIONS.items()):
            print(f" [{key}] {label}")
        print(" [B] Back (no change)")
        print()
        version_label = {
            "skip": "skip (no version update)",
            "build": "build (build++)",
            "ver": "ver (minor++, reset build)",
            "release": "release (major++, reset minor/build)",
        }.get(Settings.release_version, Settings.release_version)
        print(f"Current version action: {version_label}")

        choice = input("Choose version action: ").strip()

        if choice.lower() == "b":
            return
        elif choice in VERSION_OPTIONS:
            Settings.release_version = VERSION_OPTIONS[choice][1]
            return
        else:
            print("\033[91mInvalid option.\033[0m")
            import time; time.sleep(1)


def invoke_release_command():
    if not check_script(SCRIPTS["build_release"]):
        return

    args = []
    if Settings.release_env:
        args.extend(["--env", Settings.release_env])
    if Settings.release_version:
        args.extend(["--version", Settings.release_version])

    clear_screen()
    print("\033[96mRunning build_and_release.py with:\033[0m")
    print(f"  Env              : {Settings.release_env}")
    print(f"  --version        : {Settings.release_version}")
    print()

    run_python_script(SCRIPTS["build_release"], args)
    wait_for_enter()


# =========================
# 4) CAPTURE LOGS MENU
# =========================

def show_capture_menu():
    while True:
        clear_screen()
        print("===============================")
        print("  Capture Logs")
        print("===============================")
        print()
        print(f" Script : {SCRIPTS['capture_logs']}")
        print(f" IP     : {Settings.capture_logs_ip}")
        print()
        print(" [1] Change IP")
        print(" [R] Run capture_logs.py")
        print(" [B] Back to main menu")
        print()

        choice = input("Select an option: ").strip().lower()

        if choice == "b":
            return
        elif choice == "r":
            invoke_capture_command()
        elif choice == "1":
            ip = input("Enter IP (blank to keep current): ").strip()
            if ip:
                Settings.capture_logs_ip = ip
        else:
            print("\033[91mInvalid option.\033[0m")
            import time; time.sleep(1)


def invoke_capture_command():
    if not check_script(SCRIPTS["capture_logs"]):
        return

    clear_screen()
    print("\033[96mRunning capture_logs.py with:\033[0m")
    print(f"  IP: {Settings.capture_logs_ip}")
    print()

    run_python_script(SCRIPTS["capture_logs"], [Settings.capture_logs_ip])
    wait_for_enter()


# =========================
# 5) STREAM LOGS MENU
# =========================

def show_stream_menu():
    while True:
        clear_screen()
        print("===============================")
        print("  Stream Logs")
        print("===============================")
        print()
        print(f" Script : {SCRIPTS['stream_logs']}")
        print()
        print(" [R] Run stream_logs.py")
        print(" [B] Back to main menu")
        print()

        choice = input("Select an option: ").strip().lower()

        if choice == "b":
            return
        elif choice == "r":
            invoke_stream_command()
        else:
            print("\033[91mInvalid option.\033[0m")
            import time; time.sleep(1)


def invoke_stream_command():
    if not check_script(SCRIPTS["stream_logs"]):
        return

    clear_screen()
    print("\033[96mRunning stream_logs.py...\033[0m")
    print()
    run_python_script(SCRIPTS["stream_logs"])
    wait_for_enter()


# =========================
# 6) DEVELOPMENT TOOLS MENU
# =========================

def show_development_menu():
    while True:
        clear_screen()
        print("===============================")
        print("  Development & Analysis Tools")
        print("===============================")
        print()
        print(" [1] Elegoo Status CLI (elegoo_status_cli.py)")
        print(" [2] Extract Log Data (extract_log_data.py)")
        print(" [B] Back to main menu")
        print()

        choice = input("Select an option: ").strip().lower()

        if choice == "b":
            return
        elif choice == "1":
            show_elegoo_status_menu()
        elif choice == "2":
            show_extract_log_menu()
        else:
            print("\033[91mInvalid option.\033[0m")
            import time; time.sleep(1)


def show_elegoo_status_menu():
    while True:
        clear_screen()
        print("===============================")
        print("  Elegoo Status CLI")
        print("===============================")
        print()
        print(f" [1] IP Address    : {Settings.elegoo_ip}")
        print(f" [2] Timeout (s)   : {Settings.elegoo_timeout}")
        print()
        print(" [R] Run with above settings")
        print(" [B] Back to development menu")
        print()

        choice = input("Select an option: ").strip().lower()

        if choice == "b":
            return
        elif choice == "r":
            invoke_elegoo_status_command()
        elif choice == "1":
            ip = input("Enter IP address (blank to keep current): ").strip()
            if ip:
                Settings.elegoo_ip = ip
        elif choice == "2":
            timeout = input("Enter timeout in seconds (blank to keep current): ").strip()
            if timeout.isdigit():
                Settings.elegoo_timeout = int(timeout)
        else:
            print("\033[91mInvalid option.\033[0m")
            import time; time.sleep(1)


def invoke_elegoo_status_command():
    if not check_script(SCRIPTS["elegoo_status"]):
        return

    args = [Settings.elegoo_ip, "--timeout", str(Settings.elegoo_timeout)]

    clear_screen()
    print("\033[96mRunning elegoo_status_cli.py with:\033[0m")
    print(f"  IP Address : {Settings.elegoo_ip}")
    print(f"  Timeout (s): {Settings.elegoo_timeout}")
    print()

    run_python_script(SCRIPTS["elegoo_status"], args)
    wait_for_enter()


def show_extract_log_menu():
    while True:
        clear_screen()
        print("===============================")
        print("  Extract Log Data")
        print("===============================")
        print()
        print(f" [1] Log File     : {Settings.log_file}")
        print(f" [2] Output File  : {Settings.output_file}")
        print()
        print(" [R] Run with above settings")
        print(" [B] Back to development menu")
        print()

        choice = input("Select an option: ").strip().lower()

        if choice == "b":
            return
        elif choice == "r":
            invoke_extract_log_command()
        elif choice == "1":
            file = input("Enter log file path (blank to keep current): ").strip()
            if file:
                Settings.log_file = file
        elif choice == "2":
            file = input("Enter output file path (blank to keep current): ").strip()
            if file:
                Settings.output_file = file
        else:
            print("\033[91mInvalid option.\033[0m")
            import time; time.sleep(1)


def invoke_extract_log_command():
    if not check_script(SCRIPTS["extract_log"]):
        return

    args = [Settings.log_file, "--output", Settings.output_file]

    clear_screen()
    print("\033[96mRunning extract_log_data.py with:\033[0m")
    print(f"  Log File    : {Settings.log_file}")
    print(f"  Output File : {Settings.output_file}")
    print()

    run_python_script(SCRIPTS["extract_log"], args)
    wait_for_enter()


# =========================
# 7) TESTING MENU
# =========================

def show_testing_menu():
    while True:
        clear_screen()
        print("===============================")
        print("  Testing Tools")
        print("===============================")
        print()
        print(" [1] Run Build Tests (bash)")
        if IS_WINDOWS:
            print(" [2] Run Build Tests (Windows batch)")
        print(" [B] Back to main menu")
        print()

        choice = input("Select an option: ").strip().lower()

        if choice == "b":
            return
        elif choice == "1":
            invoke_test_build_bash()
        elif choice == "2" and IS_WINDOWS:
            invoke_test_build_windows()
        else:
            print("\033[91mInvalid option.\033[0m")
            import time; time.sleep(1)


def invoke_test_build_bash():
    if not check_script(SCRIPTS["test_build_sh"]):
        return

    clear_screen()
    print("\033[96mRunning build tests (bash)...\033[0m")
    print()
    run_shell_script(SCRIPTS["test_build_sh"], "bash")
    wait_for_enter()


def invoke_test_build_windows():
    if not check_script(SCRIPTS["test_build_bat"]):
        return

    clear_screen()
    print("\033[96mRunning build tests (Windows)...\033[0m")
    print()
    cmd = [str(SCRIPTS["test_build_bat"])]
    print(f"\033[96mCommand: {' '.join(cmd)}\033[0m")
    print("=" * 50)
    print()
    subprocess.run(cmd, shell=True)
    wait_for_enter()


# =========================
# 8) MAIN MENU
# =========================

def show_main_menu():
    while True:
        clear_screen()
        print("=======================================")
        print("  Centauri Motion Detector Tools Menu")
        print("=======================================")
        print()
        platform_info = "Windows" if IS_WINDOWS else ("WSL" if IS_WSL else "Linux")
        print(f" Platform: {platform_info}")
        print(f" Base dir: {BASE_DIR}")
        print()
        print(" Build & Flash Tools:")
        print(" [1] Build & Flash Firmware (build_and_flash.py)")
        print(" [2] Local Build (build_local.py)")
        print(" [3] Build & Release (build_and_release.py)")
        print()
        print(" Development & Analysis:")
        print(" [4] Development & Analysis Tools")
        print()
        print(" Log Management:")
        print(" [5] Capture Logs (capture_logs.py)")
        print(" [6] Stream Logs (stream_logs.py)")
        print()
        print(" Testing:")
        print(" [7] Testing Tools")
        print()
        print(" [Q] Quit")
        print()

        choice = input("Select an option: ").strip().lower()

        if choice == "q":
            print("Goodbye!")
            return
        elif choice == "1":
            show_build_menu()
        elif choice == "2":
            invoke_build_local_command()
        elif choice == "3":
            show_release_menu()
        elif choice == "4":
            show_development_menu()
        elif choice == "5":
            show_capture_menu()
        elif choice == "6":
            show_stream_menu()
        elif choice == "7":
            show_testing_menu()
        else:
            print("\033[91mInvalid option.\033[0m")
            import time; time.sleep(1)


# =========================
# 9) ENTRY POINT
# =========================

if __name__ == "__main__":
    try:
        show_main_menu()
    except KeyboardInterrupt:
        print("\n\nInterrupted. Goodbye!")
        sys.exit(0)
