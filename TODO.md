# Repository roadmap

This roadmap focuses on making mxsd a dependable, easy-to-adopt embedded
project. The goal is complete and rigorous support for the documented XSD 1.0
subset, not complete coverage of the W3C specification. Unsupported constructs
must continue to fail explicitly.

## High priority

### Typed streaming bindings

- [x] Generate an opt-in typed reader that consumes raw XML fragments and
      validates and deserializes them in a single pass without retaining the
      complete document.
- [x] Allow applications to select the roots, elements, or schema paths that
      are materialized while the rest of the document is still validated.
- [x] Resolve selections at generation time; do not add a dynamic XPath engine
      to the target runtime.
- [x] Preserve fixed-memory operation with caller-owned output storage and
      arenas, explicit capacities, sticky first-error behavior, and no hidden
      allocation.
- [x] Generate typed callbacks or bounded output structures for selected
      values, including repeated values and values split across input chunks.
- [x] Test arbitrary fragment boundaries, UTF-8 and entity boundaries,
      skipped-but-invalid content, capacity limits, and equivalence with
      full-document validation and bindings.
- [x] Migrate the Zephyr weather sample to the generated streaming binding for
      the MET Norway response once the API is stable.
      `--stream-bind weatherdata/product/time` delivers typed forecasts using
      a reusable 4096-byte arena. Once a complete summary is copied, later
      content is validated without materialization; display waits for finish.

### Compact streaming content-model state

- [x] Eliminate the quadratic validation of homogeneous repeated leaf particles:
      `stream_consecutive_starts()` previously rescanned the remaining child
      history for every occurrence of `maxOccurs > 1` or `unbounded`. Direct
      element and wildcard particles now bypass that unnecessary look-ahead.
- [x] Replace sibling look-ahead with incremental content-model state.
      Flat models use counters/cursors; nested sequences, choices, and complex
      repetitions retain viable partitions in a shared bounded continuation
      pool, not child history. Work per child depends on configured model/pool
      limits, not preceding or remaining sibling count. Exhaustion and the
      bounded epsilon-work guard report explicit limit errors. A zero-capacity
      pool retains the historical fallback for applications choosing it.
- [x] Add a performance regression benchmark at 128, 256, 512, 1024, and 2048
      repeated elements and verify approximately linear validation and
      validated-serialization scaling. `tests/benchmark/run_zephyr_scaling.py`
      checks normalized per-element costs and records raw QEMU output. The
      measured variation is below 2% with child-history capacity set to 1.
- [x] Record a `qemu_cortex_a53` baseline for 1024 elements. After removing
      repeated large scratch-buffer clears, mxsd takes 56268788 cycles in
      validation and 41656088 cycles in validated serialization, respectively
      2.4x and 2.7x the measured XSD/e times. The original baseline was
      668927727 and 375882181 cycles.
- [x] Replace the per-depth `child_particles[]` pointer history with 16-bit
      per-frame particle-table tokens. On the AArch64 benchmark with two
      validators and a child capacity of 1024, static RAM drops from 888 KiB
      to 796 KiB without changing content-model behavior.
- [x] Evaluate a generated or runtime-interpreted incremental content-model
      state made of particle cursors, branch state, and bounded occurrence
      counters. The runtime selects a cursor after xsi:type resolution for
      eligible flat sequences, choices, all groups, and leaf particles.
- [x] Preserve recursive models, substitution groups, wildcards, dynamic
      types, identity constraints, sticky errors, and existing event ordering.
      Covered by the shared host/Twister tests, 15302 DOM/stream differential
      cases, 6560 independent nested-language cases, long repetitions, exact
      pool boundaries/cleanup, and the disabled-pool history fallback.
- [ ] Add exact RAM and validation-time comparisons against the current
      implementation using the weather schema and representative recursive
      and high-`maxOccurs` schemas.
- [x] Reduce the weather sample's need for an unusually large
      `CONFIG_MXSD_STREAM_CHILD_CAPACITY`, or document why a remaining bound is
      fundamental. Its flat models now work with capacity 1 instead of 256;
      the real MET schema is checked with 512 fragmented forecasts and invalid
      values in the optional host benchmark tests.

### Reproducible XSD/e comparison

- [x] Add an opt-in documented host benchmark that compares mxsd with
      CodeSynthesis XSD/e C++/Parser validation on an identical schema and XML
      document without downloading or imposing XSD/e on normal builds.
- [x] Extend the benchmark to compare identical extracted values, compiler
      optimization, and target hardware once mxsd typed streaming bindings are
      available.
      `tests/benchmark/streaming` compares the generated typed mxsd reader with
      real XSD/e callbacks on the same QEMU target, with a common value sink,
      independent value checks, and audited application/runtime compiler flags.
- [ ] Measure transport/TLS alone, XML parsing without schema validation, mxsd,
      and XSD/e separately so networking costs are not attributed to the
      validator.
      The paired XML-only benchmark now excludes networking/TLS explicitly;
      isolated live transport and bare-parser measurements remain to implement.
- [x] Report generated-source size, linked Flash, static RAM, maximum stack,
      dynamic allocation, throughput, and first-result latency.
      The paired report records ROM payload/ELF sections and observed stack/heap
      high-water marks. Physical MCU Flash and worst-case stack are not inferred
      from non-XIP QEMU images or these representative measurements.
- [ ] Include at least a small configuration schema and a large streamed
      document such as the weather response.
- [x] Publish build commands, toolchain versions, configuration files, inputs,
      and raw results; do not compare mxsd's complete application footprint
      with XSD/e's minimum-runtime marketing figure.
      See the paired streaming protocol and its raw QEMU report under
      `tests/benchmark/results/`; the synthetic workload is not a weather/HIL result.
- [ ] Profile and reduce typed-streaming overhead against the new paired
      baseline: mxsd uses 2.48x the total virtual cycles, 3.95x the first-result
      cycles, and 26208 bytes of observed worker stack versus 3248 for XSD/e.
      Keep equivalent value extraction and the zero-heap contract when optimizing.

### Consumable CMake package

- [x] Export the installed `mxsd::mxsd` target with `install(EXPORT ...)`.
- [x] Install `mxsdConfig.cmake` and `mxsdConfigVersion.cmake`.
- [x] Set appropriate `VERSION` and `SOVERSION` properties for shared builds.
- [x] Provide an official `mxsd_generate()` helper,
      inspired by nanopb, that runs `mxsdc` when an XSD dependency changes,
      compiles the generated validator and optional bindings, and adds the
      generated include directory to the consuming target.
- [x] Keep host and target concerns separate during cross-compilation: expose
      `mxsdc` as a host executable while compiling `mxsd::mxsd` and generated C
      sources with the target toolchain.
- [x] Support `--bindings`, repeated `--bind-root`, and
      `--max-schema-complexity` through the generation helper.
- [ ] Replace duplicated sample `add_custom_command()` boilerplate with the
      installed helper once its standalone consumer tests pass.
      The host, bindings, and REST examples are migrated; `zephyr/basic`
      remains intentionally untouched pending its planned rewrite.
- [x] Add a CI test that builds a separate consumer with
      `find_package(mxsd CONFIG REQUIRED)` against the installed package.
- [x] Provide a `pkg-config` file for non-CMake consumers.

### Release-readiness contract

- [ ] Define the criteria for removing the `Draft / Experimental` notice.
- [ ] Require a published representative W3C subset report, stable embedded
      resource measurements, and passing host, no-heap, Twister, and selected
      HIL gates before the first non-experimental release.
- [x] Document the stability policy for the public C API.
- [x] Document compatibility expectations for generated descriptors and
      generated binding code.
- [x] Version the generated descriptor ABI or add an equivalent compile-time
      runtime/generator compatibility check.
- [x] Define the supported upgrade paths between the runtime, generated
      validators, and generated typed bindings.
- [ ] State which platforms and toolchains are officially supported.
- [ ] Keep known limitations and resource bounds easy to find.
- [ ] Align the README maturity statement, package classifiers, version, and
      release notes.

### Embedded resource measurements

- [x] Add an `mxsdc --zephyr-config` mode that emits a documented Kconfig
      fragment with resource minima inferred from the compiled XSD.
- [x] Infer schema-defined requirements such as particle and attribute counts,
      schema complexity, and finite non-recursive depth.
- [x] Flag limits that remain application policies, including recursive depth,
      unconstrained text lengths, and `maxOccurs="unbounded"`; suggest explicit
      values instead of pretending they can be derived from the schema alone.
- [x] Add generated compile-time diagnostics when the active `CONFIG_MXSD_*`
      values are below a schema's known minimum.
- [x] Document that generated fragments must be merged before Zephyr's Kconfig
      phase (for example into `prj.conf` or an overlay), because
      `mxsd_generate()` runs later during the CMake build.
- [ ] Refresh the resource report for the current release and configuration
      defaults; the published Cortex-M figures still refer to the v0.13 limits.
- [ ] Measure a representative physical Cortex-M target with a stable Zephyr
      release and SDK.
- [ ] Report the validator, streaming API, and typed bindings separately where
      useful.
- [ ] Cover zero-heap operation and identity constraints with caller-owned
      scratch storage.
- [ ] Add reasonable CI regression budgets for code size, static RAM, and the
      most important stack measurements.

## Public project readiness

### Community and security files

- [x] Add `CONTRIBUTING.md` with build, test, formatting, generated-code, and
      change-discipline guidance.
- [x] Add `SECURITY.md` with a private reporting channel and supported-version
      policy.
- [x] Add focused issue and pull-request templates.
- [ ] Consider adding `CODE_OF_CONDUCT.md` if external contributions grow.

### README and discovery

- [x] Add concise badges for CI, documentation, PyPI, version, and license.
- [x] Lead with a one-sentence promise: a strict, bounded, zero-heap-capable XSD
      subset for embedded systems.
- [x] Keep a minimal generate/build/validate example near the top.
- [x] Add a short supported-versus-intentionally-unsupported summary with links
      to the complete support matrix.
- [x] Link prominently to the published documentation.
- [x] Move detailed architecture and conformance discussion out of the primary
      quick-start path where appropriate.

### Release process

- [ ] Publish source archives and an installable CMake package for each tag.
- [ ] Add and maintain `CHANGELOG.md` with user-visible changes and migration
      notes.
- [ ] Generate release notes from `CHANGELOG.md`.
- [ ] Verify that the Git tag, CMake version, Python package version, and
      documentation version agree.
- [ ] Consider publishing an SBOM and build provenance/attestations.
- [ ] Decide whether host binary artifacts provide enough value to maintain.

## Quality and portability

### Coverage

- [ ] Add line and branch coverage reporting without treating 100% coverage as
      a project objective.
- [ ] Use coverage to find untested error handling, limit boundaries,
      compiler diagnostics, and public APIs.
- [ ] Keep exact-boundary and first-rejected-value tests for every resource
      limit.

### Embedded-oriented testing

- [ ] Exercise builds with deliberately small `MXSD_MAX_*` values.
- [ ] Track stack budgets for representative configurations.
- [ ] Add further embedded compiler/toolchain coverage when it is sustainable.
- [ ] Add a big-endian build or test target if a reliable CI target is
      available.
- [ ] Exercise ABI-sensitive layouts and alignments on additional platforms.
- [x] Add compatibility tests for generated output when the compatibility
      policy has been defined.

### W3C tests on Zephyr

- [ ] Add a host-side fixture generator that compiles supported W3C schemas
      and packages their XML instances as immutable C data for Zephyr.
- [ ] Keep schema-acceptance results on the host; run only instance validation
      on the target.
- [ ] Split generated cases into bounded firmware batches rather than placing
      the complete W3C suite in one image.
- [ ] Add a representative W3C smoke batch to Twister on `qemu_x86` or
      `native_sim` without treating complete W3C coverage as a project goal.
- [ ] Classify embedded resource-limit results separately from validation
      failures and unsupported XSD constructs.
- [ ] Consider a larger scheduled Zephyr run after measuring its CI cost.

### Hardware-in-the-loop validation

- [ ] Define a small HIL corpus focused on embedded-specific risks rather than
      replaying the complete W3C suite.
- [ ] Cover configured depth, text, regex, namespace, identity, and binding
      limits at their exact boundary and first rejected value.
- [ ] Measure real stack usage, static RAM, Flash, execution time, and watchdog
      behavior on at least one constrained Cortex-M target.
- [ ] Exercise fragmented XML input from a representative MCU transport where
      applicable.
- [ ] Run HIL on a scheduled or release gate unless its reliability and runtime
      justify making it a pull-request requirement.

### Fuzzing policy

- [ ] Keep fuzz harnesses and seed corpora in `fuzz/`, separate from the normal
      deterministic test suite.
- [ ] Turn every confirmed fuzzing defect into a focused deterministic
      regression test under `tests/`.
- [ ] Retain useful triggering inputs in the bounded fuzz corpus.

## Repository hygiene

- [ ] Remove tracked `.DS_Store` files.
- [ ] Correct the `.DS_Storename` ignore entry to `.DS_Store` if still present.
- [ ] Keep virtual environments, Python caches, build trees, generated
      documentation, downloaded W3C data, and fuzz artifacts untracked.
- [ ] Validate and document the REST Zephyr sample before committing it.
- [ ] Run the complete host, no-heap, sample, documentation, and applicable
      Zephyr checks before the next release.

## Explicit non-goals

- [ ] Do not pursue complete W3C XSD 1.0 coverage as a release criterion.
- [ ] Do not accept unsupported schema constructs by silently ignoring them.
- [ ] Do not weaken memory limits or introduce unbounded target-side storage to
      make additional conformance cases pass.
- [ ] Add XSD features only when they have a concrete user need and fit the
      embedded resource model.


automate incrémental du modèle de contenu
