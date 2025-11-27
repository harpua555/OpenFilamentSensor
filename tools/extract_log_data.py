#!/usr/bin/env python3
"""
Extract flow tracking data from ESP32 logs for unit test replay.

Usage:
    python parse_test_log.py normal_print.log > test_normal.csv
    python parse_test_log.py soft_snag.log > test_soft_snag.csv
    python parse_test_log.py hard_snag.log > test_hard_snag.csv
"""

import re
import sys

def parse_flow_line(line):
    """Parse a flow state log line."""
    pattern = r'expected=([\d.]+)mm actual=([\d.]+)mm deficit=([\d.]+)mm.*?ratio=([\d.]+)'
    match = re.search(pattern, line)
    if match:
        return {
            'expected': float(match.group(1)),
            'actual': float(match.group(2)),
            'deficit': float(match.group(3)),
            'ratio': float(match.group(4))
        }
    return None

def parse_jam_line(line):
    """Check if line indicates a jam detection."""
    if 'Filament jam detected' in line:
        if 'hard' in line.lower():
            return 'hard'
        elif 'soft' in line.lower():
            return 'soft'
    return None

def main(log_file):
    print("timestamp_ms,expected_mm,actual_mm,deficit_mm,ratio,jam_state")
    
    timestamp = 0
    with open(log_file, 'r') as f:
        for line in f:
            flow = parse_flow_line(line)
            if flow:
                jam = parse_jam_line(line)
                jam_code = 2 if jam == 'hard' else (1 if jam == 'soft' else 0)
                
                print(f"{timestamp},{flow['expected']:.2f},{flow['actual']:.2f},"
                      f"{flow['deficit']:.2f},{flow['ratio']:.2f},{jam_code}")
                timestamp += 1000

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    main(sys.argv[1])