#!/usr/bin/env python3
"""Produce deterministic mxsd resource measurements from build artifacts."""

import argparse
import json
import pathlib
import subprocess


def size(tool, path):
    output = subprocess.check_output([tool, str(path)], text=True).splitlines()
    totals = {"text": 0, "data": 0, "bss": 0}
    rows = 0
    for line in output:
        fields = line.split()
        if len(fields) < 3 or not all(value.isdigit() for value in fields[:3]):
            continue
        totals["text"] += int(fields[0])
        totals["data"] += int(fields[1])
        totals["bss"] += int(fields[2])
        rows += 1
    if rows == 0:
        raise SystemExit("size tool produced no measurement for %s" % path)
    return totals


def peak_frame(paths):
    maximum = 0
    symbol = None
    for path in paths:
        for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
            fields = line.split("\t")
            if len(fields) >= 2 and fields[1].isdigit() and int(fields[1]) > maximum:
                maximum = int(fields[1])
                symbol = fields[0]
    return maximum, symbol


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", required=True)
    parser.add_argument("--size-tool", required=True)
    parser.add_argument("--runtime-object", required=True, type=pathlib.Path)
    parser.add_argument("--generated-object", required=True, type=pathlib.Path)
    parser.add_argument("--elf", required=True, type=pathlib.Path)
    parser.add_argument("--stack-usage", nargs="*", default=[])
    parser.add_argument("--zero-heap", action="store_true")
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    frame, symbol = peak_frame(args.stack_usage)
    runtime = size(args.size_tool, args.runtime_object)
    generated = size(args.size_tool, args.generated_object)
    image = size(args.size_tool, args.elf)
    report = {
        "platform": args.platform,
        "generated_code_bytes": generated["text"],
        "libmxsd_flash_bytes": runtime["text"] + runtime["data"],
        "libmxsd_static_ram_bytes": runtime["data"] + runtime["bss"],
        "image_flash_bytes": image["text"] + image["data"],
        "image_static_ram_bytes": image["data"] + image["bss"],
        "largest_function_stack_frame_bytes": frame,
        "largest_function_stack_frame": symbol,
        "peak_heap_bytes": 0 if args.zero_heap else None,
        "stack_note": "Static compiler frame maximum; recursive call-chain peak depends on XML/schema depth.",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
