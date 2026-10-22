# Repository roadmap

This roadmap focuses on making mxsd a dependable, easy-to-adopt embedded
project. The goal is complete and rigorous support for the documented XSD 1.0
subset, not complete coverage of the W3C specification. Unsupported constructs
must continue to fail explicitly.

## High priority

### Consumable CMake package

- [ ] Export the installed `mxsd::mxsd` target with `install(EXPORT ...)`.
- [ ] Install `mxsdConfig.cmake` and `mxsdConfigVersion.cmake`.
- [ ] Set appropriate `VERSION` and `SOVERSION` properties for shared builds.
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
- [ ] Add a CI test that builds a separate consumer with
      `find_package(mxsd CONFIG REQUIRED)` against the installed package.
- [ ] Consider providing a `pkg-config` file for non-CMake consumers.

### Release-readiness contract

- [ ] Define the criteria for removing the `Draft / Experimental` notice.
- [ ] Document the stability policy for the public C API.
- [ ] Document compatibility expectations for generated descriptors and
      generated binding code.
- [ ] State which platforms and toolchains are officially supported.
- [ ] Keep known limitations and resource bounds easy to find.
- [ ] Align the README maturity statement, package classifiers, version, and
      release notes.

### Embedded resource measurements

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
- [ ] Add compatibility tests for generated output when the compatibility
      policy has been defined.

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
