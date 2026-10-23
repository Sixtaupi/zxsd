# mxsd versus CodeSynthesis XSD/e benchmark

For an audited comparison extracting identical values through **typed streaming
bindings**, use the [new paired protocol](streaming/README.md). It measures XML
cycles, time to first typed result, throughput, observed stack/heap, and linked
memory with matched compiler options, including the external XSD/e runtime.
The validation-only and serialization measurements below are historical,
separate scenarios; they must not be labeled as binding-streaming performance.

This opt-in benchmark compares mxsd with the XSD/e `C++/Parser` mapping. Both
programs validate the same deterministic XML file against
[`benchmark.xsd`](benchmark.xsd), process it as a stream, discard typed values,
and exit with a failure status if validation fails.

It is intentionally separate from the normal test suite. It never downloads
XSD/e and does not make XSD/e a build or CI dependency. Obtain and license
XSD/e separately from CodeSynthesis.

## Build mxsd and the fixture

```sh
cmake -S tests/benchmark -B build-benchmark -DCMAKE_BUILD_TYPE=Release
cmake --build build-benchmark --parallel
```

The default fixture contains 1024 samples and is approximately 120 KiB. To
generate a different deterministic size:

```sh
python3 tests/benchmark/generate_fixture.py build-benchmark/benchmark.xml \
  --samples 4096
```

The mxsd build configures `MXSD_STREAM_CHILD_CAPACITY=1`: this schema uses
incremental content-model counters, so the 1024 children do not need a token
history. Nested models can use a bounded continuation pool; this benchmark does not
measure them. The host configuration uses `MXSD_MAX_PARTICLES=64` to exercise
the real MET Norway schema's 45-term location sequence as well.

Run the offline weather regression with:

```sh
ctest --test-dir build-benchmark --output-on-failure
```

It feeds 512 forecasts (132703 bytes) in seven-byte fragments with no heap
and history capacity 1, including repeated optional location sequences. It
also checks rejection of an invalid temperature attribute.

## Build the XSD/e runner

First configure and build the XSD/e runtime with XML Schema validation enabled.
Use a release configuration compatible with the flags used for mxsd. Then run:

```sh
python3 tests/benchmark/build_xsde.py \
  --xsde /path/to/xsde \
  --runtime-include /path/to/xsde/libxsde \
  --runtime-library /path/to/xsde/libxsde/xsde/libxsde.a \
  --output-dir build-benchmark/xsde
```

`--runtime-include` is the directory containing the `xsde/` include tree. It
can be repeated when an out-of-tree XSD/e build stores generated `config.h` or
version headers in a separate include tree. Use `--cxxflags` and `--ldflags`
if the selected XSD/e configuration requires additional compiler definitions
or system libraries. The helper compiles all sources produced by
`--generate-noop-impl --generate-test-driver`; generated files remain in the
build directory and must not be committed.

For example, a build2 source checkout normally needs both trees:

```sh
python3 tests/benchmark/build_xsde.py \
  --xsde /tmp/xsde-host/xsde/xsde/xsde \
  --runtime-include /tmp/xsde-target/libxsde \
  --runtime-include /tmp/xsde/libxsde \
  --runtime-library /tmp/xsde-target/libxsde/xsde/libxsde.a \
  --output-dir build-benchmark/xsde
```

## Run

```sh
python3 tests/benchmark/run.py \
  --mxsd build-benchmark/mxsd-benchmark \
  --xsde build-benchmark/xsde/xsde-benchmark \
  --input build-benchmark/benchmark.xml \
  --output build-benchmark/results.json
```

The report contains the input and executable sizes, the output of the host
`size` tool when available, peak resident memory measured by `/usr/bin/time`,
and raw, minimum, median, and maximum wall times. Process startup and file I/O
are included equally for both programs. Use enough input and repeated runs to
make their contribution small.

For embedded conclusions, rebuild both runners with the same cross-toolchain
and optimization flags and additionally record linked Flash, static RAM,
maximum stack, and allocator high-water marks. Do not compare a complete
Zephyr networking application with XSD/e's minimum-runtime footprint.

## Zephyr benchmark

The two applications under `zephyr/` run validation and serialization on the
same generated fixture, in 20 iterations, and report average target cycles.
Build and run mxsd with:

```sh
west build -b qemu_cortex_a53 tests/benchmark/zephyr/mxsd \
  -d build-benchmark/zephyr-mxsd --pristine
west build -d build-benchmark/zephyr-mxsd -t run
```

Build the XSD/e static runtime for the Zephyr AArch64 toolchain with STL,
iostreams, and exceptions disabled, and parser/serializer validation plus
regular expressions enabled. Then provide its host compiler, source tree,
generated build headers, and static library explicitly:

```sh
west build -b qemu_cortex_a53 tests/benchmark/zephyr/xsde \
  -d build-benchmark/zephyr-xsde --pristine -- \
  -DXSDE_COMPILER=/path/to/host/xsde \
  -DXSDE_SOURCE_DIR=/path/to/xsde/libxsde \
  -DXSDE_BUILD_DIR=/path/to/xsde-target/libxsde \
  -DXSDE_LIBRARY=/path/to/xsde-target/libxsde/xsde/libxsde.a
west build -d build-benchmark/zephyr-xsde -t run
```

The mxsd application uses `CONFIG_MXSD_NO_HEAP=y`. The XSD/e application uses
a 256 KiB libc arena because the default XSD/e runtime performs allocations;
therefore its linked RAM total is informative but is not a like-for-like
minimum-memory result. Raw QEMU target cycles are suitable for comparing these
two runs; host wall time and cycle-to-nanosecond conversions are not.

To check scaling at 128, 256, 512, 1024, and 2048 samples:

```sh
python3 tests/benchmark/run_zephyr_scaling.py \
  --build-dir build-benchmark/zephyr-scaling \
  --output build-benchmark/zephyr-scaling.json
```

The script rebuilds that dedicated directory, runs QEMU, captures all three
measurements, and exits QEMU automatically. Pass `--west /path/to/west` when
it is not in `PATH`. Each operation has one warm-up and 20 measured iterations.
The script fails if the maximum/minimum per-sample cost differs by more than
50%, leaving the JSON report for inspection. This is an opt-in performance
check; ordinary functional tests do not depend on wall-clock thresholds.

The recorded [incremental baseline](results/qemu_cortex_a53-incremental.json)
uses Zephyr v4.5.0-rc3, SDK 1.0.0, GCC 14.3.0 and `-Os`:

| Samples | Validation cycles | Validated serialization cycles |
|---:|---:|---:|
| 128 | 6989117 | 5026174 |
| 256 | 13842480 | 9998534 |
| 512 | 27549206 | 19943196 |
| 1024 | 54962391 | 39821017 |
| 2048 | 109952392 | 79654433 |

The stream context is 12280 bytes at every size; the complete 1024-sample
firmware occupies 764 KiB of linked RAM, including the kernel, stacks,
embedded fixture and application arrays. QEMU uses an instruction-count
virtual clock: these readings are useful for regression comparisons under
the same settings, but do not predict execution time on a physical Cortex-A53.

## Serialization benchmark

The serialization benchmark creates the same 1024 typed samples in each
program and serializes them with schema validation enabled. Both serializers
write to a counting callback, so output buffering and filesystem performance
are excluded. XSD/e is configured to use the same `ns1` prefix as mxsd; the
resulting document sizes differ only by their XML declarations.

Build the XSD/e serializer runner with the same runtime used above:

```sh
python3 tests/benchmark/build_xsde_serializer.py \
  --xsde /tmp/xsde-host/xsde/xsde/xsde \
  --runtime-include /tmp/xsde-target/libxsde \
  --runtime-include /tmp/xsde/libxsde \
  --runtime-library /tmp/xsde-target/libxsde/xsde/libxsde.a \
  --output-dir build-benchmark/xsde-serializer
```

Then run the comparison:

```sh
python3 tests/benchmark/run_serialization.py \
  --mxsd build-benchmark/mxsd-serializer-benchmark \
  --xsde build-benchmark/xsde-serializer/xsde-serializer-benchmark \
  --output build-benchmark/serialization-results.json
```

The runner alternates which implementation executes first to limit bias from
CPU frequency changes and cache warm-up.

## Scope and limitations

- This measures validating event-driven parsing, not mxsd typed bindings or
  XSD/e Hybrid object-model construction.
- The generated XSD/e no-op implementation performs schema-aware parsing but
  intentionally does not retain application values, matching the mxsd runner.
- Binary file size is not linked Flash. Use the target linker map or `size` for
  that comparison.
- Results are meaningful only when validation, compiler options, architecture,
  XML backend, and input are recorded together.
