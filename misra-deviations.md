# MISRA C:2012 Deviation Register

## Status and scope

This document proposes deviations for the low-criticality findings in `report.xml`.
It does not, by itself, constitute a MISRA compliance claim. Each deviation must be
approved through the quality process of the product integrating mxsd.

- Report reference: `report.xml`
- SHA-256 fingerprint:
  `4aa97f0572398920b914c1724c50acd140ba1a6351b6d7f2500d4d792c4f90a3`
- Analyzed scope: C files under `src/`
- Total findings: 2,984
- Findings covered by the advisory rules below: 2,271
- Required or mandatory findings not covered: 713
- Deviation status: **proposed, pending approval**

The term "low-criticality" is interpreted here as "an advisory rule whose residual
risk is controlled in this project." A required or mandatory rule is not waived on
that basis alone. The `(-)` column in the reference report does not provide a
category because that report was generated before classifications were added to
`misra2023_rules_ascii.txt`. A regenerated report should display them.

The deviations are limited to the patterns described below. Any new occurrence that
does not match these patterns requires a separate analysis.

## Summary

| ID | Rule | Findings | Disposition |
| --- | --- | ---: | --- |
| DEV-8.9 | 8.9 | 5 | Accepted: immutable static objects |
| DEV-11.5 | 11.5 | 20 | Accepted: generic APIs using `void *` |
| DEV-12.1 | 12.1 | 1,035 | Accepted: conventional C precedence |
| DEV-12.3 | 12.3 | 64 | 63 false positives; 1 accepted case |
| DEV-13.3 | 13.3 | 35 | Accepted: local cursor increments |
| DEV-13.4 | 13.4 | 3 | Accepted: pointer assignment followed by a test |
| DEV-15.1 | 15.1 | 4 | Accepted: single restoration point |
| DEV-15.4 | 15.4 | 1 | Accepted: two normal tag terminators |
| DEV-15.5 | 15.5 | 898 | Accepted: guard clauses and error propagation |
| DEV-17.8 | 17.8 | 39 | Accepted: local working copies |
| DEV-18.4 | 18.4 | 165 | Accepted: bounded buffer traversal |
| DEV-19.2 | 19.2 | 2 | Accepted: maximum-alignment calculation |

After excluding the 63 false positives for Rule 12.3, these records cover 2,208
effective deviations.

## Detailed deviations

### DEV-8.9 — objects retained at file scope

**Decision:** accepted for the five findings in the reference report.

The affected objects are namespace constants, linked wildcard descriptors, and
generated Unicode tables. They all have internal linkage, are `const`-qualified, and
carry no mutable state. Their static storage duration is intentional: it avoids stack
copies, allows descriptors to refer to one another, and keeps large generated tables
in read-only memory.

**Risk and controls:** the wider scope could enable future coupling. The `static`
linkage, `const` qualification, and absence of an address exposed outside the
translation unit limit this risk. The deviation must be reviewed if any of these
objects becomes mutable or is exposed through an API.

### DEV-11.5 — conversion from `void *`

**Decision:** accepted for the twenty findings in the reference report.

The binding, arena, and callback layers use `void *` to carry an object whose concrete
type is described by generated tables. Conversion to a byte pointer or to the
expected internal type is the generic mechanism provided by the C99 API. Typed value
locations are loaded and stored using `memcpy`, and the arena enforces the required
alignment.

**Risk and controls:** an incorrect type description, size, or alignment could make
the conversion invalid. The compiler generates the types and descriptors together;
sizes, offsets, capacity limits, and ABI epochs are checked by the runtime and binding
tests. This deviation does not cover removal of `const` or conversion to a type not
described by the active descriptor.

### DEV-12.1 — implicit operator precedence

**Decision:** accepted for the 1,035 findings in the reference report.

The code uses conventional C precedence for index calculations, limit checks, bit
masks, and Boolean expressions. Adding parentheses at every level would substantially
increase noise in the parsers without changing their semantics. Groups intended to
override standard precedence are already parenthesized.

**Risk and controls:** a reader or later modification could misinterpret an
expression. Expressions remain local, accesses are bounded before dereferencing, and
positive, negative, and boundary paths are covered by runtime, streaming, binding,
and conformance tests. Any expression combining assignment, dependent side effects,
or types whose conversion changes the result must be rewritten rather than covered
by this deviation.

### DEV-12.3 — commas reported as the comma operator

**Decision:** one deviated case and 63 findings classified as false positives.

Sixty-three findings identify commas that separate declarators, parameters,
arguments, or aggregate initializers. These tokens are not the comma operator covered
by Rule 12.3 and therefore require no deviation.

The only actual use covered is the loop initialization in `src/mxsd_xml_mxml.c`,
where `index` is set to zero before `count` is read. The operations affect distinct
objects, and the comma operator guarantees left-to-right sequencing.

**Risk and controls:** the use can be overlooked during review. It is limited to the
initialization clause of a local loop and does not modify validator state. Any other
actual use of the comma operator must be split into separate statements.

### DEV-13.3 — increment combined with another side effect

**Decision:** accepted for the 35 findings in the reference report.

The occurrences advance local cursors or counters while copying, decoding, or writing
an element. The incremented objects are distinct from the object being written and
are neither read nor modified a second time without sequencing in the same
expression.

**Risk and controls:** multiple side effects can become ambiguous after maintenance.
Counters are bounded before access, capacities are constant or checked, and tests
exercise truncated inputs and boundary conditions. The deviation does not cover any
expression whose result depends on an unspecified evaluation order.

### DEV-13.4 — use of an assignment result

**Decision:** accepted for the three findings in the reference report.

Each occurrence assigns the result of a lookup or allocation to a pointer, then
explicitly compares that pointer with `NULL` in the same condition. This guarantees a
single call, immediately preserves the useful value, and makes the failure test
visible.

**Risk and controls:** confusion with `==` is limited by parentheses and the explicit
comparison with `NULL`. This deviation does not cover a scalar assignment used as an
implicit condition.

### DEV-15.1 — restoration `goto`

**Decision:** accepted for the four findings, all in
`validate_typed_wildcard_element()`.

The branches go forward to the single `restore` label. This point systematically
restores the parser position and namespace count before returning. Duplicating the
restoration on every error path would increase the risk of omitting part of the
state.

**Risk and controls:** no jump enters a scope, no loop is created, and the label is
local to the function. The deviation no longer applies if the branch targets anything
other than a restoration or release epilogue.

### DEV-15.4 — multiple loop exits

**Decision:** accepted for the finding in `parse_start_tag()`.

The loop recognizes the two valid and distinct XML tag terminators: `/>` and `>`.
Each updates the corresponding state and immediately exits the loop. Errors use
explicit returns and cannot be confused with these normal terminators.

**Risk and controls:** both exits are adjacent to the token test that justifies them,
and neither bypasses intermediate cleanup. Any new loop exit requires a new analysis.

### DEV-15.5 — multiple return points

**Decision:** accepted for the 898 findings in the reference report.

mxsd uses guard clauses to immediately reject an invalid argument, malformed input,
or exceeded limit. The parsers and streaming API therefore propagate the first error
without adding nesting levels or modifying state after failure. This style notably
supports the streaming API's sticky-result behavior.

**Risk and controls:** an early return could bypass restoration or release. Functions
that temporarily modify state use a common epilogue when necessary; allocations are
owned by the arena or released by their documented owner. Tests cover boundary errors
and both heap and no-heap modes. Any function acquiring a new resource not managed by
the arena must have its returns reviewed.

### DEV-17.8 — locally modified parameters

**Decision:** accepted for the 39 findings in the reference report.

The affected parameters are values passed by copy and used as a cursor, remaining
length, position, or normalized span. Modifying them does not affect the caller's
variable and avoids redundant local variables in short traversal functions.

**Risk and controls:** a reader could assume that the initial value remains
available. Modifications occur near the function entry or the loop that consumes the
value. This deviation does not cover modification of the object referenced by a
parameter when the contract declares that object read-only.

### DEV-18.4 — pointer arithmetic

**Decision:** accepted for the 165 findings in the reference report.

The runtime represents XML input and lexical values using pointers and lengths, then
traverses contiguous arrays, buffers, spans, and arenas. Pointer additions and
subtractions index those same objects or calculate a length. This representation
avoids an allocation and copy for every token, which is central to a bounded embedded
runtime.

**Risk and controls:** moving outside the array object would make the operation
invalid. Lengths and capacities are checked before access, capacity calculations
check for overflow, and a one-past-the-end pointer is used only for comparison or a
difference within the same array. The deviation covers neither arithmetic between
distinct objects nor dereferencing beyond a bound.

### DEV-19.2 — alignment union

**Decision:** accepted for the two findings, which originate from one mechanism in
`src/mxsd_bind.c`.

`bind_max_align_t` collects the types with the strictest alignment requirements
supported by bindings. It is placed in `alignment_probe_t` to calculate the alignment
required for arena allocations in C99, where `max_align_t` is unavailable.

**Risk and controls:** the union is never used to read a member different from the
one written, so it performs no type punning. It is used only to impose alignment. The
deviation must be reviewed when adding a bound type with a stricter alignment
requirement.

## Findings not deviated

The following rules from the report are not covered by this register:

- 7.2, 8.4, 10.3, 10.4, 10.6, 10.8, 11.3, and 11.9;
- 13.5, 14.2, 14.4, 15.6, 15.7, and 16.4;
- 17.2, 17.3, and 17.7;
- 20.9, 21.3, 21.6, 21.14, 21.15, 21.16, 22.8, and 22.9.

These 713 findings must be corrected or individually classified as false positives
before any compliance claim. In particular, recursion, essential-type conversions,
prohibited library functions, `errno` handling, and implicit declarations must not be
treated as low-criticality by default.

## Common measures and review criteria

The proposed deviations rely on the following common measures:

- C99 compilation on all supported platforms;
- explicit limits on input size, depth, attributes, particles, models, regular
  expressions, and identity constraints;
- runtime, streaming-validator, binding, and boundary-value tests;
- separate tests with `MXSD_NO_HEAP=ON` for allocation-free paths;
- static analysis rerun with the same configuration after every modification.

The register must be reviewed if the affected code changes pattern, a limit is
removed or increased, a public API or generated layout changes, a new target compiler
is added, or the analysis tool fixes a false positive used in this justification.

## Expected verification

Before approval, run at least:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DMXSD_USE_MXML=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure

cmake -S . -B build-no-heap \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMXSD_USE_MXML=OFF \
  -DMXSD_NO_HEAP=ON
cmake --build build-no-heap --parallel
ctest --test-dir build-no-heap --output-on-failure
```

For Rule 12.3, the Mini-XML configuration must also be built and tested because the
only actual comma-operator use is in that adapter.
