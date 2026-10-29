.. _zxsd_xsd_validation_mxml_sample:

zxsd Mini-XML validation
########################

Overview
********

This sample is for applications that already parse XML with Mini-XML 4.
Instead of validating the XML text, zxsd validates the Mini-XML tree against a
known schema. ``device.xsd`` is compiled on the build host with the
``zxsd_generate()`` CMake helper; the target never parses the schema. On the
target, the sample:

#. loads a document that conforms to ``device.xsd`` with ``mxmlLoadString()``
   and validates the tree with ``zxsd_mxml_validate()``;
#. does the same with a document that lacks the required ``sample`` element
   and prints the structured error returned by the validator.

For validation of XML text with the allocation-free built-in backend, see the
``samples/zephyr/xsd-validation`` sample.

Requirements
************

``CONFIG_MXML`` is defined by a Zephyr module for Mini-XML 4, which must be
present in the west workspace. The upstream ``v4.0.6`` release pinned in this
repository's ``west.yml`` does not include one. When ``CONFIG_MXML`` is enabled,
the zxsd module builds its Mini-XML backend, which provides
``zxsd_mxml_validate()``, in place of the built-in backend. Validation of XML
text, for example with ``device_validate()``, behaves the same with either
backend.

Mini-XML allocates its tree from the libc heap, so this sample is not
allocation-free; the zxsd validation itself does not allocate. ``prj.conf``
reserves a 4 KiB arena with ``CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE``; size it
for the largest document parsed at once.

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

From an initialized Zephyr environment that contains a Mini-XML module:

.. code-block:: console

   $ west build -b qemu_x86 samples/zephyr/xsd-validation-mxml
   $ west build -t run

Expected output:

.. code-block:: console

   valid document: valid
   invalid document: invalid, incorrect element order (device) at offset 6
   zxsd Mini-XML validation sample done

The adapter walks a parsed tree, so its error codes and offsets can differ from
those reported by the built-in backend for the same document.
