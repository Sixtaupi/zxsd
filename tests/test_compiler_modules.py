#!/usr/bin/env python3
"""Verify the modular compiler API and extensionless launcher facade."""

import importlib.machinery
import importlib.util
import pathlib
import sys

project = pathlib.Path(sys.argv[1]).resolve()
sys.path.insert(0, str(project))

import zxsdc_lib  # noqa: E402

loader = importlib.machinery.SourceFileLoader("zxsdc_launcher_test", str(project / "zxsdc"))
spec = importlib.util.spec_from_loader(loader.name, loader)
launcher = importlib.util.module_from_spec(spec)
loader.exec_module(launcher)

assert launcher.VERSION == zxsdc_lib.VERSION
assert launcher.ABI_VERSIONS == zxsdc_lib.ABI_VERSIONS
assert launcher.SchemaError is zxsdc_lib.SchemaError
assert launcher.parse is zxsdc_lib.parse
assert launcher.generate is zxsdc_lib.generate

model = zxsdc_lib.parse(project / "tests/device.xsd")
header, source = zxsdc_lib.generate("device", model)
assert "device_validate" in header
assert "schema_device" in source
