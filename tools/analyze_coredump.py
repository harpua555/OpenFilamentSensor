#!/usr/bin/env python3
"""
Decode ESP32 coredump files using Windows GDB from WSL environment.
Relocatable script that anchors paths to the repository root.
"""
import subprocess
import os
import argparse
from datetime import datetime

def analyze():
    # 1. Handle Script Location and Paths
    # Finds the 'reporoot' by looking one level up from the 'tools' folder
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, ".."))
    
    # 2. Setup Argument Parser
    parser = argparse.ArgumentParser(description="Decode ESP32 Coredump from WSL using Windows GDB")
    # Chip now defaults to esp32s3
    parser.add_argument("chip", nargs="?", default="esp32s3", help="Chip type (default: esp32s3)")
    # Core now defaults to .coredump/coredump.bin relative to repo root
    parser.add_argument("--core", default=os.path.join(repo_root, ".coredump/coredump.bin"), 
                        help="Path to coredump file")
    args = parser.parse_args()

    # 3. Define internal paths relative to repo root
    elf_path = os.path.join(repo_root, f".pio/build/{args.chip}/firmware.elf")
    timestamp = datetime.now().strftime("%Y-%m-%d_%H%M")
    
    # Analysis output folder
    analysis_dir = os.path.join(repo_root, ".coredump/analysis")
    os.makedirs(analysis_dir, exist_ok=True)
    
    report_path = os.path.join(analysis_dir, f"report_{args.chip}_{timestamp}.txt")

    # 4. Convert Linux paths to Windows paths using 'wslpath'
    try:
        def to_win(path):
            # Resolve to absolute path before converting to ensure Windows can find it
            abs_path = os.path.abspath(path)
            return subprocess.check_output(["wslpath", "-w", abs_path]).decode().strip()

        win_core = to_win(args.core)
        win_elf = to_win(elf_path)
        win_report = to_win(report_path)
    except Exception as e:
        print(f"Error converting paths: {e}")
        return

    # 5. Construct the PowerShell Command
    # Map GDB package names based on chip architecture
    if "c3" in args.chip or "c6" in args.chip: # RISC-V Chips
        gdb_pkg = "toolchain-riscv32-esp"
        gdb_exe = "riscv32-esp-elf-gdb.exe"
    else: # Xtensa Chips (ESP32, S2, S3)
        gdb_pkg = f"toolchain-xtensa-{args.chip}"
        gdb_exe = f"xtensa-{args.chip}-elf-gdb.exe"

    gdb_path = f"$env:USERPROFILE\\.platformio\\packages\\{gdb_pkg}\\bin\\{gdb_exe}"

    ps_cmd = (
        f"python.exe -m esp_coredump info_corefile "
        f"--gdb \"{gdb_path}\" --core \"{win_core}\" \"{win_elf}\" > \"{win_report}\""
    )

    # 6. Execute via powershell.exe
    print(f"--- Analyzing {args.chip} core dump ---")
    print(f"Target ELF: {elf_path}")
    
    # Run the process
    result = subprocess.run(["powershell.exe", "-Command", ps_cmd])
    
    if result.returncode == 0:
        print(f"Success! Report saved to: {report_path}")
    else:
        print("Analysis failed. Ensure the .elf file exists and GDB is installed in Windows.")

if __name__ == "__main__":
    analyze()