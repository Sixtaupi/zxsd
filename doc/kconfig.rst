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
     - Maximum particles in one generated complex type.
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

Pattern parsing is allocation-free and uses a fixed automatic workspace. With
the default 512-node capacity, reserve approximately 11 KiB in the validating
thread in addition to the normal validation stack. Lower
``CONFIG_MXSD_MAX_REGEX_NODES`` when the compiled schemas use smaller patterns;
the repository Zephyr sample reserves a 16 KiB main stack for the defaults.

