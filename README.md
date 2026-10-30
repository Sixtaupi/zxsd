# zxsd — XSD compiler and bounded C validator

[![Build and test][build-badge]][build-workflow]
[![Documentation][docs-badge]][documentation]
[![OpenSSF Scorecard][scorecard-badge]][scorecard-report]
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/16087/badge)](https://www.bestpractices.dev/projects/16087)
[![PyPI](https://img.shields.io/pypi/v/zxsdc.svg)](https://pypi.org/project/zxsdc/)
[![License](https://img.shields.io/github/license/Sixtaupi/zxsd.svg)](LICENSE)

[build-badge]: https://github.com/Sixtaupi/zxsd/actions/workflows/build.yml/badge.svg
[build-workflow]: https://github.com/Sixtaupi/zxsd/actions/workflows/build.yml
[docs-badge]: https://github.com/Sixtaupi/zxsd/actions/workflows/documentation.yml/badge.svg
[documentation]: https://sixtaupi.github.io/zxsd/
[scorecard-badge]: https://api.scorecard.dev/projects/github.com/Sixtaupi/zxsd/badge
[scorecard-report]: https://scorecard.dev/viewer/?uri=github.com/Sixtaupi/zxsd

zxsd compiles a supported XML Schema (XSD) subset into immutable C descriptors.
The target application links those descriptors with a portable C99 runtime to
validate XML without parsing the schema at runtime.

- `zxsdc` runs on the build host and generates deterministic C source and headers.
- `libzxsd` validates complete documents, raw byte fragments, or parser events.
- Optional generated bindings read and write typed C structures using caller-owned storage.
- Resource limits are fixed at compile time; zero-heap builds are supported.

Python, filesystem access, networking, and an XSD parser are never required on
the target.

> **Status:** draft/experimental. zxsd implements a documented XSD 1.0 subset,
> not the complete specification. Unsupported schema constructs fail explicitly.
> During 0.x, upgrades require rebuilding the runtime and generated code.

[Documentation](https://sixtaupi.github.io/zxsd/) ·
[XSD support matrix](SUPPORT.md) ·
[Getting started](doc/getting-started.rst) ·
[Typed bindings](doc/bindings.rst) ·
[Zephyr](doc/zephyr.rst) ·
[Compatibility](doc/compatibility.rst)

## Quick start

Generate a schema-specific validator:

```sh
python3 zxsdc tests/device.xsd -o generated
```

The generated header exposes a validation entry point:

```c
#include "device.zxsd.h"

zxsd_result_t result = device_validate(xml);
if (result.code != ZXSD_OK) {
    /* result.message, result.offset and result.path describe the error. */
}
```

The installed CMake package can generate and compile the validator as part of
the application build:

```cmake
find_package(zxsd CONFIG REQUIRED)

add_executable(application main.c)
zxsd_generate(TARGET application SCHEMA device.xsd)
target_link_libraries(application PRIVATE zxsd::zxsd)
```

Add `BINDINGS` to `zxsd_generate()` to generate typed C structures and
read/write functions. The host compiler is also available as
`python -m pip install zxsdc`. See [Getting started](doc/getting-started.rst)
for source builds, installation, and non-CMake integration.

## What it validates

The supported subset includes:

- simple and complex types, derivation, lists, unions, and facets;
- sequences, choices, `all`, groups, occurrences, and wildcards;
- namespaces, schema composition, substitution groups, and recursive types;
- `xsi:nil`, `xsi:type`, defaults, fixed values, and abstract declarations;
- `unique`, `key`, `keyref`, and the restricted XSD identity XPath language;
- a bounded XSD regular-expression subset with Unicode categories and blocks.

The exact contract, including partial and unsupported features, is maintained
in [SUPPORT.md](SUPPORT.md). Compiler acceptance alone is not treated as
conformance.

## Runtime modes

- **Document validation:** validate a NUL-terminated XML document with one call.
- **Incremental XML:** feed arbitrary byte fragments from a socket, file, or HTTP client.
- **Event validation:** send expanded start, attribute, text, and end events from another parser.
- **Typed bindings:** validate and materialize selected schemas into generated C structures.
- **Streaming bindings:** receive selected typed subtrees while validating the document once.

All modes use the same generated schema descriptors. Inputs that exceed a
configured depth, text, identity, model, or input limit fail with a structured
error instead of growing memory without a bound.

## Embedded use

The runtime is portable C99 and is tested on Linux, macOS, Windows, and Zephyr.
`ZXSD_NO_HEAP=ON` removes standard-library allocation; schemas with identity
constraints can use an explicit application allocator. `zxsdc --zephyr-config`
generates Kconfig recommendations and compile-time limit checks from the schema.

Zephyr uses the allocation-free XML backend by default. Mini-XML 4 is an
optional adapter for validating an existing Mini-XML tree, on the host or on
Zephyr when a Mini-XML module provides `CONFIG_MXML`.

See the [host sample](samples/host), [Zephyr samples](samples/zephyr), and
[resource-usage guide](doc/resource-usage.md) for complete integrations.

## Conformance

Project-owned positive and negative tests gate every documented feature. An
optional W3C XSD 1.0 harness inventories the official suite while keeping
`passed`, `failed`, `unsupported`, `expected-failure`, and `skipped`
results distinct.

See [Conformance and test coverage](doc/conformance.rst).

zxsd is licensed under [Apache-2.0](LICENSE).

## Assistance

Development assisted by GPT-5.6 Sol.
