# Support matrix

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
| restricted identity XPath | v0.8 | child, descendant and attribute field tests |
| include/import/redefine and schema registry | v0.9 | composed multi-namespace tests |
| resolver callback and dependency cycles | v0.9 | local registry/network/cycle compiler tests |
| substitution groups and abstract elements | v0.10 | direct/substituted/block tests |
| nillable, `xsi:nil`, `xsi:type` | v0.10 | derived, blocked and nil-content tests |
| default/fixed/form/block/final | v0.10 | compiler and runtime positive/negative tests |
| annotation/documentation/appinfo | v0.11 | nested annotation and foreign metadata compiler test |
| event-driven validation API | v0.12 | start/attribute/text/end positive and negative tests |
| Mini-XML tree adapter | v0.12 | optional host adapter, expanded namespace events |
| configurable runtime/schema limits | v0.13 | compiler limit and streaming overflow tests |
| custom allocator and zero-heap mode | v0.13 | allocator accounting and no-heap builds |
| machine-readable conformance report | v0.14 | Apache-2.0 manifest with positive/negative gates |

The generated public function is backend-independent. Mini-XML 4 is used when
available; the allocation-free backend is used for Zephyr. Pattern facets use a
documented embedded subset (literals, `.`, classes, `\\d`, `\\s`, `\\w`, and
`?`, `*`, `+`, `{m,n}` quantifiers); unsupported operators fail compilation.
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

The event API retains one fixed frame per open XML element and never retains
the input document. Its upper bound is `sizeof(mxsd_stream_t)`, primarily
`MXSD_MAX_DEPTH * MXSD_STREAM_TEXT_CAPACITY`; occurrence and attribute state
are bounded by `MXSD_MAX_PARTICLES` and `MXSD_MAX_ATTRIBUTES`. Exceeding a
configured name or text capacity returns `MXSD_ERR_STREAM_LIMIT`.

The DOM-style reader also limits every lexical value with
`MXSD_MAX_TEXT_LENGTH`. `MXSD_MAX_SCHEMA_COMPLEXITY` rejects oversized runtime
descriptors, while `mxsdc --max-schema-complexity` bounds the host-side schema
graph. Identity tuple capacity is configured with
`MXSD_MAX_IDENTITY_VALUES`.

With `MXSD_NO_HEAP`, schemas without identity constraints perform no runtime
allocation. Identity constraints can still be validated explicitly through
`mxsd_validate_with_allocator()`; passing no allocator returns
`MXSD_ERR_RESOURCE_LIMIT`.
