#!/usr/bin/env python3
"""
Stream (tail) the most recent ESP32 crash log file.
Cross-platform Python version of stream_logs.ps1.

Finds the most recent log file in reporoot/logs/ and tails it.
"""

import sys
import time
from pathlib import Path

# Determine repo root from script location
SCRIPT_DIR = Path(__file__).parent.resolve()
REPO_ROOT = SCRIPT_DIR.parent
LOG_DIR = REPO_ROOT / "logs"

# ANSI color codes
GREEN = "\033[92m"
YELLOW = "\033[93m"
RED = "\033[91m"
RESET = "\033[0m"


def find_latest_log() -> Path | None:
    """Find the most recently modified log file."""
    if not LOG_DIR.exists():
        return None

    log_files = list(LOG_DIR.glob("esp32-crash-logs-*.txt"))
    if not log_files:
        return None

    # Sort by modification time, most recent first
    return max(log_files, key=lambda f: f.stat().st_mtime)


def tail_file(filepath: Path):
    """Tail a file, printing new content as it's added."""
    print(f"{GREEN}Tailing: {filepath}{RESET}")
    print(f"{YELLOW}Press Ctrl+C to stop{RESET}")
    print()

    # Start by reading the entire file
    with open(filepath, "r", encoding="utf-8", errors="replace") as f:
        # Print existing content
        content = f.read()
        if content:
            print(content, end="")

        # Now tail for new content
        try:
            while True:
                line = f.readline()
                if line:
                    print(line, end="")
                else:
                    time.sleep(0.1)
        except KeyboardInterrupt:
            print(f"\n{YELLOW}Stopped.{RESET}")


def main():
    if not LOG_DIR.exists():
        print(f"{RED}Log directory not found: {LOG_DIR}{RESET}")
        sys.exit(1)

    latest = find_latest_log()

    if not latest:
        print(
            f"{RED}No log files found in {LOG_DIR} matching esp32-crash-logs-*.txt{RESET}"
        )
        sys.exit(1)

    tail_file(latest)


if __name__ == "__main__":
    main()
