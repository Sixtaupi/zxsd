.. _zxsd_xsd_validation_sample:

zxsd XSD validation
###################

Overview
********

This sample validates XML documents against a known schema. ``device.xsd`` is
compiled on the build host with the ``zxsd_generate()`` CMake helper, which
runs ``zxsdc`` and generates C descriptors and a ``device_validate()`` entry
point. The target never parses the schema. On the target, the sample:

#. validates a document that conforms to ``device.xsd``;
#. validates a document that lacks the required ``sample`` element and prints
   the structured error returned by the validator.

No heap is used.

Resource limits
***************

``prj.conf`` sets the runtime limits in two groups, as recommended in the
Kconfig reference:

* schema-derived limits (attributes, depth, particles, names, regex and
  content-model capacities), copied after review from the fragment printed by
  ``python3 zxsdc device.xsd -o generated --zephyr-config``;
* application budgets that the XSD cannot bound: ``CONFIG_ZXSD_MAX_INPUT``
  and ``CONFIG_ZXSD_MAX_TEXT_LENGTH``.

``CMakeLists.txt`` passes ``ZEPHYR_CONFIG`` to ``zxsd_generate()``, so the
generated validator checks the schema-derived values at compile time: the
build fails if one of them drops below what ``device.xsd`` requires. When the
schema changes, regenerate the fragment and update ``prj.conf``.

Building and running
********************

From an initialized Zephyr environment:

.. code-block:: console

   $ west build -b qemu_x86 samples/zephyr/xsd-validation
   $ west build -t run

Expected output:

.. code-block:: console

   valid document: valid
   invalid document: invalid, missing required element (device) at offset 32
   zxsd XSD validation sample done

To validate a tree that the application already parses with Mini-XML, see
the ``samples/zephyr/xsd-validation-mxml`` sample.

To validate your own documents, replace ``device.xsd`` and the documents in
``src/main.c``. The generated function is named after the schema file:
``<stem>_validate()`` in ``<stem>.zxsd.h``.
