#!/usr/bin/env python3
"""
Continuous log capture from ESP32 device.
Cross-platform Python version of capture_logs.ps1.

Logs will be written to: reporoot/logs/
"""

import argparse
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import requests
except ImportError:
    print("Error: 'requests' module not found. Install with: pip install requests")
    sys.exit(1)

# Determine repo root from script location (one level up from tools/)
SCRIPT_DIR = Path(__file__).parent.resolve()
REPO_ROOT = SCRIPT_DIR.parent
LOG_DIR = REPO_ROOT / "logs"

# ANSI color codes
GREEN = "\033[92m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
RED = "\033[91m"
GRAY = "\033[90m"
RESET = "\033[0m"


def main():
    parser = argparse.ArgumentParser(description="Capture logs from ESP32 device")
    parser.add_argument(
        "device_ip",
        nargs="?",
        default="192.168.0.153",
        help="IP address of the ESP32 device (default: 192.168.0.153)",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=1.0,
        help="Polling interval in seconds (default: 1.0)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        help="HTTP request timeout in seconds (default: 5.0)",
    )
    args = parser.parse_args()

    # Create logs directory if it doesn't exist
    LOG_DIR.mkdir(exist_ok=True)

    # Timestamped log file
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    output_file = LOG_DIR / f"esp32-crash-logs-{timestamp}.txt"

    endpoint = f"http://{args.device_ip}/api/logs_live"

    print(f"{GREEN}Starting log capture from {endpoint}{RESET}")
    print(f"{GREEN}Saving to: {output_file}{RESET}")
    print(f"{YELLOW}Press Ctrl+C to stop{RESET}")
    print()

    # Write header to file
    with open(output_file, "w", encoding="utf-8") as f:
        f.write(f"=== Log capture started at {datetime.now()} ===\n")

    iteration = 0
    last_seen_lines: list[str] = []

    try:
        while True:
            iteration += 1
            timestamp_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

            try:
                # Fetch logs from device
                response = requests.get(endpoint, timeout=args.timeout)
                response.raise_for_status()

                current_lines = [
                    line for line in response.text.split("\n") if line.strip()
                ]

                # Find new lines (lines we haven't seen before)
                new_lines: list[str] = []

                if not last_seen_lines:
                    # First iteration - capture everything
                    new_lines = current_lines
                else:
                    # Use the last actual log line as anchor
                    last_line = last_seen_lines[-1]
                    found_index = -1

                    # Search from the end for efficiency
                    for i in range(len(current_lines) - 1, -1, -1):
                        if current_lines[i] == last_line:
                            found_index = i
                            break

                    if found_index >= 0 and found_index < len(current_lines) - 1:
                        # Found the last line, capture everything after it
                        new_lines = current_lines[found_index + 1 :]
                    elif found_index == -1:
                        # Didn't find last line - log buffer may have wrapped
                        new_lines = current_lines
                        print(
                            f"{YELLOW}[{timestamp_str}] WARNING: Log buffer wrapped, capturing all entries{RESET}"
                        )
                    # If found_index is the last line, there are no new lines

                # Write new lines to file
                if new_lines:
                    with open(output_file, "a", encoding="utf-8") as f:
                        for line in new_lines:
                            f.write(line + "\n")
                    print(
                        f"{CYAN}[{timestamp_str}] Captured {len(new_lines)} new log entries (iteration {iteration}){RESET}"
                    )
                else:
                    print(
                        f"{GRAY}[{timestamp_str}] No new log entries (iteration {iteration}){RESET}"
                    )

                # Update last seen lines
                last_seen_lines = current_lines

            except requests.RequestException as e:
                error_msg = f"[{timestamp_str}] ERROR: {e}"
                print(f"{RED}{error_msg}{RESET}")
                with open(output_file, "a", encoding="utf-8") as f:
                    f.write(error_msg + "\n")

            time.sleep(args.interval)

    except KeyboardInterrupt:
        print(f"\n{YELLOW}Log capture stopped.{RESET}")
        print(f"{GREEN}Logs saved to: {output_file}{RESET}")


if __name__ == "__main__":
    main()
