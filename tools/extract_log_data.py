#!/usr/bin/env python3
"""
Extract flow tracking data from ESP32 logs for unit test replay.

Parses log files containing flow tracking data and outputs CSV format
for test playback.

Usage:
    python extract_log_data.py input.log                    # outputs to ./condensed/input.csv
    python extract_log_data.py -i normal_print.log -o test_normal.csv
    python extract_log_data.py -i soft_snag.log -o test_soft_snag.csv -d ./condensed
"""

import argparse
import os
import re
import sys


def parse_flow_line(line):
    """
    Parse a verbose flow state log line.

    Expected format:
    - Flow: win_exp=X win_sns=Y deficit=Z | cumul=W pulses=P | thr=T ratio=R jam=J hard=H soft=S pass=P heap=M

    Note: Lines may be truncated in logs, so we extract what we can
    """
    # Try verbose flow format - handle potentially truncated lines
    # Look for the key fields we need: win_exp, win_sns, deficit, and try to get jam/hard/soft/pass if present

    # First check if this is a Flow: line
    if 'Flow:' not in line:
        return None

    data = {}

    # Extract windowed expected
    match = re.search(r'win_exp=([\d.]+)', line)
    if match:
        data['expected'] = float(match.group(1))
    else:
        return None

    # Extract windowed sensor
    match = re.search(r'win_sns=([\d.]+)', line)
    if match:
        data['actual'] = float(match.group(1))
    else:
        return None

    # Extract deficit
    match = re.search(r'deficit=([\d.]+)', line)
    if match:
        data['deficit'] = float(match.group(1))

    # Extract ratio (may be in different positions)
    match = re.search(r'ratio=([\d.]+)', line)
    if match:
        data['ratio'] = float(match.group(1))

    # Extract jam state (0 or 1)
    match = re.search(r'jam=(\d+)', line)
    if match:
        data['jam_state'] = int(match.group(1))
    else:
        data['jam_state'] = 0

    # Extract hard jam percent
    match = re.search(r'hard=([\d.]+)', line)
    if match:
        data['hard_pct'] = float(match.group(1))
    else:
        data['hard_pct'] = 0.0

    # Extract soft jam percent
    match = re.search(r'soft=([\d.]+)', line)
    if match:
        data['soft_pct'] = float(match.group(1))
    else:
        data['soft_pct'] = 0.0

    # Extract pass ratio (for ratio field in CSV)
    match = re.search(r'pass=([\d.]+)', line)
    if match:
        data['pass_ratio'] = float(match.group(1))
    elif 'ratio' in data:
        # Use ratio field if pass not found
        data['pass_ratio'] = data['ratio']
    else:
        data['pass_ratio'] = 0.0

    return data


def parse_jam_detection(line):
    """Check if line indicates a jam detection event."""
    if 'Filament jam detected' in line:
        if 'hard+soft' in line.lower():
            return 'hard+soft'
        elif 'hard' in line.lower():
            return 'hard'
        elif 'soft' in line.lower():
            return 'soft'
    return None


def extract_timestamp(line):
    """
    Extract timestamp from log line.

    Formats:
    - Raw timestamp at start: "1764214927 Flow: ..."
    - Bracketed: "[12345] Flow: ..."
    """
    # Try raw timestamp at start (most common)
    match = re.match(r'^(\d{10,})\s+', line)
    if match:
        return int(match.group(1))

    # Try bracketed format
    match = re.match(r'^\[(\d+)\]', line)
    if match:
        return int(match.group(1))

    return None


def main():
    parser = argparse.ArgumentParser(
        description='Extract flow tracking data from ESP32 logs for unit test replay',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Default - output to ./condensed/<input_basename>.csv
  python extract_log_data.py normal_print.log

  # Specify output file
  python extract_log_data.py -i normal_print.log -o test_normal.csv

  # Specify output directory (creates ./condensed/test_normal.csv)
  python extract_log_data.py -i normal_print.log -o test_normal.csv -d ./condensed
        """
    )

    # Make input optional as positional argument
    parser.add_argument('input_file', nargs='?', default=None,
                        help='Input log file to parse (positional)')
    parser.add_argument('-i', '--input', default=None,
                        help='Input log file to parse')
    parser.add_argument('-o', '--output', default=None,
                        help='Output CSV file (default: auto-generated from input name)')
    parser.add_argument('-d', '--outputdir', default='./condensed',
                        help='Output directory (default: ./condensed)')

    args = parser.parse_args()

    # Determine input file (positional takes precedence)
    input_file = args.input_file or args.input
    if not input_file:
        parser.print_help()
        sys.exit(1)

    # Auto-generate output filename if not specified
    if not args.output:
        # Get base name without extension, add .csv
        base_name = os.path.splitext(os.path.basename(input_file))[0]
        output_filename = f"{base_name}.csv"
    else:
        output_filename = args.output

    # Create output directory
    os.makedirs(args.outputdir, exist_ok=True)
    output_path = os.path.join(args.outputdir, output_filename)

    # Open output file
    output_file = open(output_path, 'w')

    # Write CSV header
    output_file.write("timestamp_ms,expected_mm,actual_mm,deficit_mm,ratio,hard_pct,soft_pct,jam_state,print_status\n")

    # Parse log file
    last_timestamp = 0
    line_count = 0
    data_count = 0

    try:
        with open(input_file, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                line_count += 1

                # Try to extract timestamp from log line
                log_ts = extract_timestamp(line)
                if log_ts is not None:
                    last_timestamp = log_ts

                # Parse flow data
                flow = parse_flow_line(line)
                if flow:
                    # Output CSV row
                    output_file.write(f"{last_timestamp},"
                                   f"{flow.get('expected', 0.0):.2f},"
                                   f"{flow.get('actual', 0.0):.2f},"
                                   f"{flow.get('deficit', 0.0):.2f},"
                                   f"{flow.get('pass_ratio', 0.0):.3f},"
                                   f"{flow.get('hard_pct', 0.0):.1f},"
                                   f"{flow.get('soft_pct', 0.0):.1f},"
                                   f"{flow.get('jam_state', 0)},"
                                   f"13\n")  # 13 = SDCP_PRINT_STATUS_PRINTING

                    data_count += 1

        # Print stats to stderr
        sys.stderr.write(f"\nProcessed {line_count} lines, extracted {data_count} data points\n")
        sys.stderr.write(f"Output written to: {output_path}\n")

    finally:
        output_file.close()


if __name__ == '__main__':
    main()
