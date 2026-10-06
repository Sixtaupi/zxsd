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
   * - ``CONFIG_MXSD_MAX_SCHEMA_COMPLEXITY``
     - 4096
     - Maximum aggregate runtime-visible descriptor count.

``CONFIG_MXSD_NO_HEAP`` defaults to enabled. Schemas without identity
constraints remain fully usable. A schema requiring identity tuple storage
returns :c:enumerator:`MXSD_ERR_RESOURCE_LIMIT` unless validation is invoked
with an application allocator.

Choose limits from the largest trusted schema and document the device accepts,
then exercise the boundaries with tests. See :doc:`resource-usage` for the
relationship between limits and memory.

