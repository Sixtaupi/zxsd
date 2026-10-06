mxsd
####

.. container:: mxsd-hero

   **Compile XML Schema on the host. Validate XML on constrained C targets.**

   mxsd turns an XSD 1.0 schema into constant C descriptors and a small,
   portable validator. The target never parses a schema and can validate a
   complete XML document or consume parser events with bounded storage.

Why mxsd?
*********

* **Embedded first:** C99, configurable limits, no operating-system dependency,
  and a zero-heap mode.
* **Predictable artifacts:** generated C is compiled with the application;
  unsupported schema constructs fail during generation.
* **Portable:** the same runtime builds on Linux, macOS, Windows, and Zephyr.
* **Testable claims:** supported features have positive and negative tests;
  W3C results distinguish failures from unsupported cases.

.. grid:: 2

   .. grid-item-card:: Start here

      Build mxsd, compile a schema, and validate a first document.

      :doc:`getting-started`

   .. grid-item-card:: XSD support

      See the exact supported, partial, and rejected XSD 1.0 features.

      :doc:`xsd-support`

   .. grid-item-card:: Conformance

      Understand the internal gates and the official W3C test-suite harness.

      :doc:`conformance`

   .. grid-item-card:: C API

      Browse the public types, limits, functions, and Mini-XML adapter.

      :doc:`api`

.. toctree::
   :maxdepth: 2
   :caption: Guide
   :hidden:

   overview
   getting-started
   xsd-support
   conformance
   zephyr
   kconfig
   resource-usage

.. toctree::
   :maxdepth: 2
   :caption: Reference
   :hidden:

   api
   building-docs

Indices
*******

* :ref:`genindex`
* :ref:`search`

