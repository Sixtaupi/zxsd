# Contributing to mxsd

Thank you for helping improve mxsd. The project targets embedded systems, so
correctness, explicit limits, portability, and predictable resource use take
priority over accepting more XML Schema constructs.

## Before opening a change

- Check [SUPPORT.md](SUPPORT.md) and [doc/xsd-support.rst](doc/xsd-support.rst)
  before changing schema behavior.
- Open an issue first for a public API break, generated-code compatibility
  change, new dependency, or substantial XSD feature.
- Report security issues privately as described in [SECURITY.md](SECURITY.md).
- Keep changes focused and preserve unrelated work in the tree.

## Development setup

The default local build uses the deterministic portable XML backend:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DMXSD_USE_MXML=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Changes involving allocation, parsing, identity constraints, limits, or
generated descriptors must also exercise the zero-heap configuration:

```sh
cmake -S . -B build-no-heap \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMXSD_USE_MXML=OFF \
  -DMXSD_NO_HEAP=ON
cmake --build build-no-heap --parallel
ctest --test-dir build-no-heap --output-on-failure
```

Build the standalone host sample with:

```sh
cmake -S samples/host -B build-sample -DMXSD_USE_MXML=OFF
cmake --build build-sample --parallel
ctest --test-dir build-sample --output-on-failure
```

Use `--config Release` and `-C Release` with multi-configuration generators.
Zephyr validation changes should run the Twister tests from an initialized
Zephyr workspace:

```sh
west twister -T tests/zephyr -p qemu_x86 --inline-logs
```

Samples are applications, not the test suite; build the relevant sample
separately when changing its integration path.

## Change requirements

- C code must remain portable C99 and follow `.clang-format` and
  `.editorconfig`.
- Python must remain deterministic, avoid implicit network access, follow the
  100-column limit, and be formatted with Black.
- Target-side work must stay bounded. Do not allocate in proportion to an
  untrusted input or `maxOccurs` without an explicit configured ceiling.
- Unknown or unsupported XSD constructs must produce a compiler error; never
  ignore them to improve a conformance count.
- Namespace identity is the namespace URI plus local name, not the lexical
  prefix.
- Public declarations and non-obvious internal functions need Doxygen comments.
- Public API changes require matching documentation and sample updates.

Run the formatting gate with:

```sh
scripts/check-format.sh
```

Pass `--fix` to format relevant files in place. Avoid unrelated reformatting.

## Tests and generated files

Every behavior change needs a focused positive or negative test. In particular:

- compiler diagnostics and resolution belong in `tests/test_compiler.py`;
- runtime behavior belongs in the nearest `tests/test_*.c` fixture;
- streaming changes must cover event ordering, sticky errors, and limits;
- limit changes must test the exact boundary and first rejected value;
- documented feature changes must update the conformance manifest and support
  documentation.

Generate validators with:

```sh
python3 mxsdc schema.xsd -o generated
```

If an intentional generator change affects the golden files, regenerate and
review them rather than editing them by hand:

```sh
python3 mxsdc tests/device.xsd -o tests/expected --bindings
```

Keep fuzz harnesses and corpora in `fuzz/`. A confirmed fuzzing defect should
also gain a deterministic regression test under `tests/`.

## Documentation

Behavior, tests, and public documentation must agree. Build Doxygen and Sphinx
in strict mode after documentation or public API changes:

```sh
python3 -m venv .venv-docs
. .venv-docs/bin/activate
python -m pip install -r doc/requirements.txt
make -C doc clean html SPHINXOPTS="-W --keep-going"
```

Do not commit build trees, virtual environments, documentation output,
downloaded W3C data, caches, or temporary generated files.

## Pull requests

Describe the user-visible effect, the resource or compatibility impact, and
the commands used for verification. Call out optional platforms or suites that
were not available. Small, reviewable commits are preferred, but no particular
commit style is required.
