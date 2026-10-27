# Copyright (c) 2026 mxsd contributors
# SPDX-License-Identifier: Apache-2.0

"""Public Python API for the mxsd host compiler."""

from .bindings import BindingError, generate_bindings
from .cli import main
from .codegen import abi_checks, abi_guard, generate
from .common import (
    ABI_VERSIONS,
    MAX_SCHEMA_COMPLEXITY,
    VERSION,
    InvalidSchemaError,
    SchemaError,
    ident,
)
from .loader import default_resolver, load_documents
from .schema import parse
from .zephyr import generate_zephyr_config

__all__ = [
    "ABI_VERSIONS",
    "BindingError",
    "InvalidSchemaError",
    "MAX_SCHEMA_COMPLEXITY",
    "SchemaError",
    "VERSION",
    "abi_checks",
    "abi_guard",
    "default_resolver",
    "generate",
    "generate_bindings",
    "generate_zephyr_config",
    "ident",
    "load_documents",
    "main",
    "parse",
]
