Kconfig reference
#################

``CONFIG_MXSD`` enables the descriptor-driven runtime. Schemas are compiled on
the host and are never parsed on the target.

.. list-table:: Resource limits
   :header-rows: 1
   :widths: 34 14 52

   * - Option
     - Default
     - Meaning
   * - ``CONFIG_MXSD_MAX_INPUT``
     - 1048576
     - Maximum bytes in a NUL-terminated document.
   * - ``CONFIG_MXSD_MAX_DEPTH``
     - 32
     - Maximum nested elements; affects parser and streaming stack storage.
   * - ``CONFIG_MXSD_MAX_PARTICLES``
     - 32
     - Maximum direct-term counters or interned history particles per frame.
   * - ``CONFIG_MXSD_STREAM_CHILD_CAPACITY``
     - 64
     - Child tokens retained by models requiring history; incremental models
       do not consume this capacity.
   * - ``CONFIG_MXSD_STREAM_MODEL_CAPACITY``
     - 128
     - Shared continuation/frontier slots for nested models. Zero removes
       the pool and selects bounded child history for non-flat models.
   * - ``CONFIG_MXSD_STREAM_NAME_CAPACITY``
     - 128
     - Bytes for a local name, namespace URI, or prefix, including the NUL.
   * - ``CONFIG_MXSD_MAX_ATTRIBUTES``
     - 32
     - Maximum non-namespace attributes and descriptors per type.
   * - ``CONFIG_MXSD_MAX_NAMESPACES``
     - 16
     - Maximum simultaneously active namespace bindings.
   * - ``CONFIG_MXSD_MAX_IDENTITY_VALUES``
     - 64
     - Maximum key, unique, or keyref tuples retained per constraint.
   * - ``CONFIG_MXSD_MAX_TEXT_LENGTH``
     - 1024
     - Maximum decoded lexical value and per-frame streaming text buffer.
   * - ``CONFIG_MXSD_XML_TOKEN_CAPACITY``
     - 4096
     - Maximum incomplete raw-XML token retained across input fragments.
   * - ``CONFIG_MXSD_MAX_PATTERN_LENGTH``
     - 256
     - Maximum UTF-8 byte length of one XSD regular expression.
   * - ``CONFIG_MXSD_MAX_REGEX_NODES``
     - 512
     - Fixed parser capacity and principal pattern-validation stack cost.
   * - ``CONFIG_MXSD_MAX_REGEX_DEPTH``
     - 32
     - Maximum group and character-class subtraction nesting.
   * - ``CONFIG_MXSD_MAX_SCHEMA_COMPLEXITY``
     - 4096
     - Maximum aggregate runtime-visible descriptor count.

``CONFIG_MXSD_NO_HEAP`` defaults to enabled. Schemas without identity
constraints remain fully usable. A schema requiring identity tuple storage
returns :c:enumerator:`MXSD_ERR_RESOURCE_LIMIT` unless validation is invoked
with an application allocator.

``CONFIG_MXSD_BINDINGS`` builds the reader and writer for structures generated
with ``mxsdc --bindings``; see :doc:`bindings`. It is disabled by default and
adds no RAM: reading uses a caller-provided arena and writing streams to a
caller-provided sink.

Choose limits from the largest trusted schema and document the device accepts,
then exercise the boundaries with tests. See :doc:`resource-usage` for the
relationship between limits and memory.

Generate schema recommendations
*******************************

Run the host compiler before configuring Zephyr:

.. code-block:: console

   $ python3 mxsdc device.xsd -o generated --zephyr-config
   $ west build -b qemu_cortex_a53 path/to/application -- \
       -DEXTRA_CONF_FILE=/absolute/path/generated/device.conf

The option retains the ordinary validator output and additionally writes
``device.conf`` and ``device.limits.h``. The generated validator source includes
the limits header. It emits a compile-time error when active ``MXSD_*`` macros
are below the known schema requirements. On Zephyr, those macros are defined
from the corresponding ``CONFIG_MXSD_*`` values for the library and application.

The fragment contains active assignments for descriptor sizes: particles,
attributes, runtime schema complexity, name storage, regex AST nodes, pattern
UTF-8 bytes, and regex nesting. It also supplies an element-depth envelope
when the compiled graph is finite, accounting for possible dynamic derived
types and descriptor-group nesting. All emitted types and roots contribute,
including unused named declarations. A requirement exceeding a Kconfig range
fails generation explicitly rather than being clamped.

The child-history recommendation accounts for incremental model selection.
Flat models and nested sequences/choices can use history capacity 1 even with
unbounded repetitions. Flat-only profiles disable the shared model pool to
avoid reserving unused RAM. Other profiles require it to be enabled and leave
its size as a commented application budget (default 128 slots). Size it for
simultaneously active nested models and alternative occurrence partitions,
then test representative documents and exhaustion. The pool is shared across
XML depths, not reserved per frame. Its per-event epsilon-work budget is the
square of its capacity. Exhaustion returns ``MXSD_ERR_STREAM_LIMIT``.

Commented assignments are budgets to review and uncomment as appropriate:

* XML input bytes, lexical text bytes, namespace bindings, and raw token bytes
  cannot be bounded from XSD alone. Comments, arbitrary prefixes, unused
  namespace declarations, entity spelling, and lexical whitespace remain
  instance choices. A ``maxLength`` facet counts characters, list items, or
  decoded binary bytes; it does not bound raw XML or every lexical spelling.
* Recursive type graphs, recursion introduced through ``xsi:type``, and
  wildcard/``anyType`` subtrees require an application depth limit.
* Identity tuple counts need an input budget and caller-owned scratch storage
  in zero-heap mode. Schema-defined field/path sizes without a Kconfig option
  are reported as C compile definitions and checked by the generated header.
* Attribute recommendations cover declarations. Add headroom for accepted
  ``xsi:*`` attributes and wildcard attributes. Name storage includes schema
  names/URIs, standard XML/XSD/xsi URIs, and NUL, but extra-long input prefixes
  can require a larger value.

The fragment is a sizing aid for the documented runtime limits, not a promise
to accept every XML spelling that satisfies the XSD. Stack sizes, caller-owned
arenas, HTTP buffers, TLS, and networking remain application configuration.

For multiple top-level schemas, generate one combined profile:

.. code-block:: console

   $ python3 mxsdc samples/zephyr/weather/nominatim.xsd \
       samples/zephyr/weather/met_weather.xsd -o generated --zephyr-config

This writes ``generated/nominatim.conf`` using the combined model: currently
45 particles, 14 attributes, depth 6, history capacity 1, and no model pool.
Explicit local ``-I`` search directories and includes/imports work as in normal generation.
If separate profiles are merged manually, take the component-wise maximum
(including enabling/sizing the model pool if any profile needs it);
successive Zephyr fragments otherwise overwrite earlier assignments.

Build-time generation can retain the checks with:

.. code-block:: cmake

   mxsd_generate(TARGET app SCHEMA device.xsd ZEPHYR_CONFIG)

This helper runs after Zephyr's Kconfig evaluation. It generates the fragment
as a build artifact but does not apply it to the current configuration.
Regenerate the reviewed profile before reconfiguring when schemas change;
the compile-time checks catch known settings that have become insufficient.
Add ``BINDINGS`` to both invocations when typed bindings are needed; the
fragment then also enables ``CONFIG_MXSD_BINDINGS``.

Pattern parsing is allocation-free and uses a fixed automatic workspace. With
the default 512-node capacity, reserve approximately 11 KiB in the validating
thread in addition to the normal validation stack. Lower
``CONFIG_MXSD_MAX_REGEX_NODES`` when the compiled schemas use smaller patterns;
the ``samples/zephyr`` validation and bindings samples copy their
schema-derived values, including this capacity, into ``prj.conf``.

