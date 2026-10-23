# Fair typed-streaming comparison

This is the reference **XML-only extraction** benchmark. The older validation
and serialization runners remain available, but their historical ratios must
not be presented as typed-streaming results.

Both implementations validate `../benchmark.xsd` and deliver each complete
`samples/sample` subtree through typed bindings. mxsd uses the generated
`--stream-bind samples/sample` reader; XSD/e uses C++/Parser skeletons with real
native-value callbacks, not the no-op implementation or Hybrid object model.
Both consume `id` (uint32), decoded `label`, `enabled` (boolean), and `reading`
(int32) in the **same C sink**. The sink hashes canonical values including the
label length and every label byte, in document order. The report checks every
run's count/checksum against an independent Python XML/value decoder.

## Build and run on Zephyr QEMU

Prerequisites: an initialized Zephyr/Ninja environment, Python with Zephyr's
`pyelftools` dependency, build2, and an explicitly provided XSD/e source checkout
and host compiler. Nothing is downloaded by these commands. Obtain and license
XSD/e separately; generated XSD/e sources stay in build directories.

Build mxsd first. Its compilation database supplies the **actual** optimization
and CPU/ABI flags used to compile the external XSD/e runtime:

```sh
west build -b qemu_cortex_a53 tests/benchmark/zephyr/mxsd_stream \
  -d build-stream-mxsd

python3 tests/benchmark/build_stream_runtime.py \
  --source-dir /path/to/xsde/libxsde \
  --build-dir /absolute/path/to/new-xsde-runtime \
  --zephyr-build build-stream-mxsd \
  --cc /path/to/sdk/gnu/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-gcc \
  --cxx /path/to/sdk/gnu/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-g++

west build -b qemu_cortex_a53 tests/benchmark/zephyr/xsde_stream \
  -d build-stream-xsde -- \
  -DXSDE_COMPILER=/path/to/host/xsde \
  -DXSDE_SOURCE_DIR=/path/to/xsde/libxsde \
  -DXSDE_BUILD_DIR=/absolute/path/to/new-xsde-runtime \
  -DXSDE_LIBRARY=/absolute/path/to/new-xsde-runtime/xsde/libxsde.a \
  -DXSDE_RUNTIME_MANIFEST=/absolute/path/to/new-xsde-runtime/benchmark-runtime.json

python3 tests/benchmark/run_streaming.py \
  --mxsd-build build-stream-mxsd --xsde-build build-stream-xsde \
  --output build-stream-results.json
```

Use a new empty directory for the external runtime: the helper refuses to
overwrite another runtime configuration. It records compiler versions, exact
configure commands/options, build2 configuration, and the archive hash. Parser
validation and regular-expression facets are enabled; STL, iostreams, and C++
exceptions are disabled. The common profile is `-Os`, `NDEBUG`, no LTO; C++
language flags (including `-fcheck-new`) are copied too. It is not sufficient to
compile only the generated XSD/e application in `-Os` over an old `-O2` archive.

The paired runner rebuilds both applications, checks their real compilation
databases, compiler paths, QEMU CPU/virtual-clock settings, OS budgets, and fixture identity. The
runtime manifest/archive is checked too. It refuses a comparison with different flags,
fixtures, clocks, fragment sizes, iteration counts, stack ceilings, or heap
ceilings. Two boots per implementation alternate the order (A/B, B/A), each
with one warm-up and 20 measured parses. All raw iterations and console output
are retained; there is no noisy timing threshold in ordinary functional CI.

For scaling, configure **both** applications with the same parameters:

```sh
west build -d build-stream-mxsd -- -DBENCHMARK_SAMPLE_COUNT=128 -DBENCHMARK_BLOCK_SIZE=7
west build -d build-stream-xsde -- -DBENCHMARK_SAMPLE_COUNT=128 -DBENCHMARK_BLOCK_SIZE=7
```

Useful sample counts are 1, 128, 1024, and 2048; useful block sizes are 1, 7,
512, and 4096. Fixtures must stay within mxsd's explicit 1 MiB document ceiling.
Do not compare different block sizes or quietly relax resource bounds.

## What is measured

- **Total cycles:** from before parser construction/initialization through all
  feeds, final full-document validation, and disposal/destruction. The fixture
  is already available; process startup, file I/O, UART printing, and Python
  reference decoding are excluded. No full document object model is retained.
- **First result:** cycles until the first complete typed sample reaches the
  common sink, plus the end offset of the block supplied at that point. This is
  provisional delivery: only successful document finish establishes validity.
  It is not merely the arrival of the first XML token or the return of a feed.
- **Throughput:** bytes/cycle and samples/cycle, plus clock-derived MiB/s. On
  QEMU the latter is a **virtual-clock** rate, not physical MCU throughput.
- **Stack:** observed high-water mark of a fresh 32 KiB worker stack, including
  warm-up and valid measured parses. The suspended worker is inspected from
  the main thread. Reporting and negative self-tests use another stack and are
  excluded. This is not a proof of worst-case stack for arbitrary XML.
- **Heap:** global libc allocated-byte high-water mark read before reporting or
  negative self-tests, and live bytes after the parses. Both images reserve
  the same 32 KiB libc arena; mxsd still uses `MXSD_NO_HEAP`. XSD/e allocation
  and free costs remain inside its timed parse. This records the real memory
  policy difference instead of assigning XSD/e a huge arbitrary heap reserve.
- **Static storage:** adapter context/object/arena bytes, writable allocated
  ELF sections, the linker RAM symbol when available, and all allocated
  sections. XSD/e parser objects live on its measured stack, not in a hidden
  static buffer. The common input fixture is embedded read-only in both images.
- **ROM/Flash:** loadable, file-backed allocated section payload and per-section
  sizes, not the executable file's byte count. These are **complete benchmark
  firmware** footprints including Zephyr, the fixture, equal stack/heap reserves,
  reporting, and self-tests; not either library's standalone minimum. On
  `qemu_cortex_a53`, `CONFIG_XIP=n`: code/rodata also occupy RAM, and the linker
  reports zero physical Flash usage. The ROM payload predicts neither an MCU
  linker's padding nor its actual Flash occupancy. Use the MCU linker map for
  physical Flash measurements.

TLS templates are reported separately rather than counted again as standalone
RAM reservations; their runtime storage belongs to the threads.

Do **not** add observed stack/heap peaks to the static RAM totals: their
reserved storage is already included there. Keep the static reserve and
observed usage columns separate. All-memory totals under non-XIP QEMU include
the common read-only fixture; writable RAM alone is not the complete image RAM.

## HTTP/TLS separation

Neither image enables networking, sockets, or TLS. The report explicitly marks
transport measurements as **not measured**, not zero. The measured XML work is
full schema validation plus typed extraction and the shared value consumer.
It is not bare parsing without validation, and it is not a weather request.

A future network-only measurement must use the same server, TLS policy,
connection/session reuse, byte stream, and fragment schedule, draining the body
without parsing it. Report DNS/connect/TLS/HTTP/body timing separately. A live
HTTP request with parsing in its receive callback interleaves the two costs;
do not subtract a synthetic offline parse time from it and call the remainder
an isolated TLS measurement. Bare-parser and live-transport comparisons remain
separate TODO items.

## Correctness gates

Both target adapters run the same positive and negative fixtures after the
timed work: value normalization, namespaces, pattern/length facets, native
integer bounds, invalid booleans, missing fields, malformed XML, and errors
after a provisional delivery, with 1-, 7-, and 4096-byte feeds. Any failure
invalidates the report. The Python protocol tests also reject discarded values,
missing iterations, bad timing, nonmatching input/options/compilers/budgets,
modified archives/configurations, missing `NDEBUG`, LTO, and linked networking.

Run offline checks without XSD/e or Zephyr:

```sh
cmake -S tests/benchmark -B build-benchmark -DCMAKE_BUILD_TYPE=Release
cmake --build build-benchmark --parallel
ctest --test-dir build-benchmark --output-on-failure
python3 tests/test_benchmark.py
```

Historical validation-only results remain in `../results/`. Typed-streaming
measurements must identify this protocol explicitly; do not silently overwrite
or relabel the earlier baseline. A second small configuration schema, the real
weather schema on both sides, and physical-MCU measurements remain future work.

## Recorded reference

The [raw paired report](../results/qemu_cortex_a53-streaming.json) uses Zephyr
v4.5.0-rc3, SDK 1.0.0, GCC 14.3.0, and XSD/e 3.4.1 development compiler
`9ba546d26b0f`. The fixture contains 1024 samples (123228 bytes), supplied in
4096-byte blocks. Both implementations deliver checksum `3181260375`.

| Measurement | mxsd | XSD/e |
|---|---:|---:|
| Median full-parse virtual cycles | 62,087,180 | 24,992,758 |
| Median first-result virtual cycles | 381,306 | 96,531 |
| XML bytes per virtual cycle | 0.001985 | 0.004931 |
| Complete firmware ROM load payload, bytes | 262,350 | 318,263 |
| Complete firmware writable static sections, bytes | 422,085 | 402,117 |
| Adapter static context/object/arena, bytes | 22,552 | 0 (on stack/heap) |
| Observed worker stack peak, bytes | 26,208 | 3,248 |
| Observed libc heap peak, bytes | 0 | 20,736 |

For this workload, mxsd takes 2.48x the total cycles and 3.95x the first-result
cycles. It uses a smaller ROM payload and no heap allocations, but more static
storage and substantially more stack. These are complete benchmark images with
equal reserves, not minimum library footprints. ROM payload is **not physical
Flash** on this non-XIP target, and the virtual rates are not MCU measurements.
