import subprocess
import os
import sys
import argparse
from datetime import datetime

def analyze():
    # 1. Setup Argument Parser
    parser = argparse.ArgumentParser(description="Decode ESP32 Coredump from WSL using Windows GDB")
    parser.add_argument("chip", help="Chip type (e.g., esp32s3, esp32c3)")
    parser.add_argument("--core", default=".coredump/coredump.bin", help="Path to coredump file")
    args = parser.parse_args()

    # 2. Define internal paths
    elf_path = f".pio/build/{args.chip}/firmware.elf"
    timestamp = datetime.now().strftime("%Y-%m-%d_%H%M")
    report_path = f".coredump/report_{args.chip}_{timestamp}.txt"

    # 3. Convert Linux paths to Windows paths using 'wslpath'
    try:
        def to_win(path):
            return subprocess.check_output(["wslpath", "-w", path]).decode().strip()

        win_core = to_win(args.core)
        win_elf = to_win(elf_path)
        win_report = to_win(report_path)
    except Exception as e:
        print(f"Error converting paths: {e}")
        return

    # 4. Construct the PowerShell Command
    # Note: We use the S3 toolchain for S3 chips, and the generic one for others.
    gdb_pkg = f"toolchain-xtensa-{args.chip}" if "s3" in args.chip else "toolchain-riscv32-esp"
    gdb_path = f"$env:USERPROFILE\\.platformio\\packages\\{gdb_pkg}\\bin\\xtensa-{args.chip}-elf-gdb.exe"
    
    if "c3" in args.chip: # C3 uses RISC-V debugger
        gdb_path = f"$env:USERPROFILE\\.platformio\\packages\\toolchain-riscv32-esp\\bin\\riscv32-esp-elf-gdb.exe"

    ps_cmd = (
        f"python.exe -m esp_coredump info_corefile "
        f"--gdb \"{gdb_path}\" --core \"{win_core}\" \"{win_elf}\" > \"{win_report}\""
    )

    # 5. Execute via powershell.exe
    print(f"--- Analyzing {args.chip} core dump ---")
    subprocess.run(["powershell.exe", "-Command", ps_cmd])
    print(f"Done! Report saved to: {report_path}")

if __name__ == "__main__":
    analyze()