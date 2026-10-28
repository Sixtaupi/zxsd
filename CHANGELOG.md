# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.20.0] - 2026-10-21

Initial public release.

### Added

- Deterministic `mxsdc` host compiler generating immutable C99 schema
  descriptors and schema-specific validation entry points.
- Portable `libmxsd` runtime for validating complete XML documents, decoded
  parser events, and incremental raw-XML byte fragments.
- Supported XSD 1.0 subset covering:
  - simple and complex types, derivation, lists, unions, and built-in datatypes;
  - restriction facets, whitespace normalization, and bounded regular expressions;
  - sequences, choices, `xs:all`, model groups, attribute groups, references,
    wildcards, mixed content, and recursive named types;
  - namespaces, schema composition, substitution groups, abstract declarations,
    defaults, fixed values, `xsi:nil`, and `xsi:type`;
  - `xs:unique`, `xs:key`, `xs:keyref`, and restricted identity XPath.
- Typed C structures and validating read/write functions generated with
  `mxsdc --bindings`, using caller-owned arenas and output buffers.
- Single-pass typed subtree delivery generated with `mxsdc --stream-bind`.
- Fixed-capacity event-driven validation and incremental XML tokenization without
  retaining input fragments.
- Configurable input, depth, text, namespace, identity, content-model, regex,
  and schema-complexity limits.
- Custom allocator support and `MXSD_NO_HEAP` builds. Schemas without identity
  constraints require no runtime allocation; identity constraints can use an
  explicit application allocator.
- Zephyr module, Kconfig integration, generated sizing recommendations, and
  compile-time limit checks.
- Allocation-free portable XML backend and optional Mini-XML 4 tree adapter.
- Linux, macOS, Windows, and Zephyr builds, including static and shared host
  libraries.
- Installed CMake package, `mxsd_generate()` build integration, pkg-config
  metadata, and independently installable `mxsdc` Python package.
- Versioned runtime, schema-descriptor, and binding compatibility epochs,
  generated link guards, runtime configuration checks, `mxsdc --version`,
  `mxsdc --abi-info`, and `mxsd_version()`.
- Project-owned positive and negative conformance manifest with machine-readable
  results.
- Optional W3C XSD 1.0 harness with separate `passed`, `failed`,
  `unsupported`, `expected-failure`, and `skipped` classifications.
- Host, no-heap, shared-library, installed-package, generated-compatibility,
  Zephyr, sanitizer, fuzzing, static-analysis, and documentation CI coverage.
- Reproducible host and embedded resource reporting and an opt-in comparison
  benchmark against CodeSynthesis XSD/e.

### Security

- Unsupported schema constructs and properties fail explicitly instead of being
  silently ignored.
- The target runtime never parses schemas and has no filesystem, network, or
  POSIX dependency.
- Schema composition is resolved locally on the host; the default resolver
  rejects HTTP and other implicit network access.
- Runtime storage is bounded by explicit compile-time limits. Limit exhaustion
  returns a structured error instead of growing memory from untrusted input.

### Known limitations

- mxsd implements the subset documented in [SUPPORT.md](SUPPORT.md); it does not
  claim complete XSD 1.0 support.
- Typed bindings support a stricter subset than validation. Unsupported binding
  representations fail during generation instead of dropping data.
- Pattern facets use a bounded XSD regular-expression subset.
- W3C suite results measure interoperability and do not replace the documented
  product support contract.
