#!/usr/bin/env python3
"""Atheris entry point for XSD parsing and C descriptor generation."""

import importlib.machinery
import importlib.util
import pathlib
import sys
import tempfile

import atheris

ROOT = pathlib.Path(__file__).resolve().parents[1]


def load_compiler():
    """Load the extensionless mxsdc script as an instrumented Python module."""
    loader = importlib.machinery.SourceFileLoader("mxsdc_fuzz_module", str(ROOT / "mxsdc"))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


with atheris.instrument_imports():
    MXSDC = load_compiler()

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
