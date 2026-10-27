#!/usr/bin/env python3
"""Verify the modular compiler API and extensionless launcher facade."""

import importlib.machinery
import importlib.util
import pathlib
import sys

project = pathlib.Path(sys.argv[1]).resolve()
sys.path.insert(0, str(project))

import mxsdc_lib  # noqa: E402

loader = importlib.machinery.SourceFileLoader("mxsdc_launcher_test", str(project / "mxsdc"))
spec = importlib.util.spec_from_loader(loader.name, loader)
launcher = importlib.util.module_from_spec(spec)
loader.exec_module(launcher)

assert launcher.VERSION == mxsdc_lib.VERSION
assert launcher.ABI_VERSIONS == mxsdc_lib.ABI_VERSIONS
assert launcher.SchemaError is mxsdc_lib.SchemaError
assert launcher.parse is mxsdc_lib.parse
assert launcher.generate is mxsdc_lib.generate

model = mxsdc_lib.parse(project / "tests/device.xsd")
header, source = mxsdc_lib.generate("device", model)
assert "device_validate" in header
assert "schema_device" in source
