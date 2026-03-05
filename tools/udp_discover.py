#!/usr/bin/env python3
"""
UDP discovery tool for OpenFilamentSensor devices.

Broadcasts the SDCP discovery probe ("M99999") on port 3000 and prints
the IP address and payload of every device that responds.

Usage:
    python tools/udp_discover.py [--timeout <seconds>]

Arguments:
    --timeout   How long to listen for responses, in seconds (default: 5)
"""

import argparse
import socket
import time


DISCOVERY_PORT = 3000
DISCOVERY_MSG = b"M99999"
BROADCAST_ADDR = "255.255.255.255"
BUFFER_SIZE = 256


def discover(timeout: float = 5.0) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("", DISCOVERY_PORT))
        sock.settimeout(0.4)

        deadline = time.monotonic() + timeout
        sock.sendto(DISCOVERY_MSG, (BROADCAST_ADDR, DISCOVERY_PORT))
        print(f"Sent discovery probe to {BROADCAST_ADDR}:{DISCOVERY_PORT} — listening for {timeout}s …")

        seen = set()
        # Determine the local IP used for outbound UDP traffic so we can
        # filter out our own broadcast echo.  Fall back to None on failure.
        my_ip: str | None = None
        try:
            probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            probe.connect((BROADCAST_ADDR, DISCOVERY_PORT))
            my_ip = probe.getsockname()[0]
            probe.close()
        except Exception:
            pass

        while time.monotonic() < deadline:
            # Re-broadcast every 400 ms (mirrors firmware behaviour)
            sock.sendto(DISCOVERY_MSG, (BROADCAST_ADDR, DISCOVERY_PORT))
            try:
                data, addr = sock.recvfrom(BUFFER_SIZE)
            except socket.timeout:
                continue

            ip = addr[0]
            if ip == my_ip or ip in seen:
                continue

            seen.add(ip)
            try:
                payload = data.decode("utf-8", errors="replace")
            except Exception:
                payload = repr(data)
            print(f"  {ip}: {payload}")

        if seen:
            print(f"\nFound {len(seen)} device(s).")
        else:
            print("No devices found.")
    finally:
        sock.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="Discover OpenFilamentSensor devices via UDP.")
    parser.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        metavar="SECONDS",
        help="How long to listen for responses (default: 5)",
    )
    args = parser.parse_args()
    discover(timeout=args.timeout)


if __name__ == "__main__":
    main()
