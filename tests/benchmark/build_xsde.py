#!/usr/bin/env python3
"""Generate and build the comparable XSD/e C++/Parser no-op driver."""

import argparse
import shlex
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--xsde", default="xsde", help="XSD/e compiler executable")
    parser.add_argument("--cxx", default="c++", help="target C++ compiler")
    parser.add_argument(
        "--runtime-include",
        type=Path,
        action="append",
        required=True,
        help="include directory containing xsde/; repeat for generated config headers",
    )
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
            "cxx-parser",
            "--generate-noop-impl",
            "--generate-test-driver",
            "--output-dir",
            str(args.output_dir),
            str(source_dir / "benchmark.xsd"),
        ],
        check=True,
    )

    sources = sorted(args.output_dir.glob("*.cxx"))
    if not sources:
        raise RuntimeError("XSD/e did not generate any .cxx source")
    output = args.output_dir / "xsde-benchmark"
    command = [
        args.cxx,
        *shlex.split(args.cxxflags),
        *(flag for path in args.runtime_include for flag in ("-I", str(path))),
        *(str(path) for path in sources),
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
