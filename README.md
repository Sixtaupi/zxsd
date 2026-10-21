# mxsd - Mini-XSD class generator / validator

# Draft / Experimental: not production ready

`mxsd` compiles a small, deliberately strict subset of XML Schema into
deterministic C validation code for embedded applications. XSD is processed on
the host only; the target links `libmxsd` and the generated files.

## Supported platforms

| Platform | `mxsdc` host compiler | C runtime | Continuous integration |
|---|:---:|:---:|:---:|
| Linux | Yes | Static and shared | Yes |
| macOS | Yes | Static and shared | Yes |
| Windows | Yes | Static and shared | Yes |
| Zephyr | Build-host only | Embedded module, zero-heap capable | `qemu_x86` |

A C99 compiler and CMake 3.16 or newer are required. Python 3 is required to
run `mxsdc` and the tests, but not by the target runtime. A complete portable
example is available in [`samples/host`](samples/host), and the Zephyr module
examples are in [`samples/zephyr`](samples/zephyr): a validation test suite in
[`basic`](samples/zephyr/basic) and typed bindings in
[`bindings`](samples/zephyr/bindings).

```sh
cmake -S . -B build -DMXSD_USE_MXML=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Windows, invoke the extensionless compiler script through Python:

```powershell
python .\mxsdc schema.xsd -o generated
```

Set `MXSD_BUILD_TESTS=OFF` for a runtime-only cross build. Set
`BUILD_SHARED_LIBS=ON` to build a shared library; the default is static.

## XSD support and conformance

The detailed support matrix and its exact limitations are maintained in the
[XSD 1.0 support documentation](doc/xsd-support.rst). Test methodology,
result classifications, and W3C suite integration are documented in
[Conformance and test coverage](doc/conformance.rst).

```sh
./mxsdc device.xsd -o generated
cc -Iinclude -Isrc -Igenerated \
  src/mxsd.c src/mxsd_dom.c src/mxsd_value.c src/mxsd_identity.c \
  src/mxsd_regex.c src/mxsd_unicode.c src/mxsd_stream.c \
  src/mxsd_xml_builtin.c generated/device.mxsd.c app.c
```

```c
#include "device.mxsd.h"
mxsd_result_t r = device_validate(xml);
if (r.code != MXSD_OK) { /* r.message, r.offset and r.path */ }
```

The compiler implements the supported portions of the v0.1-v0.20 milestones
in `SPEC.md`: type
derivation, simple/complex content, inheritance, abstract types, lists,
unions, the XSD 1.0 simple datatype family, whitespace normalization,
content models, model and attribute groups, declaration references,
namespace/QName handling, recursive occurrence
models, wildcards, identity constraints, schema composition, substitution
groups, recursive named complex types, bounded XSD regular expressions,
notation declarations, mixed
content, `xs:anyType`, advanced
instance semantics, and bounded event-driven validation. Unknown constructs and properties
are errors. The portable XML reader has no filesystem, network,
or POSIX dependency. Whitespace normalization uses a temporary buffer whose
size is proportional to the simple value being checked. Limits such as
`MXSD_MAX_INPUT`, `MXSD_MAX_DEPTH`,
`MXSD_MAX_PARTICLES`, and `MXSD_MAX_ATTRIBUTES` are compile-time configurable.

The runtime is split into focused implementation units:

| Source | Responsibility |
|---|---|
| `src/mxsd.c` | Public validation facade, allocators, and resource checks |
| `src/mxsd_dom.c` | XML parsing, namespaces, elements, attributes, and content models |
| `src/mxsd_value.c` | Simple datatypes, whitespace, patterns, and facets |
| `src/mxsd_regex.c` | Bounded XSD regular-expression parser and matcher |
| `src/mxsd_unicode.c` | Generated Unicode 16 category and block tables |
| `src/mxsd_identity.c` | Bounded `unique`, `key`, `keyref`, and restricted XPath processing |
| `src/mxsd_stream.c` | Fixed-memory event-driven validation |
| `src/mxsd_bind.c` | Typed binding reader and writer for `mxsdc --bindings` output |
| `src/mxsd_internal.h` | Private contracts shared by runtime modules |

The same generated descriptor can validate a complete string or an event
stream. Generated headers provide `<schema>_stream_init()` for the default
root and one initializer per global root:

```c
mxsd_stream_t stream;
sensor_stream_init(&stream);
mxsd_start_element(&stream, NULL, "sensor");
mxsd_attribute(&stream, NULL, "id", "12");
mxsd_start_content(&stream);
/* Send child start/text/end events here. */
mxsd_end_element(&stream, NULL, "sensor");
mxsd_result_t result = mxsd_stream_finish(&stream);
```

Event names are expanded names: pass the namespace URI separately from the
local name, and decoded text/attribute values. Resolve `xsi:type` QName values
in the XML frontend and call `mxsd_xsi_type()`. Streaming state is fixed-size;
text for one simple element is bounded by `MXSD_STREAM_TEXT_CAPACITY` and names
by `MXSD_STREAM_NAME_CAPACITY`.

## Typed bindings

`mxsdc --bindings` also generates C structures and read/write functions for
every global element with a complex type. Reading validates the document, then
fills the structure using a caller-provided arena; writing produces a valid
document from the structure. Neither uses the heap.

```c
#include "device.bind.h"

static unsigned char storage[2048];
mxsd_bind_arena_t arena;
device_Device_t device;

mxsd_bind_arena_init(&arena, storage, sizeof(storage));
if (device_device_read(xml, &device, &arena).code == MXSD_OK) {
    /* device.id, device.name, device.sample[0 .. device.sample_count - 1] */
}

char out[256];
mxsd_result_t r = device_device_write_buffer(&device, out, sizeof(out), NULL);
```

Integers and booleans use native C types, string enumerations become C enums,
and other values stay as their normalized lexical text. Constructs without a
faithful C representation, such as wildcards or mixed content, make
generation fail instead of being dropped. See
[Typed bindings](doc/bindings.rst).

Resource-constrained builds can set `MXSD_NO_HEAP=ON`, override the
`MXSD_MAX_*` compile-time limits, and use `mxsd_validate_with_allocator()` for
application-owned identity-constraint scratch storage. `mxsdc` also accepts
`--max-schema-complexity`. Measured Cortex-M usage and a reproducible reporting
tool are documented in [`doc/resource-usage.md`](doc/resource-usage.md).

The Apache-2.0 conformance manifest in `tests/conformance` executes positive
and negative test groups and generates `conformance-report.json`. Its report
keeps supported, unsupported, expected-failure, passed, and failed entries
separate; compiler acceptance alone never promotes a feature to supported.
The optional official W3C XSD 1.0 harness in [`tests/w3c`](tests/w3c) inventories
every published test and executes accepted schema and instance cases. It keeps
unsupported behavior separate from failures and does not download data during
normal CMake builds.

Identity constraints use the complete XSD 1.0 restricted XPath subset, including
path unions, compiled into fixed descriptors; no general XPath engine is linked. Tuple storage is
bounded by `MXSD_MAX_IDENTITY_VALUES`, `MXSD_MAX_IDENTITY_FIELDS`, and
`MXSD_MAX_IDENTITY_CONSTRAINTS`.

Schema composition is explicit and local. Relative `schemaLocation` values are
resolved from the referring schema. Additional local registries can be supplied
with `-I`; imports without `schemaLocation` use the namespace converted to a
safe filename such as `urn_example.xsd`. The Python `parse()` entry point also
accepts a resolver callback with the signature
`resolver(namespace, location, referring_path)`. The default resolver rejects
HTTP and other network URLs, so `mxsdc` never performs implicit network access.

Mini-XML 4 is selected automatically when its headers and library are found;
set `MXML_INCLUDE_DIR` and `MXML_LIBRARY` if it is installed outside the normal
search paths. CI and packaging checks can set `MXSD_REQUIRE_MXML=ON` to reject
an accidental fallback to the portable backend. Zephyr uses the allocation-free
embedded backend. Both sit behind the same internal boundary so a SAX backend
does not affect generated APIs.
When enabled, `<mxsd_mxml.h>` provides `mxsd_mxml_validate()` for an existing
Mini-XML document or element tree; pass the generated `<schema>_schema()`
descriptor accessor.

## Continuous integration

GitHub Actions runs seven independent workflows:

- `Code format` checks the Zephyr C style with clang-format and Python with Black;
- `Build and test` builds static and shared libraries on Linux, macOS, and Windows,
  exercises the host sample, checks the no-heap configuration, runs ASan and
  UBSan, requires and tests the Mini-XML 4 adapter, and runs the Zephyr samples
  on `qemu_x86`;
- `Conformance` gates the internal positive/negative suite and archives W3C XSD
  1.0 reports. Pull requests run a 500-case smoke test; scheduled and manual
  runs execute every accepted W3C case;
- `CodeQL` analyzes both C/C++ and Python on pushes, pull requests, and a weekly
  schedule;
- `Fuzz smoke tests` exercises the portable XML validator with libFuzzer plus
  ASan/UBSan and instruments `mxsdc` with Atheris;
- `Documentation` builds Doxygen and Sphinx in strict mode;
- `Python package` builds and verifies the wheel and source distribution before
  trusted publishing to PyPI.

W3C discrepancies are currently reported and archived without failing the
workflow because the documented XSD subset is not yet fully W3C-conformant.
The internal conformance suite remains a required failing gate.
