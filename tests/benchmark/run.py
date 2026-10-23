#!/usr/bin/env python3
"""Run two schema validators under the same process-level benchmark protocol."""

import argparse
import json
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import List, Optional, Tuple


def peak_rss(command: List[str]) -> Tuple[Optional[int], str]:
    time_command = Path("/usr/bin/time")
    if not time_command.exists():
        return None, "unavailable: /usr/bin/time was not found"
    option = "-l" if sys.platform == "darwin" else "-v"
    completed = subprocess.run(
        [str(time_command), option, *command],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    for line in completed.stderr.splitlines():
        if sys.platform == "darwin" and "maximum resident set size" in line:
            return int(line.split()[0]), "bytes reported by /usr/bin/time -l"
        if "Maximum resident set size (kbytes):" in line:
            return int(line.rsplit(":", 1)[1].strip()) * 1024, "KiB converted to bytes"
    return None, f"unavailable: peak RSS was absent from /usr/bin/time {option}"


def linked_size(executable: Path) -> str | None:
    size_command = shutil.which("size")
    if size_command is None:
        return None
    completed = subprocess.run(
        [size_command, str(executable.resolve())],
        check=True,
        capture_output=True,
        text=True,
    )
    return completed.stdout.strip()


def measure(executable: Path, xml: Path, warmups: int, runs: int) -> dict[str, object]:
    command = [str(executable.resolve()), str(xml.resolve())]
    for _ in range(warmups):
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

    elapsed = []
    for _ in range(runs):
        started = time.perf_counter_ns()
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        elapsed.append((time.perf_counter_ns() - started) / 1_000_000.0)
    resident_bytes, resident_note = peak_rss(command)
    return {
        "command": command,
        "binary_bytes": executable.stat().st_size,
        "linked_size": linked_size(executable),
        "peak_resident_bytes": resident_bytes,
        "peak_resident_note": resident_note,
        "runs_ms": elapsed,
        "median_ms": statistics.median(elapsed),
        "minimum_ms": min(elapsed),
        "maximum_ms": max(elapsed),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mxsd", type=Path, required=True)
    parser.add_argument("--xsde", type=Path, required=True)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--warmups", type=int, default=2)
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.warmups < 0 or args.runs < 1:
        parser.error("--warmups must be non-negative and --runs must be positive")

    report = {
        "protocol": "one process and one streaming validation per run",
        "input": str(args.input.resolve()),
        "input_bytes": args.input.stat().st_size,
        "warmups": args.warmups,
        "runs": args.runs,
        "mxsd": measure(args.mxsd, args.input, args.warmups, args.runs),
        "xsde": measure(args.xsde, args.input, args.warmups, args.runs),
    }
    encoded = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
