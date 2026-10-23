#!/usr/bin/env python3
"""Convert a benchmark XML fixture to a generated C byte array."""

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    data = args.input.read_bytes()
    lines = [
        "/* Generated benchmark fixture. Do not edit. */",
        "#include <stddef.h>",
        "const unsigned char benchmark_xml[] = {",
    ]
    for offset in range(0, len(data), 16):
        values = ", ".join(f"0x{value:02x}" for value in data[offset : offset + 16])
        lines.append(f"\t{values},")
    lines.extend(
        (
            "};",
            f"const size_t benchmark_xml_size = {len(data)}u;",
            "",
        )
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
