#!/usr/bin/env python3
"""Compare schema-valid serialization to a counting sink."""

import argparse
import json
import statistics
import subprocess
import time
from pathlib import Path


def execute(command: list[str]) -> tuple[float, int]:
    started = time.perf_counter_ns()
    completed = subprocess.run(command, check=True, capture_output=True, text=True)
    elapsed = (time.perf_counter_ns() - started) / 1_000_000.0
    return elapsed, int(completed.stdout.strip())


def result(executable: Path, elapsed: list[float], byte_count: int) -> dict[str, object]:
    return {
        "command": [str(executable.resolve())],
        "binary_bytes": executable.stat().st_size,
        "output_bytes": byte_count,
        "runs_ms": elapsed,
        "median_ms": statistics.median(elapsed),
        "minimum_ms": min(elapsed),
        "maximum_ms": max(elapsed),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mxsd", type=Path, required=True)
    parser.add_argument("--xsde", type=Path, required=True)
    parser.add_argument("--warmups", type=int, default=2)
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.warmups < 0 or args.runs < 1:
        parser.error("--warmups must be non-negative and --runs must be positive")

    commands = {
        "mxsd": [str(args.mxsd.resolve())],
        "xsde": [str(args.xsde.resolve())],
    }
    byte_counts = {}
    elapsed = {"mxsd": [], "xsde": []}
    for index in range(args.warmups):
        order = ("mxsd", "xsde") if index % 2 == 0 else ("xsde", "mxsd")
        for name in order:
            _, byte_counts[name] = execute(commands[name])
    for index in range(args.runs):
        order = ("mxsd", "xsde") if index % 2 == 0 else ("xsde", "mxsd")
        for name in order:
            duration, current_count = execute(commands[name])
            if name in byte_counts and current_count != byte_counts[name]:
                raise RuntimeError(f"{name} serializer output size changed between runs")
            byte_counts[name] = current_count
            elapsed[name].append(duration)

    report = {
        "protocol": "one process and one validated serialization to a counting sink per run",
        "ordering": "mxsd and xsde alternate first position on every run",
        "samples": 1024,
        "warmups": args.warmups,
        "runs": args.runs,
        "mxsd": result(args.mxsd, elapsed["mxsd"], byte_counts["mxsd"]),
        "xsde": result(args.xsde, elapsed["xsde"], byte_counts["xsde"]),
    }
    encoded = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
