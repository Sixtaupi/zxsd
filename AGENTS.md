# AGENTS.md

This file applies to the entire repository. It is the working guide for human
contributors and coding agents. Keep it synchronized with the build, tests,
and repository layout when those change.

## Project purpose

mxsd is an Apache-2.0, C99 XML Schema validator aimed at embedded systems.
`mxsdc` compiles the supported XSD 1.0 subset on the host into immutable C
descriptor tables. A generic C runtime interprets those descriptors on Linux,
macOS, Windows, and Zephyr.

The project does **not** claim complete XSD 1.0 support. `README.md`,
`SUPPORT.md`, and `doc/xsd-support.rst` are the public support contract.
Unsupported schema constructs must fail explicitly in `mxsdc`; never accept a
construct by silently ignoring it or describe compiler acceptance alone as
conformance.

## Source of truth

- `SPEC.md`: feature milestones and intended behavior.
- `README.md`: concise user-facing capabilities and examples.
- `SUPPORT.md` and `doc/xsd-support.rst`: supported, partial, and unsupported
  XSD features.
- `include/mxsd.h`: stable public runtime API and resource-limit definitions.
- `include/mxsd_version.h` and `doc/compatibility.rst`: compatibility epochs,
  release guarantees, and migration rules.
- `tests/conformance/manifest.json`: feature-to-test conformance mapping.
- `zephyr/Kconfig`: Zephyr configuration contract.

`mxsdc --zephyr-config` derives sizing recommendations from the compiler model.
Keep its Kconfig ranges and incremental-model classification synchronized with
`zephyr/Kconfig`, `stream_incremental_model()`, and `mxsd_model_supported()` when
any changes. Input
budgets must remain explicitly distinct from schema-derived requirements.

When changing behavior, update the implementation, tests, support matrix, and
documentation together. Do not let these descriptions drift apart.

## Repository layout

- `mxsdc`: Python host compiler and C code generator.
- `include/`: public, installed C headers.
- `src/mxsd.c`: public validation facade and resource checks.
- `src/mxsd_version.c`: runtime version, configuration checks, and ABI link guards.
- `src/mxsd_dom.c`: document parsing, namespaces, elements, and content models.
- `src/mxsd_value.c`: simple datatypes, whitespace, and facets.
- `src/mxsd_regex.c`: bounded XSD regular-expression parser and matcher.
- `src/mxsd_unicode.c`: generated Unicode category and block tables.
- `src/mxsd_identity.c`: `unique`, `key`, `keyref`, and restricted XPath.
- `src/mxsd_stream.c`: bounded event-driven validator.
- `src/mxsd_model.c`: shared bounded continuation pool for nested content models.
- `src/mxsd_xml_stream.c`: incremental raw-XML tokenizer feeding the streaming validator.
- `src/mxsd_bind.c`: typed binding reader and writer for `mxsdc --bindings`.
- `src/mxsd_bind_stream.c`: single-pass typed subtree callbacks for `--stream-bind`.
- `src/mxsd_bind_internal.h`: shared private arena and scalar-conversion helpers.
- `src/mxsd_xml_builtin.c`: portable allocation-free XML backend.
- `src/mxsd_xml_mxml.c`: optional Mini-XML 4 adapter.
- `src/mxsd_internal.h` and `src/mxsd_xml.h`: private runtime contracts.
- `tests/`: compiler, runtime, conformance, Zephyr Twister, and optional W3C tests.
- `tests/benchmark/`: opt-in comparisons; `streaming/` is the paired typed-binding
  protocol, with shared value consumption, memory/latency measurements, and flag audits.
- `fuzz/`: libFuzzer and Atheris targets with bounded seed corpora.
- `samples/host/`: cross-platform CMake example.
- `samples/zephyr/`: Zephyr examples: `basic` ztest suite plus `bindings`, `rest`, and
  interactive `weather` applications.
- `zephyr/`: external-module CMake, Kconfig, and metadata.
- `doc/`: Sphinx guide and Doxygen API configuration.
- `.github/workflows/`: format, build, conformance, CodeQL, and documentation CI.

Keep public declarations in `include/`; do not expose private runtime helpers.
Generated validators must depend only on the public API.

Review runtime, descriptor, and binding epochs for public layout or generated
contract changes. Incompatible changes require the affected epoch increments,
migration notes, and a minor (not patch) release during 0.x. Keep embedded
compiler/header release metadata synchronized with CMake. New public limit
macros must be included in the runtime configuration stamp and its tests.

## Build and test

Use an out-of-tree build. The portable backend is the deterministic default
for local validation and CI-style checks:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DMXSD_USE_MXML=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Also exercise zero-heap changes when they affect allocation, identity
constraints, parsing, limits, or generated descriptors:

```sh
cmake -S . -B build-no-heap \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMXSD_USE_MXML=OFF \
  -DMXSD_NO_HEAP=ON
cmake --build build-no-heap --parallel
ctest --test-dir build-no-heap --output-on-failure
```

Build the host sample with:

```sh
cmake -S samples/host -B build-sample -DMXSD_USE_MXML=OFF
cmake --build build-sample --parallel
ctest --test-dir build-sample --output-on-failure
```

On multi-configuration generators, add `--config Release` to build commands
and `-C Release` to CTest commands.

The host `mxsd_model_limits_test` compiles its own `mxsd_model.c` with the private
force-included `tests/model_limits_config.h` (eight pool slots). Keep that override
on both test translation units, independent of global flags and the normal runtime;
do not replace it with an order-sensitive MSVC `/U`/`/D` pair.

For Zephyr, run the dedicated validation tests from an initialized Zephyr
environment:

```sh
west twister -T tests/zephyr -p qemu_x86 --inline-logs
```

Do not require the W3C suite for ordinary offline builds. It is downloaded and
licensed separately; follow `tests/w3c/README.md` when a change needs it.

## Generated code

Generate a validator with:

```sh
python3 mxsdc schema.xsd -o generated
```

Generation must be deterministic. `tests/test_compiler.py` runs the compiler
twice and compares the result byte-for-byte with `tests/expected/`.

If an intentional generator change alters the golden files, regenerate them
from `tests/device.xsd`, inspect the complete diff, and commit the compiler,
tests, and golden outputs together:

```sh
python3 mxsdc tests/device.xsd -o tests/expected --bindings
```

Never hand-edit `tests/expected/*.mxsd.[ch]` or `*.bind.[ch]` to conceal a
generator bug. Generated files in build directories are disposable and must not be
committed.

## Coding conventions

Follow `.editorconfig` and `.clang-format`.

- C is portable C99, uses Zephyr/Linux-style formatting, tabs for indentation,
  and a 100-column limit.
- Avoid compiler extensions, POSIX-only APIs, variable-length arrays, hidden
  filesystem/network access, and assumptions that exclude Windows.
- Keep resource use bounded. Do not allocate in proportion to untrusted
  `maxOccurs` or input values without an explicit configured ceiling.
- Return an existing structured `mxsd_error_t` result for malformed input and
  limit violations. Do not abort or print from the library.
- Preserve the first-error/sticky-result behavior of the streaming API.
- Treat namespace URI plus local name as the identity of XML names; prefixes
  are lexical aliases, not identities.
- Public C declarations and all non-obvious internal functions need Doxygen
  comments in the existing Zephyr-like style.
- Python uses four spaces, a 100-column limit, type-oriented data structures
  where helpful, and Black formatting. Keep `mxsdc` deterministic and free of
  implicit network access.
- CMake, Kconfig, YAML, reStructuredText, and Markdown must follow the
  indentation rules in `.editorconfig`.

Run the format checks used by CI with:

```sh
scripts/check-format.sh
```

Pass `--fix` to format the files in place.

Use the formatter to fix only files relevant to the change; do not mix broad
unrelated reformatting into functional work.

## Testing expectations

Every behavior change needs focused tests:

- compiler parsing, resolution, or diagnostics: extend `tests/test_compiler.py`;
- runtime validation: add positive and negative cases to the closest
  `tests/test_*.c` and schema fixture;
- a documented XSD feature: update `tests/conformance/manifest.json` and the
  support documentation;
- streaming behavior: test event ordering, sticky errors, and fixed limits;
- resource-limit behavior: test the exact boundary and the first rejected
  value;
- Zephyr configuration or integration: build the `samples/zephyr` tests;
- public API changes: update Doxygen comments, Sphinx API guidance, and samples.

Do not turn an unsupported W3C case into a pass by reclassifying or skipping it
without documenting the reason. Keep `passed`, `failed`, `unsupported`,
`expected-failure`, and `skipped` semantically distinct.

## Documentation

Doxygen is the source of truth for declarations; Sphinx provides the narrative
guide. Build both in strict mode after documentation or public API changes:

```sh
python3 -m venv .venv-docs
. .venv-docs/bin/activate
python -m pip install -r doc/requirements.txt
make -C doc clean html SPHINXOPTS="-W --keep-going"
```

The outputs are `doc/_build_doxygen/html` and `doc/_build_sphinx/html`. Both are
ignored build artifacts. The documentation CI publishes the combined site on
pushes to `main` and validates it on pull requests.

## Change discipline

- Preserve API and generated-code compatibility unless the task explicitly
  requires a breaking change.
- Keep `mxsd.c` as a small public facade; place implementation logic in the
  focused runtime module that owns it.
- Do not add a full DOM, regex, XPath, network, or filesystem dependency to the
  target runtime.
- Do not fetch remote schemas implicitly. Includes/imports resolve locally on
  the host through explicit paths or resolver callbacks.
- Do not weaken configured limits to make a test pass.
- Avoid checking in build trees, downloaded W3C data, virtual environments,
  caches, or generated documentation.
- Preserve unrelated user changes in a dirty working tree.

Before handing off a change, report which build/test/documentation commands
were run and call out any platform or optional suite that was not available.

For performance comparisons, keep validation/extracted values, fragment sizes,
target/compiler options (including external XSD/e runtime options), and common
resource ceilings aligned. Keep XML measurements separate from HTTP/TLS; do not
call virtual QEMU rates physical-MCU throughput or ROM payload physical Flash.
