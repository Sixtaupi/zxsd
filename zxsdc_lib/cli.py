# Copyright (c) 2026 zxsd contributors
# SPDX-License-Identifier: Apache-2.0

"""Command-line interface for the zxsd schema compiler."""

import argparse
import json
import pathlib
import sys

from .bindings import generate_bindings
from .codegen import generate
from .common import (
    ABI_VERSIONS,
    MAX_SCHEMA_COMPLEXITY,
    VERSION,
    InvalidSchemaError,
    SchemaError,
    ident,
)
from .schema import parse
from .zephyr import generate_zephyr_config


def main():
    """Parse command-line arguments and compile one root schema."""
    parser = argparse.ArgumentParser(prog="zxsdc")
    parser.add_argument("--version", action="version", version="zxsdc " + VERSION)
    parser.add_argument(
        "--abi-info",
        action="version",
        version=json.dumps({"compiler_version": VERSION, **ABI_VERSIONS}, sort_keys=True),
        help="print compiler release and required ABI epochs as JSON",
    )
    parser.add_argument("schema", nargs="+", type=pathlib.Path)
    parser.add_argument("-o", "--output", required=True, type=pathlib.Path)
    parser.add_argument(
        "-I",
        "--schema-path",
        action="append",
        default=[],
        type=pathlib.Path,
        help="explicit local schema search directory",
    )
    parser.add_argument(
        "--max-schema-complexity",
        type=int,
        default=MAX_SCHEMA_COMPLEXITY,
        help="maximum XSD element count across the schema graph",
    )
    parser.add_argument(
        "--allow-unresolved-schema-locations",
        action="store_true",
        help="warn and continue when a schemaLocation hint cannot be retrieved",
    )
    parser.add_argument(
        "--bindings",
        action="store_true",
        help="also generate typed C structures with read/write functions (<stem>.bind.[ch])",
    )
    parser.add_argument(
        "--stream-bind",
        action="append",
        default=[],
        metavar="ELEMENT/PATH",
        help="generate a typed fragment reader for an absolute schema element path (repeatable)",
    )
    parser.add_argument(
        "--zephyr-config",
        action="store_true",
        help="also generate <stem>.conf recommendations and <stem>.limits.h compile-time checks",
    )
    parser.add_argument(
        "--bind-root",
        action="append",
        default=[],
        metavar="NAME",
        help="bind only this global element, by local name or {namespace}name (repeatable)",
    )
    args = parser.parse_args()
    if args.bind_root and not args.bindings:
        parser.error("--bind-root requires --bindings")
    try:
        model = parse(
            args.schema[0],
            search_paths=args.schema_path,
            max_schema_complexity=args.max_schema_complexity,
            additional_paths=args.schema[1:],
            allow_unresolved_locations=args.allow_unresolved_schema_locations,
        )
        args.output.mkdir(parents=True, exist_ok=True)
        stem = ident(args.schema[0].stem)
        header, source = generate(stem, model)
        outputs = {stem + ".zxsd.h": header, stem + ".zxsd.c": source}
        if args.bindings or args.stream_bind:
            bind_header, bind_source = generate_bindings(
                stem,
                model,
                args.bind_root,
                args.stream_bind,
                stream_only=not args.bindings,
            )
            outputs[stem + ".bind.h"] = bind_header
            outputs[stem + ".bind.c"] = bind_source
        if args.zephyr_config:
            fragment, limits_header = generate_zephyr_config(
                stem, model, args.bindings or bool(args.stream_bind)
            )
            outputs[stem + ".conf"] = fragment
            outputs[stem + ".limits.h"] = limits_header
            outputs[stem + ".zxsd.c"] = source.replace(
                '#include "%s.zxsd.h"' % stem,
                '#include "%s.zxsd.h"\n#include "%s.limits.h"' % (stem, stem),
                1,
            )
        # Nothing is written unless every requested output could be generated.
        for name, text in outputs.items():
            (args.output / name).write_text(text, encoding="utf-8", newline="\n")
    except (OSError, SchemaError) as error:
        category = "invalid schema: " if isinstance(error, InvalidSchemaError) else ""
        print("error: %s%s" % (category, error), file=sys.stderr)
        return 1
    return 0
