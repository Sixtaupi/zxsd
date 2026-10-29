Zephyr integration
##################

zxsd is a Zephyr module. Its root ``zephyr/module.yml`` points Zephyr to the
module CMake and Kconfig files; schema compilation still happens on the host.

Add the module
**************

In a west workspace, add zxsd to the manifest alongside the application:

.. code-block:: yaml

   manifest:
     projects:
       - name: zxsd
         url: https://github.com/sixtaupi/zxsd
         revision: main
         path: modules/lib/zxsd

After ``west update``, Zephyr discovers the module. For a standalone checkout,
pass the repository with ``-DZEPHYR_MODULES=/absolute/path/to/zxsd``. The
supplied samples already append their repository root, so from an initialized
Zephyr environment this is normally sufficient:

.. code-block:: console

   $ west build -b qemu_x86 samples/zephyr/xsd-validation
   $ west build -t run

``samples/zephyr/xsd-validation`` is the minimal application: ``device.xsd`` is
compiled on the build host with ``zxsd_generate()``, and ``main()`` validates a
conforming and a non-conforming document with the generated
``device_validate()``, printing the result. Its console output is checked by
Twister:

.. code-block:: console

   $ west twister -T samples/zephyr/xsd-validation -p qemu_x86 --inline-logs

``samples/zephyr/xsd-validation-mxml`` is the same application for code that
already parses XML with Mini-XML 4: it loads each document with
``mxmlLoadString()`` and validates the tree with ``zxsd_mxml_validate()``.
``CONFIG_MXML`` comes from a Mini-XML Zephyr module, which must be present in
the west workspace; when it is enabled, the zxsd module builds its Mini-XML
backend in place of the built-in one, as host builds do. Validation of XML text
is unchanged. Mini-XML allocates the tree from the libc heap. Twister skips
the sample when no ``mxml`` module is available:

.. code-block:: console

   $ west twister -T samples/zephyr/xsd-validation-mxml -p qemu_x86 --inline-logs

Run the tests
*************

The automated Zephyr validation suite lives under ``tests/zephyr`` rather than
in the sample applications. Twister builds and runs it on ``qemu_x86``:

.. code-block:: console

   $ west twister -T tests/zephyr -p qemu_x86 --inline-logs

The suite compiles the portable ``tests/test_*.c`` files directly and calls
their shared entry points from ztest wrappers. It exercises validation,
streaming, schema composition, identity constraints, and typed bindings in an
explicit ``CONFIG_ZXSD_NO_HEAP=y`` runtime configuration. ``test_mxml.c`` is
the sole exception because Zephyr uses the built-in XML backend rather than
the optional Mini-XML adapter. Files under ``samples/zephyr`` remain example
applications and are built separately.

Configure the runtime
*********************

Enable zxsd in ``prj.conf`` and choose limits for the application:

.. code-block:: kconfig

   CONFIG_ZXSD=y
   CONFIG_ZXSD_NO_HEAP=y
   CONFIG_ZXSD_MAX_DEPTH=16
   CONFIG_ZXSD_MAX_TEXT_LENGTH=256

Every option is described in :doc:`kconfig`. Lower limits reduce worst-case
memory, but documents exceeding them fail deterministically.

``python3 zxsdc device.xsd -o generated --zephyr-config`` emits a schema-specific
``device.conf`` and compile-time limit checks. Review its commented application
budgets and pass the fragment with ``-DEXTRA_CONF_FILE`` before Zephyr's
configuration phase. The :doc:`kconfig` guide explains combining several schemas
and distinguishing finite schema requirements from input policies.

Generate and link a schema
**************************

Generate validators as a CMake build step in a production application with
the same helper used by the host example:

.. code-block:: cmake

   include(${ZXSD_ROOT}/cmake/zxsdGenerate.cmake)

   target_sources(app PRIVATE src/main.c)
   zxsd_generate(TARGET app SCHEMA device.xsd)

``zxsdc`` and Python run on the build host, while Zephyr's target toolchain
compiles the generated C. Add ``BINDINGS`` for typed bindings and list
secondary schema files with ``DEPENDS``. The target device never needs Python
or the XSD files.


Typed bindings
**************

To read and write documents through generated C structures, add ``BINDINGS``
to ``zxsd_generate()``; the helper compiles ``<schema>.bind.c`` with the
application. Also enable the runtime support:

.. code-block:: kconfig

   CONFIG_ZXSD_BINDINGS=y

``samples/zephyr/bindings`` is a complete application: it reads a settings
document into structures, updates them, and writes a validated document. Its
console output is checked by Twister:

.. code-block:: console

   $ west twister -T samples/zephyr/bindings -p qemu_x86 --inline-logs

``samples/zephyr/bindings-stream`` does the same with the fragment-based APIs
only, so that no complete document is held in memory: it feeds the received
document to a ``STREAM_BIND`` reader in fragments, writes through a sink with a
streaming validator, and validates the written bytes with an
``zxsd_xml_reader_t`` as they arrive:

.. code-block:: console

   $ west twister -T samples/zephyr/bindings-stream -p qemu_x86 --inline-logs

See :doc:`bindings` for the C representation and memory use.

REST XML client
***************

``samples/zephyr/rest`` is a networked application using Zephyr's HTTP client.
It retrieves an XML resource from httpbin, validates and deserializes it through
generated bindings, modifies the typed C structure, then serializes and
validates an XML request body before posting it back. See the sample README for
network setup and transport-security notes. The application keeps
``CONFIG_HEAP_MEM_POOL_SIZE=0`` and uses Zephyr's fixed-pool asynchronous DNS
API instead of the allocating ``getaddrinfo()`` convenience function.

Interactive weather client
**************************

``samples/zephyr/weather`` prompts for a city, resolves it through Nominatim,
then requests MET Norway's XML forecast for the resulting coordinates. Both
HTTPS responses are validated before the application displays the place,
temperature, humidity, wind, pressure, and cloud cover. The sample keeps the
Zephyr system heap disabled; its TLS implementation uses a separate fixed
mbedTLS arena. HTTP body fragments feed a generated typed reader selecting
``weatherdata/product/time``. Its callback copies a compact summary from typed
forecasts using a reusable 4096-byte arena, then requests validation-only
processing. Display waits for successful full-document finish.
The much smaller Nominatim response demonstrates the basic typed-binding API.

.. code-block:: console

   $ west twister -T samples/zephyr/weather -p qemu_x86 --inline-logs
