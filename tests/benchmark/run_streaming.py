#!/usr/bin/env python3
"""Audit and run the paired Zephyr typed-streaming benchmark; never time HTTP/TLS."""

import argparse
import json
import queue
import re
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

from stream_protocol import (
    audit_pair,
    build_metadata,
    elf_memory,
    reference_values,
    sha256,
    validate_run,
)


def capture(west: str, build: Path, timeout: float) -> dict:
    """Collect complete JSON records and terminate QEMU normally after BENCH_DONE."""
    command = [west, "build", "-d", str(build), "-t", "run"]
    process = subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    lines = queue.Queue()

    def read():
        for line in process.stdout:
            lines.put(line)
        lines.put(None)

    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    console, samples, meta = [], [], None
    deadline = time.monotonic() + timeout
    try:
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("benchmark timed out:\n" + "".join(console))
            try:
                line = lines.get(timeout=remaining)
            except queue.Empty as error:
                raise TimeoutError("benchmark timed out:\n" + "".join(console)) from error
            if line is None:
                raise RuntimeError("benchmark exited before BENCH_DONE:\n" + "".join(console))
            console.append(line)
            if line.startswith("BENCH_ERROR"):
                raise RuntimeError("".join(console))
            for prefix in ("BENCH_META ", "BENCH_SAMPLE ", "BENCH_DONE "):
                if not line.startswith(prefix):
                    continue
                record = json.loads(line[len(prefix) :])
                if prefix == "BENCH_META ":
                    if meta is not None:
                        raise ValueError("duplicate benchmark metadata")
                    meta = record
                elif prefix == "BENCH_SAMPLE ":
                    samples.append(record)
                else:
                    if meta is None:
                        raise ValueError("missing benchmark metadata")
                    return {
                        "command": command,
                        "meta": meta,
                        "samples": samples,
                        "done": record,
                        "console": "".join(console),
                    }
    finally:
        if process.poll() is None:
            try:
                process.stdin.write("\x01x")
                process.stdin.flush()
            except BrokenPipeError:
                pass
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        reader.join(timeout=1)
        process.stdin.close()
        process.stdout.close()


def summarize(runs: list[dict]) -> dict:
    """Keep raw iterations plus statistics, distinguishing virtual rates from hardware rates."""
    samples = [sample for run in runs for sample in run["samples"]]
    cycles = [sample["cycles"] for sample in samples]
    first = [sample["first_cycles"] for sample in samples]
    median = statistics.median(cycles)
    meta = runs[0]["meta"]
    return {
        "median_cycles": median,
        "minimum_cycles": min(cycles),
        "maximum_cycles": max(cycles),
        "median_first_result_cycles": statistics.median(first),
        "first_result_supplied_bytes": sorted(
            {sample["first_supplied_bytes"] for sample in samples}
        ),
        "bytes_per_cycle": meta["input_bytes"] / median,
        "samples_per_cycle": samples[0]["count"] / median,
        "clock_derived_mib_per_second": meta["input_bytes"] * meta["clock_hz"] / median / 1048576,
        "clock_derived_first_result_us": statistics.median(first) * 1000000 / meta["clock_hz"],
        "maximum_observed_worker_stack_bytes": max(
            run["meta"]["worker_stack_peak_bytes"] for run in runs
        ),
        "maximum_observed_heap_bytes": max(run["meta"]["heap_peak_bytes"] for run in runs),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mxsd-build", required=True, type=Path)
    parser.add_argument("--xsde-build", required=True, type=Path)
    parser.add_argument("--west", default="west")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument(
        "--repeats", type=int, default=2, help="boots per implementation; order alternates"
    )
    args = parser.parse_args()
    if args.timeout <= 0 or args.repeats < 1:
        parser.error("timeout and repeats must be positive")
    builds = {"mxsd": args.mxsd_build.resolve(), "xsde": args.xsde_build.resolve()}
    for build in builds.values():
        subprocess.run([args.west, "build", "-d", str(build)], check=True)
    metadata = {name: build_metadata(path) for name, path in builds.items()}
    manifest = json.loads((builds["xsde"] / "benchmark-runtime.json").read_text(encoding="utf-8"))
    audit_pair(metadata["mxsd"], metadata["xsde"], manifest)
    xml = (builds["mxsd"] / "benchmark.xml").read_bytes()
    expected = reference_values(xml)
    report = {
        "protocol": "full XML validation + identical typed sample values + common canonical checksum",
        "scope": {
            "xml": "preloaded offline fixture; initialization, feeds, finish, disposal included",
            "transport_http_tls": {
                "included": False,
                "measurements": None,
                "note": "not measured, not zero; no network/TLS in either image",
            },
            "first_result": "complete typed subtree at callback entry; provisional until document finish",
            "stack": "observed high-water mark of equal dedicated worker stacks; not a worst-case proof",
            "heap": "global libc high-water mark before reporting/self-tests; inside reserved static arena",
            "rom": "linked loadable section payload, not ELF file size or a minimum-runtime claim",
            "qemu": "cycle/rate readings use QEMU virtual time; not physical Cortex-A53 throughput",
        },
        "schema_sha256": sha256(Path(__file__).parent / "benchmark.xsd"),
        "reference_values": expected,
        "builds": metadata,
        "xsde_runtime": manifest,
        "runs": {"mxsd": [], "xsde": []},
        "execution_order": [],
    }
    cache = (builds["xsde"] / "CMakeCache.txt").read_text(encoding="utf-8")
    compiler = re.search(r"^XSDE_COMPILER:[^=]+=(.+)$", cache, re.M).group(1)
    report["generators"] = {
        "xsde": subprocess.check_output(
            [compiler, "--version"], stderr=subprocess.STDOUT, text=True
        ),
        "mxsdc": subprocess.check_output(
            [
                sys.executable,
                str(Path(__file__).resolve().parents[2] / "mxsdc"),
                "--abi-info",
            ],
            text=True,
        ),
    }
    report["qemu_version"] = subprocess.check_output(
        [metadata["mxsd"]["emulator_command"][0], "--version"], text=True
    )
    for repeat in range(args.repeats):
        for name in ("mxsd", "xsde") if repeat % 2 == 0 else ("xsde", "mxsd"):
            result = capture(args.west, builds[name], args.timeout)
            if result["meta"]["implementation"] != name or result["meta"]["input_bytes"] != len(
                xml
            ):
                raise ValueError("unexpected image or input size")
            validate_run(result["meta"], result["samples"], result["done"], expected)
            report["runs"][name].append(result)
            report["execution_order"].append(name)
            print(name + ": measurement complete", flush=True)
    reference_meta = report["runs"]["mxsd"][0]["meta"]
    for runs in report["runs"].values():
        for run in runs:
            for key in ("clock_hz", "block_size", "runs", "worker_stack_reserved_bytes"):
                if reference_meta[key] != run["meta"][key]:
                    raise ValueError("different measurement protocol: " + key)
    report["memory"] = {
        name: elf_memory(path / "zephyr/zephyr.elf") for name, path in builds.items()
    }
    report["summary"] = {name: summarize(runs) for name, runs in report["runs"].items()}
    report["mxsd_over_xsde"] = {
        "total_cycles": report["summary"]["mxsd"]["median_cycles"]
        / report["summary"]["xsde"]["median_cycles"],
        "first_result_cycles": report["summary"]["mxsd"]["median_first_result_cycles"]
        / report["summary"]["xsde"]["median_first_result_cycles"],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {"summary": report["summary"], "mxsd_over_xsde": report["mxsd_over_xsde"]}, indent=2
        )
    )


if __name__ == "__main__":
    main()
