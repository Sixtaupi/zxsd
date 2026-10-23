#!/usr/bin/env python3
"""Generate and build the XSD/e serializer benchmark runner."""

import argparse
import shlex
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--xsde", default="xsde")
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--runtime-include", type=Path, action="append", required=True)
    parser.add_argument("--runtime-library", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--cxxflags", default="-O2 -DNDEBUG")
    parser.add_argument("--ldflags", default="")
    args = parser.parse_args()

    source_dir = Path(__file__).resolve().parent
    args.output_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            args.xsde,
            "cxx-serializer",
            "--output-dir",
            str(args.output_dir),
            str(source_dir / "benchmark.xsd"),
        ],
        check=True,
    )
    generated = sorted(args.output_dir.glob("*.cxx"))
    if not generated:
        raise RuntimeError("XSD/e did not generate any serializer .cxx source")
    output = args.output_dir / "xsde-serializer-benchmark"
    command = [
        args.cxx,
        *shlex.split(args.cxxflags),
        *(flag for path in args.runtime_include for flag in ("-I", str(path))),
        "-I",
        str(args.output_dir),
        *(str(path) for path in generated),
        str(source_dir / "xsde_serializer.cxx"),
        str(args.runtime_library),
        *shlex.split(args.ldflags),
        "-o",
        str(output),
    ]
    subprocess.run(command, check=True)
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
