#!/usr/bin/env python3
"""Atheris entry point for XSD parsing and C descriptor generation."""

import pathlib
import sys
import tempfile

import atheris

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))


with atheris.instrument_imports():
    import mxsdc_lib as MXSDC

TEMPORARY = tempfile.TemporaryDirectory(prefix="mxsdc-fuzz-")
SCHEMA = pathlib.Path(TEMPORARY.name) / "input.xsd"


def test_one_input(data):
    """Compile one bounded candidate and generate descriptors when it is valid."""
    if len(data) > 256 * 1024:
        return
    SCHEMA.write_bytes(data)
    try:
        model = MXSDC.parse(SCHEMA, max_schema_complexity=512)
        MXSDC.generate("fuzz", model)
    except (OSError, MXSDC.SchemaError):
        pass


def main():
    atheris.Setup(sys.argv, test_one_input)
    atheris.Fuzz()


if __name__ == "__main__":
    main()
