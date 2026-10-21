XSD 1.0 support
###############

**Supported** means positive and negative behavior is covered by project
tests. **Partial** means only the documented subset is implemented.
**Unsupported** means ``mxsdc`` rejects the construct instead of ignoring it.

Schema structure and types
**************************

.. list-table::
   :header-rows: 1
   :widths: 31 14 55

   * - Feature
     - Status
     - Scope
   * - ``xs:schema``, global and local elements
     - Supported
     - Multiple roots, named, typed, anonymous, and global element references.
   * - ``xs:complexType`` and ``xs:simpleType``
     - Supported
     - Named and anonymous definitions.
   * - ``sequence``, ``choice``, ``all``
     - Supported
     - Nested particles and bounded or ``unbounded`` occurrences.
   * - attributes
     - Supported
     - Optional, required, prohibited, form, default, fixed, and global references.
   * - attribute ``ref``
     - Supported
     - References preserve the global declaration namespace, type, and value constraint.
   * - ``group`` and ``attributeGroup``
     - Supported
     - Nested references, occurrences, namespace resolution, wildcard algebra, and
       legal self references in redefinitions; circular definitions are rejected.
   * - simple restriction and facets
     - Supported
     - Enumeration, lengths, bounds, decimal digit limits, whitespace, and inherited
       restrictions.
   * - ``xs:list`` and ``xs:union``
     - Supported
     - Named and anonymous item/member types.
   * - XSD 1.0 built-in simple datatypes
     - Supported
     - String/name, numeric, date/time, duration, binary, QName, URI, ID/IDREF,
       ENTITY, and lexical NOTATION families.
   * - ``xs:notation`` declarations
     - Supported
     - Public/system metadata and namespace-aware enumerated NOTATION values.
   * - type extension and restriction
     - Supported
     - ``simpleContent`` and ``complexContent`` with inherited content/attributes.
   * - abstract types
     - Supported
     - Direct use is rejected; valid derived runtime types are accepted.
   * - recursive type definitions
     - Supported
     - Direct and mutual named complex types plus guarded recursive global element
       references, bounded at runtime by ``MXSD_MAX_DEPTH``.
   * - ``xs:anyType`` and mixed content
     - Supported
     - Interleaved text, lax arbitrary content, implicit element types, and restrictions.

Namespaces and composition
**************************

.. list-table::
   :header-rows: 1
   :widths: 31 14 55

   * - Feature
     - Status
     - Scope
   * - target namespaces and QName resolution
     - Supported
     - Qualified and unqualified local declarations.
   * - ``elementFormDefault`` / ``attributeFormDefault``
     - Supported
     - Per-declaration ``form`` overrides.
   * - ``xs:include`` and ``xs:import``
     - Supported
     - Explicit local resolution only; implicit network access is forbidden.
   * - ``xs:redefine``
     - Supported
     - Type, model-group, and attribute-group redefinitions with original-component
       self references.
   * - resolver callback and schema registry
     - Supported
     - Search paths, namespace registry, and circular-dependency detection.
   * - substitution groups
     - Supported
     - Type derivation, abstract heads, ``block``, and ``final`` are checked.
   * - annotations and foreign metadata
     - Supported
     - Validated while compiling, then omitted from generated descriptors.

Instance validation
*******************

.. list-table::
   :header-rows: 1
   :widths: 31 14 55

   * - Feature
     - Status
     - Scope
   * - ``xs:any`` and ``xs:anyAttribute``
     - Supported
     - Namespace constraints and strict, lax, or skip processing.
   * - ``unique``, ``key``, ``keyref``
     - Supported
     - Bounded tuple storage suitable for embedded targets.
   * - identity XPath
     - Supported
     - XSD restricted child/descendant/attribute paths, wildcards, and unions; no
       general XPath engine.
   * - ``nillable`` and ``xsi:nil``
     - Supported
     - Nil content and fixed/nillable rules are enforced.
   * - ``xsi:type``
     - Supported
     - Runtime QName resolution plus derivation and block checks.
   * - element and attribute defaults/fixed values
     - Supported
     - Atomic, list, union, simple-content, ``anyType``, and mixed-complex value constraints.
   * - pattern facet
     - Supported
     - Bounded subset with grouping, alternation, repetition, class subtraction,
       implicit whole-value anchoring, standard name/multi-character escapes, and
       Unicode categories/blocks. Some accepted W3C expressions are rejected
       explicitly and are listed as unsupported in the W3C baseline.
   * - DTD and XML entities
     - Unsupported
     - Rejected deliberately by schema and embedded XML processing.
   * - event-driven validation
     - Supported
     - Bounded expanded-name events, namespace/QName values, recursive content models,
       substitution, wildcards, dynamic types, and identity constraints. Identity
       constraints require the optional scratch allocator.
   * - Mini-XML 4 adapter
     - Supported
     - Optional adapter selected when Mini-XML is available.
   * - XSD 1.1
     - Unsupported
     - mxsd targets XSD 1.0.

Typed bindings
**************

``mxsdc --bindings`` generates C structures with read and write functions for
global elements with a complex type. Bindings cover a subset of the constructs
above: wildcards, mixed content, ``xs:anyType``, repeating model groups,
nillable and abstract elements, substitution-group heads, abstract types, and
``QName`` or ``NOTATION`` values are rejected with a ``bindings:`` diagnostic.
They remain fully supported for validation. See :doc:`bindings`.

This matrix is a feature contract, not a claim of complete XSD 1.0
implementation. See :doc:`conformance` for how these claims are tested.

