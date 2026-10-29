Getting started
###############

Requirements
************

* a C99 compiler;
* CMake 3.16 or newer;
* Python 3 to run ``zxsdc`` and the tests.

Prefer a compiler/runtime pair from the same release. On upgrades, follow
:doc:`compatibility`: legacy unversioned output must be regenerated, and all
0.x consumers must be rebuilt with matching memory limits and ABI flags.

Build and test the runtime
**************************

Linux and macOS:

.. code-block:: console

   $ cmake -S . -B build -DZXSD_USE_MXML=OFF
   $ cmake --build build
   $ ctest --test-dir build --output-on-failure

Windows PowerShell with Visual Studio Build Tools:

.. code-block:: powershell

   cmake -S . -B build -DZXSD_USE_MXML=OFF
   cmake --build build --config Release
   ctest --test-dir build -C Release --output-on-failure

Compile a schema
****************

Given ``device.xsd``, generate a validator in ``generated``:

.. code-block:: console

   $ ./zxsdc device.xsd -o generated

On Windows, invoke the extensionless script through Python:

.. code-block:: powershell

   python .\zxsdc device.xsd -o generated

The output contains ``device.zxsd.c`` and ``device.zxsd.h``. The header exposes
the generated schema accessor and convenience validation functions. File and
symbol names are derived from the XSD filename.

Validate a complete document
****************************

.. code-block:: c

   #include <stdio.h>
   #include "device.zxsd.h"

   int main(void)
   {
           const char xml[] =
                   "<device id='7'><name>node</name><sample>1</sample></device>";
           zxsd_result_t result = device_validate(xml);

           if (result.code != ZXSD_OK) {
                   fprintf(stderr, "%s at byte %lu (schema item: %s)\n",
                           result.message, (unsigned long)result.offset,
                           result.path != NULL ? result.path : "unknown");
                   return 1;
           }
           return 0;
   }

Compile the generated source together with the runtime, or use the supplied
CMake helper to generate and compile it during the build:

.. code-block:: cmake

   find_package(zxsd CONFIG REQUIRED)

   add_executable(application main.c)
   zxsd_generate(TARGET application SCHEMA device.xsd)
   target_link_libraries(application PRIVATE zxsd::zxsd)

Add ``BINDINGS`` to generate typed bindings. For composed schemas, pass files
reached through ``xs:include``, ``xs:import``, or ``xs:redefine`` to
``DEPENDS`` so CMake regenerates the output when they change. Repeated
``BIND_ROOT`` and ``SCHEMA_PATH`` arguments map to repeated compiler options;
``MAX_SCHEMA_COMPLEXITY`` sets the compiler limit.

The installed package exports ``zxsd::zxsd``, ``zxsd_generate()``, and the
host ``zxsdc`` location. Set ``CMAKE_PREFIX_PATH`` to the installation prefix
when it is outside CMake's normal search locations.

The complete cross-platform example in ``samples/host`` uses this helper:

.. code-block:: console

   $ cmake -S samples/host -B build-example -DZXSD_USE_MXML=OFF
   $ cmake --build build-example
   $ ./build-example/zxsd_example

Windows multi-configuration generators place the executable at
``build-example\Release\zxsd_example.exe``.

Consume parser events
*********************

The streaming API avoids retaining a complete XML document. Pass expanded
names: the namespace URI and local name are separate, and text and attribute
values must already be XML-decoded.

.. code-block:: c

   zxsd_stream_t stream;
   zxsd_result_t result = device_stream_init(&stream);

   if (result.code == ZXSD_OK)
           result = zxsd_start_element(&stream, NULL, "device");
   if (result.code == ZXSD_OK)
           result = zxsd_attribute(&stream, NULL, "id", "7");
   if (result.code == ZXSD_OK)
           result = zxsd_start_content(&stream);
   /* Send child start/text/end events in document order. */
   if (result.code == ZXSD_OK)
           result = zxsd_end_element(&stream, NULL, "device");
   if (result.code == ZXSD_OK)
           result = zxsd_stream_finish(&stream);

Every operation returns a sticky :c:struct:`zxsd_result_t`; stop sending events
after the first error. Resolve the QName value of ``xsi:type`` in the XML
frontend before calling :c:func:`zxsd_xsi_type`.

Consume raw XML fragments
*************************

Use :c:struct:`zxsd_xml_reader_t` when a socket or HTTP client returns raw XML
in pieces. Fragment boundaries may occur anywhere in the document, and the
reader does not retain pointers into the input buffer.

.. code-block:: c

   zxsd_xml_reader_t reader;
   zxsd_result_t result = zxsd_xml_reader_init(&reader, device_schema());

   while (result.code == ZXSD_OK && receive_fragment(buffer, sizeof(buffer), &length))
           result = zxsd_xml_reader_feed(&reader, buffer, length);
   if (result.code == ZXSD_OK)
           result = zxsd_xml_reader_finish(&reader);
   zxsd_xml_reader_dispose(&reader);

The embedded validator preserves the event API's sticky first error. Storage
for an incomplete XML token is fixed by ``ZXSD_XML_TOKEN_CAPACITY`` and decoded
text is bounded by ``ZXSD_MAX_TEXT_LENGTH``.
Install an :c:type:`zxsd_xml_event_fn` with
:c:func:`zxsd_xml_reader_set_event_callback` to copy a compact application
summary while the complete document continues to be validated.

Memory policy
*************

Set ``ZXSD_NO_HEAP=ON`` for a zero-heap runtime. Schemas without identity
constraints then validate without allocation. Identity constraints require
bounded scratch storage and can use an application-owned
:c:struct:`zxsd_allocator_t` through
:c:func:`zxsd_validate_with_allocator`.

All ``ZXSD_MAX_*`` values can be overridden at compile time. Exceeding a limit
returns a resource or streaming error; the runtime does not grow without a
bound. See :doc:`resource-usage` for measured Cortex-M figures.

