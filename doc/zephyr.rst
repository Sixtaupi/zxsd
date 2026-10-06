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

   $ west build -b qemu_x86 samples/zephyr
   $ west build -t run

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

Generate validators as a CMake build step in a production application. The
sample performs generation at configure time for clarity:

.. code-block:: cmake

   find_package(Python3 REQUIRED COMPONENTS Interpreter)
   set(MXSD_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated)
   file(MAKE_DIRECTORY ${MXSD_GEN})
   execute_process(
     COMMAND ${Python3_EXECUTABLE} ${MXSD_ROOT}/mxsdc
             ${CMAKE_CURRENT_SOURCE_DIR}/device.xsd -o ${MXSD_GEN}
     COMMAND_ERROR_IS_FATAL ANY)
   target_sources(app PRIVATE src/main.c ${MXSD_GEN}/device.mxsd.c)
   target_include_directories(app PRIVATE ${MXSD_GEN})

For reproducible releases, generated files may instead be committed or
produced in a dedicated host-generation stage. The target never needs Python
or the XSD file.

