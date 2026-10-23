Benchmark methodology
#####################

The opt-in benchmark under ``tests/benchmark`` compares mxsd and CodeSynthesis
XSD/e without making XSD/e a dependency of normal builds. It does not download
third-party sources. Obtain and license XSD/e separately.

Typed streaming, not no-op parsing
**************************************************

Use ``tests/benchmark/zephyr/mxsd_stream`` and ``xsde_stream`` for the current
paired protocol. Both validate the same XSD and XML fixture, extract every
sample's uint32 identifier, decoded label, boolean, and int32 reading, and pass
them to the same bounded C sink. mxsd uses generated typed streaming bindings;
XSD/e uses real C++/Parser callbacks. There is no retained document object model.

``tests/benchmark/run_streaming.py`` verifies each run's count and canonical
value checksum against an independent Python decoder. Both adapters also run
the same positive/negative XML checks over one-byte, seven-byte, and 4096-byte
fragments. Provisional callbacks do not establish whole-document validity:
successful finish remains mandatory.

The historical validation-only and serialization runners are separate
scenarios. Their ratios must not be relabeled as typed-binding measurements.

Fairness gates
**************

The shared Zephyr harness uses equal worker stacks, libc heap ceilings,
fragment sizes, iteration counts, clocks, and OS settings. One warm-up is
followed by 20 measured parses per boot, with A/B then B/A execution order.
Reporting takes place outside the measured worker and after heap/stack sampling.

The comparison profile uses ``-Os``, ``NDEBUG``, and no LTO. The helper
``build_stream_runtime.py`` builds the external XSD/e runtime from the real
mxsd application's compilation flags, including CPU/ABI and C++ options. The
runner audits compilation databases, compiler paths, archive/configuration
hashes, fixture identity, and measurement settings before accepting a ratio.
Parser validation and regular-expression facets must be enabled for both.

Results and their limits
**************************************************

Reports retain raw cycles, first-result timing, bytes supplied at that point,
throughput, observed worker stack and libc heap peaks, adapter static storage,
generated-source sizes/hashes, linked ELF sections, build configuration,
compiler versions, commands, and raw console output.

Total cycles include construction/initialization, all feeds, final validation,
and destruction/disposal. The first-result timestamp is taken when the first
complete typed subtree reaches the common sink, not when a feed returns.

Do not add stack/heap high-water marks to static RAM totals: their reserved
storage is already included. The stack measurement is observed usage for this
workload, not a worst-case proof. Complete firmware footprints include Zephyr,
the embedded fixture, common reserves, reporting, and self-tests; they are not
either library's standalone minimum.

Under ``qemu_cortex_a53``, clock-derived throughput and latency use QEMU virtual
time, not physical Cortex-A53 execution time. ``CONFIG_XIP=n`` also places code
and read-only data in RAM. The report's ROM load payload is not physical Flash
occupancy; use an actual MCU linker's memory map for that claim.

HTTP/TLS is outside the XML measurement
**************************************************

Both images run from the same offline XML bytes and reject linked networking
or TLS. The report records transport as **not measured**, never as zero. No
DNS, connection, TLS handshake, HTTP transfer, file I/O, or UART reporting is
included in the XML interval.

A future network-only comparison must drain the same response without parsing,
with an identical connection/session policy. Keep that result separate: parsing
inside a receive callback interleaves transport and XML work, so subtracting an
offline parser measurement does not isolate TLS. Bare XML parsing without
validation is another distinct, currently unmeasured scenario.

Reproducing a run
**************************************************

Complete commands and parameter choices are in
``tests/benchmark/streaming/README.md``. Raw typed-streaming results live under
``tests/benchmark/results/`` with their protocol identified explicitly.

Offline functional checks require neither XSD/e nor Zephyr:

.. code-block:: console

   cmake -S tests/benchmark -B build-benchmark -DCMAKE_BUILD_TYPE=Release
   cmake --build build-benchmark --parallel
   ctest --test-dir build-benchmark --output-on-failure

Normal CTest also runs the Python fairness/report regression tests without
timing thresholds or external dependencies. The real weather schema on both
sides, another small configuration schema, isolated live HTTP/TLS, and physical
MCU measurements remain separate work; the synthetic fixture is not evidence
for those workloads.
