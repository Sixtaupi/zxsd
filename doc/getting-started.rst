Getting started
###############

Requirements
************

* a C99 compiler;
* CMake 3.16 or newer;
* Python 3 to run ``mxsdc`` and the tests.

Build and test the runtime
**************************

Linux and macOS:

.. code-block:: console

   $ cmake -S . -B build -DMXSD_USE_MXML=OFF
   $ cmake --build build
   $ ctest --test-dir build --output-on-failure

Windows PowerShell with Visual Studio Build Tools:

.. code-block:: powershell

   cmake -S . -B build -DMXSD_USE_MXML=OFF
   cmake --build build --config Release
   ctest --test-dir build -C Release --output-on-failure

Compile a schema
****************

Given ``device.xsd``, generate a validator in ``generated``:

.. code-block:: console

   $ ./mxsdc device.xsd -o generated

On Windows, invoke the extensionless script through Python:

.. code-block:: powershell

   python .\mxsdc device.xsd -o generated

The output contains ``device.mxsd.c`` and ``device.mxsd.h``. The header exposes
the generated schema accessor and convenience validation functions. File and
symbol names are derived from the XSD filename.

Validate a complete document
****************************

.. code-block:: c

   #include <stdio.h>
   #include "device.mxsd.h"

   int main(void)
   {
           const char xml[] =
                   "<device id='7'><name>node</name><sample>1</sample></device>";
           mxsd_result_t result = device_validate(xml);

           if (result.code != MXSD_OK) {
                   fprintf(stderr, "%s at byte %lu (schema item: %s)\n",
                           result.message, (unsigned long)result.offset,
                           result.path != NULL ? result.path : "unknown");
                   return 1;
           }
           return 0;
   }

Compile the generated source together with the runtime, or let CMake link the
``mxsd::mxsd`` target. The complete cross-platform example in
``samples/host`` automates generation:

.. code-block:: console

   $ cmake -S samples/host -B build-example -DMXSD_USE_MXML=OFF
   $ cmake --build build-example
   $ ./build-example/mxsd_example

Windows multi-configuration generators place the executable at
``build-example\Release\mxsd_example.exe``.

Consume parser events
*********************

The streaming API avoids retaining a complete XML document. Pass expanded
names: the namespace URI and local name are separate, and text and attribute
values must already be XML-decoded.

.. code-block:: c

   mxsd_stream_t stream;
   mxsd_result_t result = device_stream_init(&stream);

   if (result.code == MXSD_OK)
           result = mxsd_start_element(&stream, NULL, "device");
   if (result.code == MXSD_OK)
           result = mxsd_attribute(&stream, NULL, "id", "7");
   if (result.code == MXSD_OK)
           result = mxsd_start_content(&stream);
   /* Send child start/text/end events in document order. */
   if (result.code == MXSD_OK)
           result = mxsd_end_element(&stream, NULL, "device");
   if (result.code == MXSD_OK)
           result = mxsd_stream_finish(&stream);

Every operation returns a sticky :c:struct:`mxsd_result_t`; stop sending events
after the first error. Resolve the QName value of ``xsi:type`` in the XML
frontend before calling :c:func:`mxsd_xsi_type`.

Memory policy
*************

Set ``MXSD_NO_HEAP=ON`` for a zero-heap runtime. Schemas without identity
constraints then validate without allocation. Identity constraints require
bounded scratch storage and can use an application-owned
:c:struct:`mxsd_allocator_t` through
:c:func:`mxsd_validate_with_allocator`.

All ``MXSD_MAX_*`` values can be overridden at compile time. Exceeding a limit
returns a resource or streaming error; the runtime does not grow without a
bound. See :doc:`resource-usage` for measured Cortex-M figures.

