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
     - Optional, required, prohibited, form, default, and fixed.
   * - attribute ``ref``
     - Unsupported
     - Local attributes must declare ``name``.
   * - ``group`` and ``attributeGroup``
     - Unsupported
     - Content and attributes must be declared inline.
   * - simple restriction and facets
     - Supported
     - Enumeration, lengths, bounds, whitespace, and inherited restrictions.
   * - ``xs:list`` and ``xs:union``
     - Supported
     - Named and anonymous item/member types.
   * - XSD 1.0 built-in simple datatypes
     - Supported
     - String/name, numeric, date/time, duration, binary, QName, URI, ID/IDREF,
       ENTITY, and lexical NOTATION families.
   * - ``xs:notation`` declarations
     - Unsupported
     - NOTATION datatype lexical validation is separate and supported.
   * - type extension and restriction
     - Supported
     - ``simpleContent`` and ``complexContent`` with inherited content/attributes.
   * - abstract types
     - Supported
     - Direct use is rejected; valid derived runtime types are accepted.
   * - recursive type definitions
     - Unsupported
     - Circular definitions are rejected during generation.
   * - ``xs:anyType`` and mixed content
     - Unsupported
     - Unrestricted and ``mixed=\"true\"`` content is rejected.

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
     - Partial
     - Accepted type redefinitions only; group redefinitions are unavailable.
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
     - Partial
     - XSD restricted child/descendant/attribute paths; no general XPath engine.
   * - ``nillable`` and ``xsi:nil``
     - Supported
     - Nil content and fixed/nillable rules are enforced.
   * - ``xsi:type``
     - Supported
     - Runtime QName resolution plus derivation and block checks.
   * - element and attribute defaults/fixed values
     - Supported
     - Simple and simple-content values.
   * - pattern facet
     - Partial
     - Literals, dot, classes, ``\\d``, ``\\s``, ``\\w``, ``?``, ``*``, ``+``,
       and ``{m,n}``; alternation, grouping, and anchors are rejected.
   * - DTD and XML entities
     - Unsupported
     - Rejected deliberately by schema and embedded XML processing.
   * - event-driven validation
     - Partial
     - Fixed-memory API; names, nesting, and a simple text value are bounded.
   * - Mini-XML 4 adapter
     - Supported
     - Optional adapter selected when Mini-XML is available.
   * - XSD 1.1
     - Unsupported
     - mxsd targets XSD 1.0.

This matrix is a feature contract, not a claim of complete XSD 1.0
implementation. See :doc:`conformance` for how these claims are tested.

