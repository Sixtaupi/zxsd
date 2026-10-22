Zephyr integration
##################

mxsd is a Zephyr module. Its root ``zephyr/module.yml`` points Zephyr to the
module CMake and Kconfig files; schema compilation still happens on the host.

Add the module
**************

In a west workspace, add mxsd to the manifest alongside the application:

.. code-block:: yaml

   manifest:
     projects:
       - name: mxsd
         url: https://github.com/<organization>/mxsd
         revision: main
         path: modules/lib/mxsd

After ``west update``, Zephyr discovers the module. For a standalone checkout,
pass the repository with ``-DZEPHYR_MODULES=/absolute/path/to/mxsd``. The
supplied sample already appends its repository root, so from an initialized
Zephyr environment this is normally sufficient:

.. code-block:: console

   $ west build -b qemu_x86 samples/zephyr/basic
   $ west build -t run

Run the tests
*************

The automated Zephyr validation suite lives under ``tests/zephyr`` rather than
in the sample applications. Twister builds and runs it on ``qemu_x86``:

.. code-block:: console

   $ west twister -T tests/zephyr -p qemu_x86 --inline-logs

The suite compiles the portable ``tests/test_*.c`` files directly and calls
their shared entry points from ztest wrappers. It exercises validation,
streaming, schema composition, identity constraints, and typed bindings in an
explicit ``CONFIG_MXSD_NO_HEAP=y`` runtime configuration. ``test_mxml.c`` is
the sole exception because Zephyr uses the built-in XML backend rather than
the optional Mini-XML adapter. Files under ``samples/zephyr`` remain example
applications and are built separately.

Configure the runtime
*********************

Enable mxsd in ``prj.conf`` and choose limits for the application:

.. code-block:: kconfig

   CONFIG_MXSD=y
   CONFIG_MXSD_NO_HEAP=y
   CONFIG_MXSD_MAX_DEPTH=16
   CONFIG_MXSD_MAX_TEXT_LENGTH=256

Every option is described in :doc:`kconfig`. Lower limits reduce worst-case
memory, but documents exceeding them fail deterministically.

Generate and link a schema
**************************

Generate validators as a CMake build step in a production application with
the same helper used by the host example:

.. code-block:: cmake

   include(${MXSD_ROOT}/cmake/mxsdGenerate.cmake)

   target_sources(app PRIVATE src/main.c)
   mxsd_generate(TARGET app SCHEMA device.xsd)

``mxsdc`` and Python run on the build host, while Zephyr's target toolchain
compiles the generated C. Add ``BINDINGS`` for typed bindings and list
secondary schema files with ``DEPENDS``. The target device never needs Python
or the XSD files.


Typed bindings
**************

To read and write documents through generated C structures, add ``BINDINGS``
to ``mxsd_generate()``; the helper compiles ``<schema>.bind.c`` with the
application. Also enable the runtime support:

.. code-block:: kconfig

   CONFIG_MXSD_BINDINGS=y

``samples/zephyr/bindings`` is a complete application: it reads a settings
document into structures, updates them, and writes a validated document. Its
console output is checked by Twister:

.. code-block:: console

   $ west twister -T samples/zephyr/bindings -p qemu_x86 --inline-logs

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
