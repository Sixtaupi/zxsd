Typed bindings
##############

Validation answers whether a document is valid. Typed bindings also convert
between a valid document and generated C structures, so applications can read
values and produce documents without handling XML text or parser events.

Bindings are optional. ``mxsdc --bindings`` writes two more files beside the
validator, and the reader and writer live in ``src/mxsd_bind.c``. They are
interpreted from constant descriptor tables, like the validator, and never use
the heap.

Generating bindings
*******************

.. code-block:: console

   $ python3 mxsdc device.xsd -o generated --bindings

This produces ``device.mxsd.[ch]`` (the validator, byte-identical to a build
without ``--bindings``) and ``device.bind.[ch]``. A structure is generated for
every complex type reachable from a bound root, and four functions are
generated for each global element with a complex type:

.. code-block:: c

   mxsd_result_t device_device_read(const char *xml, device_Device_t *object,
                                    mxsd_bind_arena_t *arena);
   mxsd_result_t device_device_write(const device_Device_t *object,
                                     mxsd_bind_sink_fn sink, void *context,
                                     mxsd_stream_t *validator);
   mxsd_result_t device_device_write_buffer(const device_Device_t *object,
                                            char *buffer, size_t capacity,
                                            size_t *length);
   const mxsd_bind_root_t *device_device_binding(void);

``--bind-root NAME`` restricts generation to selected roots, by local name or
as ``{namespace}name``. It can be repeated. Use it when another root of the same
schema uses a construct that bindings cannot represent.

Generation is all or nothing: if any reachable construct cannot be represented
faithfully, ``mxsdc`` fails with a diagnostic starting with ``bindings:`` and
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
   mxsd_bind_arena_t arena;
   device_Device_t device;

   mxsd_bind_arena_init(&arena, storage, sizeof(storage));
   mxsd_result_t r = device_device_read(xml, &device, &arena);
   if (r.code == MXSD_OK) {
           printf("%s has %zu samples\n", device.name, device.sample_count);
   }

The document is validated first, with the same rules as ``mxsd_validate()``.
Strings, arrays and optional structures are then allocated in the arena, so
the structure stays valid as long as the arena storage does. Text is
entity-decoded, CDATA is included, and the type's ``whiteSpace`` facet is
applied. Schema defaults are applied: an absent attribute with a default or
fixed value is reported as present, and an element present with empty content
takes its default.

On failure the structure is zeroed and the arena is restored, so no pointer
refers to released storage. ``MXSD_ERR_RESOURCE_LIMIT`` means that the arena
is too small. Measure the largest document the device accepts and keep a
margin: the peak includes the decoded text of numeric values, which is
released once they are converted.

Reading runs after the validation pass has returned and does not add to its
stack peak: it keeps one parser state of about 7 KiB with the default limits
and recurses once per nesting level with a small frame, bounded by
``MXSD_MAX_DEPTH``. Validation therefore sets the peak. Its per-level text
buffers are sized by ``MXSD_MAX_TEXT_LENGTH``: on ``qemu_x86``, the Zephyr
sample peaks at about 26 KiB with the default of 1024 bytes and 15.7 KiB with
128 bytes.

A valid document can still be impossible to represent. ``xsi:type`` naming a
derived type, or ``xsi:nil="true"``, makes the reader return
``MXSD_ERR_UNSUPPORTED`` instead of silently dropping information.

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
   mxsd_result_t r = device_device_write_buffer(&device, xml, sizeof(xml), &length);

``*_write_buffer()`` writes a NUL-terminated document and validates it with
``mxsd_validate()``, so facets, choices and identity constraints are checked
before the result is trusted. ``*_write()`` streams to a callback instead. Pass
a streaming validator initialized for the same root to validate while writing
without keeping the document in memory.

The writer itself rejects what the C representation can express incorrectly:
a required ``NULL`` string or structure, an array count outside its occurrence
bounds, an out-of-range enumeration, text that is not valid UTF-8 or contains
characters outside XML 1.0, and object graphs nested deeper than
``MXSD_MAX_DEPTH``, which includes accidental cycles. Output is deterministic:
namespaces are declared once on the root as ``ns1``, ``ns2`` and so on, and
characters that XML would otherwise normalize, such as tab and line feed in
attributes or carriage return in text, are written as character references.

The Zephyr application in ``samples/zephyr/bindings`` reads, updates and writes
a settings document with these functions on ``qemu_x86``.

Zero-heap builds
****************

Reading and writing never allocate. Validation of identity constraints needs
scratch memory exactly as for ``mxsd_validate()``. With ``MXSD_NO_HEAP``, use
``mxsd_bind_read_with_allocator()`` and
``mxsd_bind_write_buffer_with_allocator()`` with the root returned by
``<schema>_<root>_binding()``, or initialize the streaming validator with
``mxsd_stream_init_with_allocator()``. On Zephyr, enable
``CONFIG_MXSD_BINDINGS``.

Constructs that cannot be bound
*******************************

These constructs are fully supported by the validator. Bindings reject them
because a fixed C structure cannot represent them without losing information:

* element wildcards (``xs:any``) and attribute wildcards (``xs:anyAttribute``);
* mixed content and ``xs:anyType`` elements;
* model groups that repeat (``maxOccurs`` greater than one on ``sequence``,
  ``choice`` or ``all``), since the interleaving of their elements would be lost;
* the same element name at several places of one content model;
* nillable elements, abstract elements, and heads of substitution groups;
* abstract complex types, which are only usable through ``xsi:type``;
* ``QName`` and ``NOTATION`` values, whose meaning depends on namespace
  declarations.
