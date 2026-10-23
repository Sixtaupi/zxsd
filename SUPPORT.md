# Support matrix

The [API/ABI and migration contract](doc/compatibility.rst) covers the runtime,
generated validators, and typed/streaming bindings. Epoch checks reject
incompatible generated formats at compile/link time; explicit startup checks
compare limits and public storage sizes. These are integration guarantees, not
additional XSD coverage or a promise of drop-in binary compatibility during 0.x.

| Construct | Status | Validation coverage |
|---|---:|---|
| `xs:schema` | v0.1 | compiler |
| global `xs:element` | v0.1 | root and wrong-root runtime tests |
| named `xs:complexType` | v0.1 | compiler/runtime |
| `xs:sequence` | v0.1 | missing and out-of-order tests |
| local `xs:element` | v0.1 | occurrence and primitive tests |
| `xs:attribute` | v0.1 | required, unexpected, primitive tests |
| string, boolean, int, unsignedInt, integer, decimal | v0.1 | runtime |
| restrictions/facets | v0.2 | advanced compiler/runtime test |
| choice/all/ref/anonymous nested types | v0.3 | advanced compiler/runtime test |
| namespaces/qualified instance names | v0.4 | prefixed/default/wrong-URI tests |
| simple/complex type derivation | v0.5 | restriction and extension positive/negative tests |
| simpleContent/complexContent | v0.5 | inherited content and attribute tests |
| anonymous/named/inherited/abstract types | v0.5 | compiler/runtime tests |
| `xs:list` and `xs:union` | v0.6 | item/member and facet tests |
| complete XSD 1.0 simple datatype family | v0.6 | lexical positive/negative tests |
| `whiteSpace` preserve/replace/collapse | v0.6 | normalized enumeration/length tests |
| nested sequence/choice/all and group occurrences | v0.7 | recursive particle tests |
| `xs:any` and `xs:anyAttribute` | v0.7 | namespace and strict/lax/skip tests |
| `xs:unique`, `xs:key`, `xs:keyref` | v0.8 | duplicate, missing and dangling-reference tests |
| restricted identity XPath | v0.8 | child, descendant, attribute, wildcard, and union tests |
| include/import/redefine and schema registry | v0.9 | type, model-group, attribute-group, and multi-namespace tests |
| resolver callback and dependency cycles | v0.9 | local registry/network/cycle compiler tests |
| substitution groups and abstract elements | v0.10 | direct/substituted/block tests |
| nillable, `xsi:nil`, `xsi:type` | v0.10 | derived, blocked and nil-content tests |
| default/fixed/form/block/final | v0.10 | compiler and runtime positive/negative tests |
| annotation/documentation/appinfo | v0.11 | nested annotation and foreign metadata compiler test |
| event-driven validation API | v0.12 | namespace/start/attribute/text/end events, raw XML fragments, recursive particles, dynamic types, and streamed identity constraints |
| Mini-XML tree adapter | v0.12 | optional host adapter, expanded namespace events |
| configurable runtime/schema limits | v0.13 | compiler limit and streaming overflow tests |
| custom allocator and zero-heap mode | v0.13 | allocator accounting and no-heap builds |
| machine-readable conformance report | v0.14 | Apache-2.0 manifest with positive/negative gates |
| `xs:group`, `xs:attributeGroup`, attribute `ref` | v0.15 | expansion, occurrence, namespace, fixed-value, and cycle tests |
| mixed complex content and `xs:anyType` | v0.16 | interleaved text, lax wildcard, restriction, implicit type, and streaming tests |
| recursive named complex types | v0.17 | direct/mutual recursion, invalid derivation cycles, depth-bound DOM and streaming tests |
| bounded XSD 1.0 regular-expression subset | v0.18 | groups, alternation, class subtraction, Unicode categories/blocks, standard escapes, repetition, and facet composition |
| `xs:notation` and `xs:NOTATION` | v0.19 | declaration metadata, QName enumeration resolution, direct-use and undeclared-name rejection |
| typed C bindings (`mxsdc --bindings`) | v0.20 | read/write round trips, native integer bounds, enumerations, arrays, recursion, defaults, escaping, arena and buffer boundaries, writer structural errors, `xsi:type` rejection, and compiler rejection tests |

Typed bindings represent a subset of the validated constructs. An unbounded
`xs:sequence` is bindable when its members are optional and uniquely named;
each member becomes a typed array. Element and attribute wildcards, mixed
content, `xs:anyType`, other repeating model groups, an element name occurring
at several places of one content model, nillable and
abstract elements, substitution-group heads, abstract complex types, and
`QName` or `NOTATION` values are rejected by `mxsdc --bindings` with a
`bindings:` diagnostic. They remain supported for validation. A valid document
using `xsi:type` substitution or `xsi:nil` cannot be read into a structure and
returns `MXSD_ERR_UNSUPPORTED`. See [`doc/bindings.rst`](doc/bindings.rst).

The generated public function is backend-independent. Mini-XML 4 is used when
available; the allocation-free backend is used for Zephyr. Pattern facets use
a bounded XSD 1.0 subset with grouping, alternation, class subtraction,
Unicode categories and blocks, the standard multi-character escapes, and
bounded repetition. Expressions are implicitly anchored to the complete
lexical value, as required by XSD 1.0, and are bounded by
`MXSD_MAX_PATTERN_LENGTH`, `MXSD_MAX_REGEX_NODES`, and `MXSD_MAX_REGEX_DEPTH`.
The W3C baseline documents accepted-suite expressions that the compiler still
rejects; mxsd therefore does not claim the complete XSD regex grammar.
Whitespace normalization happens before value-space validation. List length
facets count items, while binary length facets count decoded octets.

## Embedded memory for identity constraints

Content-model validation keeps counters proportional to the number of schema
particles, never to `maxOccurs`. Identity validation stores non-owning spans
into the original XML input. Its tuple table is allocated only when a root has
identity constraints and is bounded by:

`identity_count * MXSD_MAX_IDENTITY_VALUES * MXSD_MAX_IDENTITY_FIELDS`

Each stored field is one pointer, one length, and a presence flag. The default
limits are 16 constraints, 64 rows per constraint, 8 fields per tuple, and an
XPath depth of 8. Applications can lower these macros at compile time. Inputs
that exceed a configured identity limit fail validation instead of growing
memory without a bound.

## Streaming memory

`mxsdc --stream-bind element/path` generates an opt-in typed fragment reader.
The path is resolved on the host; no runtime XPath is added. One selected
subtree is materialized in a reusable caller arena while the full document is
validated once. Root and simple-element selections are supported. Callbacks
are provisional until finish, and may request validation-only processing for
later occurrences. The selected subtree follows the ordinary binding subset;
unselected content is not silently skipped by the validator. Arena exhaustion
and unsupported dynamic/nil materialization fail explicitly. See
[typed streaming bindings](doc/bindings.rst) for lifetimes, growth overhead,
identity scratch, and the independent-reader semantics of multiple selections.

The event API retains one fixed frame per open XML element. Schemas without
identity constraints are allocation-free and use `sizeof(mxsd_stream_t)`,
primarily `MXSD_MAX_DEPTH * MXSD_STREAM_TEXT_CAPACITY`; occurrence, namespace,
and attribute state are bounded by `MXSD_MAX_PARTICLES`, `MXSD_MAX_NAMESPACES`,
and `MXSD_MAX_ATTRIBUTES`.

Flat sequences and `all` groups with at most one iteration, flat sequences
with at most one mandatory iteration (including unbounded repetition), and
single leaf particles use incremental counters. Flat choices use a branch
cursor for one iteration, or a repetition counter when every branch consumes
exactly one child. These models do not retain child tokens and can process
more than `MXSD_STREAM_CHILD_CAPACITY` children. Invalid order or excess
occurrences fail on the offending start-element event; missing children fail
when the parent closes. Nested sequences/choices and complex repetitions use
`MXSD_STREAM_MODEL_CAPACITY` shared continuation/frontier slots (default 128),
including alternative occurrence partitions and nullable groups. This pool is
shared across XML depths and does not grow with the document. Slot exhaustion
or the per-event epsilon-work budget (capacity squared) returns
`MXSD_ERR_STREAM_LIMIT`; it never silently drops a viable partition.

Setting the pool capacity to zero selects the old bounded-history matcher for
non-flat models. Hand-built non-XSD trees with nested `all` groups also use
that fallback. It retains 16-bit particle tokens up to
`MXSD_STREAM_CHILD_CAPACITY`, plus a bounded pointer table. The history arrays
remain reserved per frame; capacity 1 is sufficient for compiler-generated
models when the continuation pool is enabled and adequately sized. Rebuild
the library and application together when changing public workspace limits.

For a schema with identity constraints, the stream allocator retains a
bounded canonical XML copy of at most `MXSD_MAX_INPUT`
bytes and releases it at finish or on the first error. This lets the same
restricted-XPath and typed tuple implementation serve both APIs. Exceeding a
configured name, text, child, namespace, or input capacity returns
`MXSD_ERR_STREAM_LIMIT`.

`mxsd_xml_reader_t` adds a raw-XML frontend around that event API. It retains
at most `MXSD_XML_TOKEN_CAPACITY + 1` bytes for a token split across fragments
and `MXSD_MAX_TEXT_LENGTH + 1` bytes for decoded text, in addition to its
embedded `mxsd_stream_t`; it does not retain the supplied fragment buffers.

The DOM-style reader also limits every lexical value with
`MXSD_MAX_TEXT_LENGTH`. `MXSD_MAX_SCHEMA_COMPLEXITY` rejects oversized runtime
descriptors, while `mxsdc --max-schema-complexity` bounds the host-side schema
graph. Identity tuple capacity is configured with
`MXSD_MAX_IDENTITY_VALUES`.

`mxsdc --zephyr-config` can emit an optional Kconfig sizing fragment and a
compile-time limits header. Descriptor counts, finite depth envelopes, regex
storage, and bounded fallback history are inferred from the compiled schema
graph. Recursion, wildcard subtrees, lexical text/input sizes, namespace
declarations, and identity row counts remain explicit application policies.
These recommendations do not extend the supported XSD subset or guarantee
acceptance of every valid XML spelling. See `doc/kconfig.rst` for the workflow.

With `MXSD_NO_HEAP`, schemas without identity constraints perform no runtime
allocation. Identity constraints can still be validated explicitly through
`mxsd_validate_with_allocator()`; passing no allocator returns
`MXSD_ERR_RESOURCE_LIMIT`.

Regular-expression parsing is also allocation-free. Its fixed automatic
workspace is dominated by `MXSD_MAX_REGEX_NODES` (about 11 KiB at the default
of 512 nodes), so pattern-enabled embedded applications must size the validator
thread stack accordingly or lower the node limit for their trusted schemas.
