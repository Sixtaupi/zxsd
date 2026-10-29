C API reference
###############

The public API consists of the portable validator in ``zxsd.h``, the typed
binding runtime in ``zxsd_bind.h``, and an optional Mini-XML 4 adapter.
Generated headers add schema-specific accessor, validation, and
streaming-initializer functions, plus read and write functions when
``zxsdc --bindings`` is used.

See :doc:`compatibility` for release guarantees, generated descriptor epochs,
binary configuration requirements, and migration rules. ``zxsd_version.h``
provides release and epoch identifiers.

Compatibility checks
********************

.. doxygenfile:: zxsd_version.h
   :project: zxsd

.. doxygenstruct:: zxsd_runtime_config_t
   :project: zxsd
   :members:

.. code-block:: c

   zxsd_result_t zxsd_check_runtime(const zxsd_runtime_config_t *expected);

Initialize the stamp with ``ZXSD_RUNTIME_CONFIG_INIT`` in the application and
check it before using a separately built runtime. For bindings, the equivalent
``zxsd_bind_runtime_config_t`` and ``zxsd_bind_check_runtime()`` include binding
layout sizes; their declarations appear in the typed bindings reference below.
See :doc:`compatibility` for the complete startup example and limitations.

Validation runtime
******************

Core types
==========

.. doxygenenum:: zxsd_error_t
   :project: zxsd

.. doxygenstruct:: zxsd_result_t
   :project: zxsd
   :members:

.. doxygenstruct:: zxsd_allocator_t
   :project: zxsd
   :members:

.. doxygenstruct:: zxsd_schema_t
   :project: zxsd
   :members:

.. doxygenstruct:: zxsd_stream_t
   :project: zxsd
   :members:

.. doxygenstruct:: zxsd_xml_reader_t
   :project: zxsd
   :members:

Validation functions
====================

.. code-block:: c

   zxsd_result_t zxsd_validate(const zxsd_schema_t *schema, const char *xml);
   zxsd_result_t zxsd_validate_with_allocator(
           const zxsd_schema_t *schema,
           const char *xml,
           const zxsd_allocator_t *allocator);
   const char *zxsd_error_string(zxsd_error_t error);

``zxsd_validate()`` validates one NUL-terminated document. The allocator
variant supplies bounded scratch storage for identity constraints.
``zxsd_error_string()`` returns a stable static description.

Streaming functions
===================

.. code-block:: c

   zxsd_result_t zxsd_stream_init(zxsd_stream_t *stream,
                                  const zxsd_schema_t *schema);
   zxsd_result_t zxsd_start_element(zxsd_stream_t *stream,
                                    const char *namespace_uri,
                                    const char *local_name);
   zxsd_result_t zxsd_attribute(zxsd_stream_t *stream,
                                const char *namespace_uri,
                                const char *local_name,
                                const char *value);
   zxsd_result_t zxsd_xsi_type(zxsd_stream_t *stream,
                               const char *type_namespace_uri,
                               const char *type_local_name);
   zxsd_result_t zxsd_start_content(zxsd_stream_t *stream);
   zxsd_result_t zxsd_text(zxsd_stream_t *stream,
                           const char *text, size_t length);
   zxsd_result_t zxsd_end_element(zxsd_stream_t *stream,
                                  const char *namespace_uri,
                                  const char *local_name);
   zxsd_result_t zxsd_stream_finish(zxsd_stream_t *stream);

Initialize once, send decoded events in document order, and finish after the
root closes. The first error is sticky. A generated ``<schema>_stream_init()``
wrapper selects the default root descriptor.

The runtime selects a counter-based cursor for flat sequences, ``all`` groups,
and choices when their repetition rules permit incremental validation. These
models can process more children than ``ZXSD_STREAM_CHILD_CAPACITY``. Nested
sequences, choices, and more complex repetitions use a fixed continuation pool
shared by all open elements. It keeps viable occurrence partitions, not child
history, and merges equivalent states. Invalid prefixes fail before notifying
the raw reader's observer; missing children fail when the parent closes. The
effective model is selected after ``xsi:type`` processing.

``ZXSD_STREAM_MODEL_CAPACITY`` defaults to 128 continuation/frontier slots;
exhaustion returns the sticky ``ZXSD_ERR_STREAM_LIMIT`` result. This is an
application budget, not a bound on children. Nullable repetitions do not
enumerate empty iterations. Epsilon expansion is iterative and limited to the
square of the pool capacity per event, preventing pathological nullable models
from doing unbounded work. Setting the capacity to zero removes the pool and
selects the historical bounded token matcher for non-flat models. Non-XSD
hand-built trees containing nested ``all`` groups also use that fallback;
compiler-generated XSD 1.0 ``all`` groups retain the flat counter path.

The frame members exposed for static allocation are internal state. Rebuild
the library and consuming applications together after updating the headers;
generated schema descriptor layouts are unchanged.

Raw XML fragment functions
==========================

For typed delivery from the same fragments, see :doc:`bindings` and the
``zxsdc --stream-bind element/path`` generation option. It validates all input
while materializing only the selected subtree into a reusable caller arena.

.. code-block:: c

   zxsd_result_t zxsd_xml_reader_init(zxsd_xml_reader_t *reader,
                                      const zxsd_schema_t *schema);
   zxsd_result_t zxsd_xml_reader_init_with_allocator(
           zxsd_xml_reader_t *reader,
           const zxsd_schema_t *schema,
           const zxsd_allocator_t *allocator);
   zxsd_result_t zxsd_xml_reader_feed(zxsd_xml_reader_t *reader,
                                     const void *data, size_t length);
   void zxsd_xml_reader_set_event_callback(zxsd_xml_reader_t *reader,
                                           zxsd_xml_event_fn callback,
                                           void *user_data);
   zxsd_result_t zxsd_xml_reader_finish(zxsd_xml_reader_t *reader);
   void zxsd_xml_reader_dispose(zxsd_xml_reader_t *reader);

Feed arbitrary raw-byte fragments and call ``finish`` once at end of input.
The reader XML-decodes names and values, resolves namespaces, and emits events
to its embedded :c:struct:`zxsd_stream_t`. The first XML or validation error is
sticky. ``dispose`` releases optional identity-constraint scratch storage.
The optional observer receives transient decoded events only after the
validator accepts them; it can copy selected values into bounded application
storage without retaining the XML response.

The `complete Doxygen reference <doxygen/index.html>`_ is exhaustive: it also
lists every descriptor type, compile-time limit, callback typedef, and
generated-data structure used by ``zxsdc`` output. In a local build it is
available under ``doc/_build_doxygen/html``.

Typed bindings
**************

Structures and descriptors generated by ``zxsdc --bindings`` are interpreted
by this runtime. See :doc:`bindings` for the C representation and usage.

.. doxygenfile:: zxsd_bind.h
   :project: zxsd

Mini-XML adapter
****************

The adapter is built only when CMake finds Mini-XML 4 and
``ZXSD_USE_MXML=ON``. It walks an existing Mini-XML tree and feeds expanded
events to the same runtime.

.. doxygenfile:: zxsd_mxml.h
   :project: zxsd

