#!/usr/bin/env python3
"""Build and measure the Zephyr benchmark at increasing repetition counts."""

import argparse
import json
import queue
import re
import subprocess
import threading
import time
from pathlib import Path


def measure(west: str, build_dir: Path, timeout: float) -> dict[str, object]:
    """Read target measurements and request normal QEMU termination via its console."""
    command = [west, "build", "-d", str(build_dir), "-t", "run"]
    process = subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    lines: queue.Queue[str | None] = queue.Queue()

    def read_output() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            lines.put(line)
        lines.put(None)

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    raw_output = []
    metrics: dict[str, int] = {}
    started = time.monotonic()
    try:
        while len(metrics) < 3:
            remaining = timeout - (time.monotonic() - started)
            if remaining <= 0:
                raise TimeoutError("QEMU did not report all three benchmark measurements")
            try:
                line = lines.get(timeout=remaining)
            except queue.Empty as error:
                raise TimeoutError("QEMU benchmark timed out") from error
            if line is None:
                raise RuntimeError("QEMU exited before measurements:\n" + "".join(raw_output))
            raw_output.append(line)
            match = re.match(
                r"^(validation|binding serialization|serialization): (\d+) cycles/run",
                line,
            )
            if match:
                metrics[match[1]] = int(match[2])
        return {"cycles_per_run": metrics, "console": "".join(raw_output)}
    finally:
        if process.poll() is None:
            assert process.stdin is not None
            try:
                process.stdin.write("\x01x")
                process.stdin.flush()
            except BrokenPipeError:
                pass
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=10)
        reader.join(timeout=1)
        if process.stdin is not None:
            process.stdin.close()
        if process.stdout is not None:
            process.stdout.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--west", default="west")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    application = Path(__file__).resolve().parent / "zephyr" / "mxsd"
    runs = []
    for index, samples in enumerate((128, 256, 512, 1024, 2048)):
        command = [
            args.west,
            "build",
            "-b",
            "qemu_cortex_a53",
            str(application),
            "-d",
            str(args.build_dir.resolve()),
        ]
        if index == 0:
            command.append("--pristine")
        command.extend(("--", f"-DBENCHMARK_SAMPLE_COUNT={samples}"))
        subprocess.run(command, check=True)
        measurement = measure(args.west, args.build_dir.resolve(), args.timeout)
        measurement["samples"] = samples
        runs.append(measurement)
        print(f"{samples}: {measurement['cycles_per_run']}", flush=True)

    # A fixed schema and token size should have approximately constant cost per sample.
    # Keep a generous ratio for fixed initialization costs and scheduler/timer noise.
    ratios = {}
    for operation in ("validation", "binding serialization", "serialization"):
        costs = [run["cycles_per_run"][operation] / run["samples"] for run in runs]
        ratios[operation] = max(costs) / min(costs)
    report = {
        "board": "qemu_cortex_a53",
        "runs_per_measurement": 20,
        "history_capacity": 1,
        "normalized_cost_max_min": ratios,
        "runs": runs,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if any(ratio > 1.5 for ratio in ratios.values()):
        raise RuntimeError("per-sample cost changed by more than 50%; inspect scaling results")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
