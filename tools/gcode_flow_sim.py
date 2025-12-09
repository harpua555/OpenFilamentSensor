#!/usr/bin/env python3
"""Derive synthetic filament flow samples from a G-code file."""

import argparse
import asyncio
import json
import re
from pathlib import Path
from typing import Generator, Iterable, List, Tuple

from aiohttp import web

EXTRUSION_RE = re.compile(r"[Ee]([+-]?\d*\.?\d+)")


def extract_extrusion_value(line: str) -> float | None:
    match = EXTRUSION_RE.search(line)
    if not match:
        return None
    return float(match.group(1))


def parse_gcode(path: Path) -> Generator[float, None, None]:
    """Yield extrusion deltas (mm) from a G-code file."""
    absolute_mode = True
    last_e = 0.0

    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        for raw_line in handle:
            stripped = raw_line.split(";", 1)[0].strip()
            if not stripped:
                continue

            command = stripped.split()[0]
            if command in ("M82", "G90"):
                absolute_mode = True
                continue
            if command in ("M83", "G91"):
                absolute_mode = False
                continue
            if command == "G92":
                value = extract_extrusion_value(stripped)
                if value is not None:
                    last_e = value
                continue

            if command not in ("G0", "G1"):
                continue

            value = extract_extrusion_value(stripped)
            if value is None:
                continue

            if absolute_mode:
                delta = value - last_e
                last_e = value
            else:
                delta = value

            if abs(delta) > 1e-6:
                yield delta


def chunk_extrusion(
    deltas: Iterable[float],
    interval_ms: int,
    max_chunk_mm: float,
    include_retractions: bool,
) -> Generator[Tuple[int, float, float], None, None]:
    """Break extrusion deltas into fixed-time chunks."""
    timestamp = 0
    total = 0.0

    for delta in deltas:
        target_sign = 1 if delta > 0 else -1
        if target_sign < 0 and not include_retractions:
            timestamp += interval_ms
            continue

        remaining = abs(delta)
        while remaining > 1e-6:
            chunk = min(max_chunk_mm, remaining)
            chunk *= target_sign
            remaining -= abs(chunk)

            total += chunk
            yield timestamp, chunk, total
            timestamp += interval_ms


def format_table(samples: Iterable[Tuple[int, float, float]]) -> str:
    lines = ["timestamp_ms,delta_mm,total_mm"]
    for ts, delta, total in samples:
        lines.append(f"{ts},{delta:.4f},{total:.4f}")
    return "\n".join(lines)


def format_json(samples: Iterable[Tuple[int, float, float]]) -> str:
    return "\n".join(
        json.dumps(
            {
                "timestamp_ms": ts,
                "PrintInfo": {
                    "CurrentExtrusion": round(delta, 6),
                    "TotalExtrusion": round(total, 6),
                },
            }
        )
        for ts, delta, total in samples
    )


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Convert a G-code file into synthetic extrusion samples "
        "for validating the ESP32 firmware without printing."
    )
    parser.add_argument("gcode", type=Path, help="Path to the G-code file")
    parser.add_argument(
        "--interval-ms", type=int, default=250, help="Sampling window (default: 250ms)"
    )
    parser.add_argument(
        "--max-chunk-mm",
        type=float,
        default=3.0,
        help="Maximum filament (mm) emitted per sample (default: 3 mm)",
    )
    parser.add_argument(
        "--include-retractions",
        action="store_true",
        help="Emit negative samples for retractions instead of skipping them",
    )
    parser.add_argument(
        "--output",
        choices=("table", "json"),
        default="table",
        help="Output format (default: table)",
    )
    parser.add_argument(
        "--serve",
        action="store_true",
        help="Start a websocket server that replays the extrusion data",
    )
    parser.add_argument(
        "--host",
        default="0.0.0.0",
        help="Websocket host when using --serve (default: 0.0.0.0)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=3030,
        help="Websocket port when using --serve (default: 3030)",
    )
    parser.add_argument(
        "--repeat",
        action="store_true",
        help="Loop the G-code stream indefinitely when serving",
    )
    parser.add_argument(
        "--speed",
        type=float,
        default=1.0,
        help="Speed multiplier for --serve timing (default: 1.0)",
    )
    return parser


def build_status_payload(delta: float, total: float, index: int, count: int) -> str:
    """Build a websocket payload that mimics the Elegoo printer.

    We include the fields that the firmware expects for proper gating:
    - PrintInfo.Status = 13 (printing)
    - Status.CurrentStatus = [1] (SDCP_MACHINE_STATUS_PRINTING)
    - Basic progress/ticks so the UI looks sane during simulation.
    """
    if count <= 0:
        progress = 0
        current_ticks = 0
        total_ticks = 0
    else:
        progress = int(min(100, max(0, (index * 100) // count)))
        total_ticks = count
        current_ticks = min(index, count)

    status = {
        "Topic": "status/simulator",
        "Status": {
            # Machine status array: [PRINTING]
            "CurrentStatus": [1],
            # Minimal PrintInfo block for ElegooCC::handleStatus
            "PrintInfo": {
                "Status": 13,  # SDCP_PRINT_STATUS_PRINTING
                "CurrentLayer": 0,
                "TotalLayer": 0,
                "Progress": progress,
                "CurrentTicks": current_ticks,
                "TotalTicks": total_ticks,
                "PrintSpeedPct": 100,
                "CurrentExtrusion": round(delta, 6),
                "TotalExtrusion": round(total, 6),
            },
        },
        "MainboardID": "SIMULATOR",
    }
    return json.dumps(status)


async def stream_samples(
    ws: web.WebSocketResponse,
    samples: List[Tuple[int, float, float]],
    repeat: bool,
    speed: float,
) -> None:
    if not samples:
        return

    count = len(samples)
    while True:
        for index, (_, delta, total) in enumerate(samples):
            await ws.send_str(build_status_payload(delta, total, index, count))
            if index + 1 < count:
                delay_ms = samples[index + 1][0] - samples[index][0]
            else:
                delay_ms = samples[0][0] if repeat else 0
            await asyncio.sleep(max(delay_ms, 0) / 1000.0 / max(speed, 1e-3))
        if not repeat:
            break


async def serve_samples(
    samples: List[Tuple[int, float, float]],
    host: str,
    port: int,
    repeat: bool,
    speed: float,
) -> None:
    app = web.Application()

    async def websocket_handler(request: web.Request) -> web.WebSocketResponse:
        ws = web.WebSocketResponse()
        await ws.prepare(request)
        print("Simulator: client connected", flush=True)
        try:
            await stream_samples(ws, samples, repeat, speed)
        finally:
            await ws.close()
            print("Simulator: client disconnected", flush=True)
        return ws

    app.router.add_get("/websocket", websocket_handler)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, host, port)
    await site.start()
    print(
        f"Simulator serving {len(samples)} samples on ws://{host}:{port}/websocket "
        f"(repeat={'on' if repeat else 'off'}, speed={speed}x)"
    )
    try:
        while True:
            await asyncio.sleep(3600)
    except asyncio.CancelledError:
        pass
    finally:
        await runner.cleanup()


def main() -> None:
    args = build_arg_parser().parse_args()
    if not args.gcode.exists():
        raise SystemExit(f"G-code file not found: {args.gcode}")

    deltas = parse_gcode(args.gcode)
    samples = list(
        chunk_extrusion(
            deltas, args.interval_ms, args.max_chunk_mm, args.include_retractions
        )
    )

    if args.serve:
        if not samples:
            raise SystemExit("No extrusion moves found in the provided G-code.")
        asyncio.run(serve_samples(samples, args.host, args.port, args.repeat, args.speed))
    else:
        formatter = format_table if args.output == "table" else format_json
        print(formatter(samples))
        print(f"\nGenerated {len(samples)} samples.", flush=True)


if __name__ == "__main__":
    main()
