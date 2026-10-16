#!/usr/bin/env python3
"""Run the licensed mxsd conformance manifest and emit a JSON report."""

import argparse
import json
import pathlib
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--build-dir", required=True, type=pathlib.Path)
    parser.add_argument("--build-config")
    parser.add_argument("--ctest", required=True)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    report = {
        "suite": manifest["suite"],
        "license": manifest["license"],
        "supported": [],
        "unsupported": [],
        "expected-failure": [],
        "passed": [],
        "failed": [],
    }
    for feature in manifest["features"]:
        name = feature["name"]
        classification = feature["classification"]
        if classification != "supported":
            report[classification].append(name)
            continue
        if not feature.get("positive_and_negative", False):
            report["failed"].append(name)
            continue
        command = [
            args.ctest,
            "--test-dir",
            str(args.build_dir),
            "-R",
            "^" + feature["test"] + "$",
            "--output-on-failure",
        ]
        if args.build_config:
            command.extend(["-C", args.build_config])
        process = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        if process.returncode == 0 and "100% tests passed" in process.stdout:
            report["supported"].append(name)
            report["passed"].append(name)
        else:
            report["failed"].append(name)
    for key in ("supported", "unsupported", "expected-failure", "passed", "failed"):
        report[key].sort()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, sort_keys=True))
    return 1 if report["failed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
