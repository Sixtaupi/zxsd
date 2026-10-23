#!/usr/bin/env python3
"""Generate the deterministic XML input shared by both benchmark runners."""

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--samples", type=int, default=1024)
    args = parser.parse_args()
    if args.samples < 1:
        parser.error("--samples must be positive")

    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<samples xmlns="urn:mxsd:benchmark">',
    ]
    for index in range(args.samples):
        lines.extend(
            (
                f'  <sample id="{index}">',
                f"    <label>sensor_{index:05d}</label>",
                f"    <enabled>{'true' if index % 2 else 'false'}</enabled>",
                f"    <reading>{(index % 2001) - 1000}</reading>",
                "  </sample>",
            )
        )
    lines.append("</samples>")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
