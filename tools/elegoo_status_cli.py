#!/usr/bin/env python3
"""Minimal CLI for querying Elegoo printers without Home Assistant."""

import argparse
import asyncio
import json
import secrets
import sys
import time
import aiohttp # type: ignore
from typing import Any

WEBSOCKET_PORT = 3030
CMD_REQUEST_STATUS_REFRESH = 0
DEFAULT_TIMEOUT = 5


def build_status_request() -> dict[str, Any]:
    """Build a bare SDCP payload that asks for a status refresh."""
    request_id = secrets.token_hex(8)
    timestamp = int(time.time())
    return {
        "Id": request_id,
        "Data": {
            "Cmd": CMD_REQUEST_STATUS_REFRESH,
            "Data": {},
            "RequestID": request_id,
            "MainboardID": "",
            "TimeStamp": timestamp,
            "From": 2,
        },
    }


def _extract_print_info(message: dict[str, Any]) -> dict[str, Any] | None:
    """Return the PrintInfo block from a websocket message if present."""
    status = message.get("Status")
    if isinstance(status, dict):
        return status.get("PrintInfo")
    return None


async def fetch_status(ip_address: str) -> dict[str, Any] | None:
    """Connect to the printer websocket and return its latest PrintInfo."""
    url = f"ws://{ip_address}:{WEBSOCKET_PORT}/websocket"
    timeout = aiohttp.ClientTimeout(total=DEFAULT_TIMEOUT)
    async with aiohttp.ClientSession(timeout=timeout) as session:
        async with session.ws_connect(url, timeout=DEFAULT_TIMEOUT) as ws:
            await ws.send_json(build_status_request())
            async for msg in ws:
                if msg.type == aiohttp.WSMsgType.TEXT:
                    try:
                        payload = json.loads(msg.data)
                    except json.JSONDecodeError:
                        continue
                    print_info = _extract_print_info(payload)
                    if print_info:
                        return print_info
                elif msg.type in (aiohttp.WSMsgType.ERROR, aiohttp.WSMsgType.CLOSED):
                    break
    return None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Query an Elegoo printer for extrusion telemetry without Home Assistant."
    )
    parser.add_argument("--ip", required=True, help="Printer IP address")
    parser.add_argument(
        "--raw", action="store_true", help="Print the entire PrintInfo JSON block"
    )
    return parser.parse_args()


def format_metric(value: Any) -> str:
    if value is None:
        return "n/a"
    try:
        return f"{float(value):.3f}"
    except (TypeError, ValueError):
        return str(value)


async def main() -> int:
    args = parse_args()
    print_info = await fetch_status(args.ip)
    if not print_info:
        print("Failed to retrieve status from printer", file=sys.stderr)
        return 1

    total = (
        print_info.get("TotalExtrusion")
        or print_info.get("54 6F 74 61 6C 45 78 74 72 75 73 69 6F 6E 00")
    )
    current = (
        print_info.get("CurrentExtrusion")
        or print_info.get("43 75 72 72 65 6E 74 45 78 74 72 75 73 69 6F 6E 00")
    )

    if args.raw:
        print(json.dumps(print_info, indent=2))
    else:
        print(f"Printer @ {args.ip}")
        print(f"  Total requested filament : {format_metric(total)} mm")
        print(f"  Delta over last interval : {format_metric(current)} mm")

    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
