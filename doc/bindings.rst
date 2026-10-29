Typed bindings
##############

Validation answers whether a document is valid. Typed bindings also convert
between a valid document and generated C structures, so applications can read
values and produce documents without handling XML text or parser events.

Bindings are optional. ``zxsdc --bindings`` writes two more files beside the
validator, and the reader and writer live in ``src/zxsd_bind.c``. They are
interpreted from constant descriptor tables, like the validator, and never use
the heap.

Generated bindings check runtime, descriptor, and binding ABI epochs at compile
and link time. See :doc:`compatibility` before upgrading or linking a separately
built runtime; use ``zxsd_bind_check_runtime()`` at that boundary. Regenerate
all binding headers and sources together when the XSD or options change.

Generating bindings
*******************

.. code-block:: console

   $ python3 zxsdc device.xsd -o generated --bindings

This produces ``device.zxsd.[ch]`` (the validator, byte-identical to a build
without ``--bindings``) and ``device.bind.[ch]``. A structure is generated for
every complex type reachable from a bound root, and four functions are
generated for each global element with a complex type:

.. code-block:: c

   zxsd_result_t device_device_read(const char *xml, device_Device_t *object,
                                    zxsd_bind_arena_t *arena);
   zxsd_result_t device_device_write(const device_Device_t *object,
                                     zxsd_bind_sink_fn sink, void *context,
                                     zxsd_stream_t *validator);
   zxsd_result_t device_device_write_buffer(const device_Device_t *object,
                                            char *buffer, size_t capacity,
                                            size_t *length);
   const zxsd_bind_root_t *device_device_binding(void);

``--bind-root NAME`` restricts generation to selected roots, by local name or
as ``{namespace}name``. It can be repeated. Use it when another root of the same
schema uses a construct that bindings cannot represent.

Generation is all or nothing: if any reachable construct cannot be represented
faithfully, ``zxsdc`` fails with a diagnostic starting with ``bindings:`` and
writes no files.

C representation
****************

.. list-table:: Values
   :header-rows: 1
   :widths: 40 60

   * - XSD type
     - C type
   * - ``boolean``
     - ``bool``
   * - ``byte``, ``short``, ``int``, ``long``
     - ``int8_t``, ``int16_t``, ``int32_t``, ``int64_t``
   * - ``unsignedByte``, ``unsignedShort``, ``unsignedInt``, ``unsignedLong``
     - ``uint8_t``, ``uint16_t``, ``uint32_t``, ``uint64_t``
   * - string-derived restriction with enumerations
     - generated C ``enum``, numbered in declaration order
   * - every other simple type, including ``decimal``, ``integer``, ``float``,
       ``double``, dates, binary types, lists and unions
     - ``const char *``, holding the whitespace-normalized lexical value

``float``, ``double`` and ``decimal`` stay textual on purpose: the conversion is
then exact and independent of the C library's locale and floating-point
formatting support. Restrictions of a native type keep the native type; their
facets are enforced by validation.

.. list-table:: Members
   :header-rows: 1
   :widths: 40 60

   * - Occurrence
     - Member
   * - required value
     - the value; a required child structure is embedded by value
   * - optional scalar or enumeration
     - ``bool has_<name>`` and the value
   * - optional string
     - ``const char *``; ``NULL`` means absent
   * - optional child structure
     - ``const T *``; ``NULL`` means absent
   * - ``maxOccurs`` greater than one
     - ``const T *<name>`` and ``size_t <name>_count``
   * - recursive child structure
     - ``const T *``, even when required

Attributes come first, then child elements in content-model order, then the
``value`` member of a simpleContent type. Derived types are flattened: an
extension contains the inherited attributes and elements. Members keep the XML
local name. Names that are C or C++ keywords get a trailing underscore, and an
attribute and an element with the same name are distinguished with ``_attr`` or
``_elem``. Elements of a ``choice`` are optional members; exactly one must be
set when writing.

Reading
*******

.. code-block:: c

   static unsigned char storage[2048];
   zxsd_bind_arena_t arena;
   device_Device_t device;

   zxsd_bind_arena_init(&arena, storage, sizeof(storage));
   zxsd_result_t r = device_device_read(xml, &device, &arena);
   if (r.code == ZXSD_OK) {
           printf("%s has %zu samples\n", device.name, device.sample_count);
   }

The document is validated first, with the same rules as ``zxsd_validate()``.
Strings, arrays and optional structures are then allocated in the arena, so
the structure stays valid as long as the arena storage does. Text is
entity-decoded, CDATA is included, and the type's ``whiteSpace`` facet is
applied. Schema defaults are applied: an absent attribute with a default or
fixed value is reported as present, and an element present with empty content
takes its default.

On failure the structure is zeroed and the arena is restored, so no pointer
refers to released storage. ``ZXSD_ERR_RESOURCE_LIMIT`` means that the arena
is too small. Measure the largest document the device accepts and keep a
margin: the peak includes the decoded text of numeric values, which is
released once they are converted.

Reading runs after the validation pass has returned and does not add to its
stack peak: it keeps one parser state of about 7 KiB with the default limits
and recurses once per nesting level with a small frame, bounded by
``ZXSD_MAX_DEPTH``. Validation therefore sets the peak. Its per-level text
buffers are sized by ``ZXSD_MAX_TEXT_LENGTH``: on ``qemu_x86``, the Zephyr
sample peaks at about 26 KiB with the default of 1024 bytes and 15.7 KiB with
128 bytes.

A valid document can still be impossible to represent. ``xsi:type`` naming a
derived type, or ``xsi:nil="true"``, makes the reader return
``ZXSD_ERR_UNSUPPORTED`` instead of silently dropping information.

Reading fragments with typed callbacks
****************************************

Select a root or a nested element at generation time:

.. code-block:: console

   $ python3 zxsdc met_weather.xsd -o generated \
       --stream-bind weatherdata/product/time

This emits the ordinary ``.zxsd.[ch]`` validator, plus ``.bind.[ch]`` containing
the selected type and a typed reader/callback interface. Full-document object
bindings are not enabled unless ``--bindings`` is also passed. Unselected
ancestors and siblings need not be representable as C structures: they still
receive complete schema validation, including facets and identity constraints.
The selected subtree itself follows the ordinary binding support contract.

.. code-block:: c

   static met_weather_weatherdata_product_time_reader_t reader;
   static met_weather_timeType_t forecast;
   static unsigned char storage[4096];
   static zxsd_bind_arena_t arena;

   static zxsd_bind_action_t on_forecast(void *context,
                                       const met_weather_timeType_t *value)
   {
           /* Copy needed values into application-owned storage here. */
           return ZXSD_BIND_CONTINUE;
   }

   zxsd_bind_arena_init(&arena, storage, sizeof(storage));
   zxsd_result_t r = met_weather_weatherdata_product_time_reader_init(
           &reader, &forecast, &arena, on_forecast, application_context, NULL);
   /* For every received HTTP body fragment, while r.code == ZXSD_OK: */
   r = met_weather_weatherdata_product_time_reader_feed(&reader, bytes, count);
   /* Once the complete HTTP body has arrived: */
   r = met_weather_weatherdata_product_time_reader_finish(&reader);
   met_weather_weatherdata_product_time_reader_dispose(&reader);

In CMake use ``zxsd_generate(TARGET app SCHEMA met_weather.xsd STREAM_BIND
weatherdata/product/time)``. ``STREAM_BIND`` accepts a list of paths. On Zephyr,
enable ``CONFIG_ZXSD_BINDINGS``. The generic functions are
``zxsd_bind_stream_init/feed/finish/dispose()``; generated wrappers adapt their
callbacks without nonportable function-pointer conversions.

Path rules
==========

Paths are finite absolute element paths, optionally starting with ``/``.
Local names work when unambiguous; use ``{namespace-uri}name`` for an explicit
expanded name. URI slashes inside braces are preserved. There are no prefixes,
wildcards, predicates, descendant axes, attributes, or runtime XPath engine.
Unknown or ambiguous steps, duplicate selections, and selected constructs
outside the binding subset fail generation explicitly. An element with a
simple type is delivered as a generated structure with a ``value`` member.
Finite paths through recursive schema types are supported.

Each repeated ``--stream-bind`` option generates an independent reader for
that selection, not simultaneous subscriptions on one reader. Reader symbols
join local path names with underscores; deterministic suffixes resolve naming
collisions. The ordinary binding output stays unchanged without the option.

Lifetime and validation
=======================

The raw XML is parsed and validated once. A callback runs after the selected
element closes successfully. Values split across fragments are reassembled
by the validator's existing bounded text storage, then converted with the
same normalization, native integer/boolean/enum, and default rules as the
full-document reader. No XML substring is retained or reparsed for binding.

The caller provides a reusable output structure and an arena. Their contents
and pointers are valid **only during the callback**. The output is cleared
and the arena restored to its initial usage after each delivery, on error,
and on dispose. Do not feed, finish, dispose, or reset the arena from inside
the callback. Copy retained values into independent application storage.

Return ``ZXSD_BIND_CONTINUE`` to receive later occurrences,
``ZXSD_BIND_VALIDATE_ONLY`` to stop materializing but keep validating all
remaining input, or ``ZXSD_BIND_CALLBACK_ERROR`` to stop with
``ZXSD_ERR_OUTPUT``. Later invalid XML or a failed key/keyref can invalidate
the document **after a callback already ran**. Treat deliveries as provisional
and only publish application results after a successful ``*_reader_finish()``.
The weather sample follows this rule even after retaining its first summary.

Memory and failure rules
========================

There is no implicit heap allocation. Streaming binding adds a fixed per-depth
binding stack to ``zxsd_xml_reader_t``. Only the currently selected subtree is
materialized. Its arrays grow geometrically in the explicit arena, without
input-based preallocation from ``maxOccurs``. Old array blocks remain in the
bump arena until delivery, so reserve growth/alignment overhead as well as
strings and nested structures. No repeated value is silently truncated.

The arena must fit the largest selected occurrence, not the whole response.
Choose a finer selection when one subtree is too large.
``ZXSD_ERR_RESOURCE_LIMIT`` reports arena exhaustion; parser/validator capacities
keep their ordinary errors. The first failure is sticky, and no later callback
runs after it. A derived ``xsi:type`` or unsupported nil inside a materialized
subtree is rejected, while unselected dynamic content is still validated.
For document-wide identity constraints, pass an explicit scratch allocator to
the generated initializer; the same bounded canonical-XML scratch requirement
as the event validator applies. Dispose releases that scratch even on error.

Writing
*******

.. code-block:: c

   static const int32_t samples[] = {7, 9};
   device_Device_t device = {0};
   char xml[256];
   size_t length;

   device.id = 42u;
   device.name = "sensor";
   device.sample = samples;
   device.sample_count = 2u;
   zxsd_result_t r = device_device_write_buffer(&device, xml, sizeof(xml), &length);

``*_write_buffer()`` writes a NUL-terminated document and validates it with
``zxsd_validate()``, so facets, choices and identity constraints are checked
before the result is trusted. ``*_write()`` streams to a callback instead. Pass
a streaming validator initialized for the same root to validate while writing
without keeping the document in memory.

The writer itself rejects what the C representation can express incorrectly:
a required ``NULL`` string or structure, an array count outside its occurrence
bounds, an out-of-range enumeration, text that is not valid UTF-8 or contains
characters outside XML 1.0, and object graphs nested deeper than
``ZXSD_MAX_DEPTH``, which includes accidental cycles. Output is deterministic:
namespaces are declared once on the root as ``ns1``, ``ns2`` and so on, and
characters that XML would otherwise normalize, such as tab and line feed in
attributes or carriage return in text, are written as character references.

The Zephyr application in ``samples/zephyr/bindings`` reads, updates and writes
a settings document with these functions on ``qemu_x86``.
``samples/zephyr/bindings-stream`` handles the same document with the
fragment-based APIs only: a ``--stream-bind`` reader fed in fragments, a
writer sink with a streaming validator, and a raw ``zxsd_xml_reader_t`` on the
receiving side.

Zero-heap builds
****************

Reading and writing never allocate. Validation of identity constraints needs
scratch memory exactly as for ``zxsd_validate()``. With ``ZXSD_NO_HEAP``, use
``zxsd_bind_read_with_allocator()`` and
``zxsd_bind_write_buffer_with_allocator()`` with the root returned by
``<schema>_<root>_binding()``, or initialize the streaming validator with
``zxsd_stream_init_with_allocator()``. On Zephyr, enable
``CONFIG_ZXSD_BINDINGS``.

Constructs that cannot be bound
*******************************

These constructs are fully supported by the validator. Bindings reject them
because a fixed C structure cannot represent them without losing information:

* element wildcards (``xs:any``) and attribute wildcards (``xs:anyAttribute``);
* mixed content and ``xs:anyType`` elements;
* repeating ``choice`` and ``all`` groups, finite repeating ``sequence``
  groups, and unbounded ``sequence`` groups with required members, since their
  occurrence correlation would be lost;
* the same element name at several places of one content model;
* nillable elements, abstract elements, and heads of substitution groups;
* abstract complex types, which are only usable through ``xsi:type``;
* ``QName`` and ``NOTATION`` values, whose meaning depends on namespace
  declarations.

An unbounded ``sequence`` whose members are all optional and uniquely named is
representable: each member is generated as its own typed array. This supports
schemas such as MET Norway's weather format without discarding XML content.
