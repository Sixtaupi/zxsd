Project overview
################

mxsd is an Apache-2.0 XML Schema validator designed for embedded C projects.
It moves schema parsing and most complexity to the development machine. The
generated descriptor tables are immutable; a generic C runtime interprets
them on the target.

Architecture
************

.. code-block:: text

   development machine                         target application
   +------------+    mxsdc      +---------+    +-----------------------+
   | schema.xsd | ------------> | .mxsd.c | -> | generated descriptors |
   +------------+               | .mxsd.h |    | + libmxsd runtime     |
                                +---------+    +-----------+-----------+
                                                          |
                                      XML string ---------+ validation
                                      parser events ------+

This split has useful consequences:

* XSD documents, imports, and includes are processed only by ``mxsdc``;
* generated schemas contain no mutable global state;
* multiple generated schemas can share one runtime library;
* applications choose whole-document validation or the fixed-memory event API;
* resource ceilings are visible as compile-time configuration values.

Repository map
**************

``mxsdc``
   Python host compiler. It resolves local schema dependencies and emits C99.

``include/``
   Stable public C headers.

``src/``
   Portable validation runtime: XML reading, content models, values, identity
   constraints, and streaming validation.

``samples/``
   Complete host and Zephyr applications.

``tests/conformance/``
   Project-owned positive and negative XSD feature suite.

``tests/w3c/``
   Runner for the separately downloaded W3C XML Schema 1.0 Test Suite.

Design boundaries
*****************

mxsd targets XSD 1.0, not XSD 1.1. It is strict by design: a construct outside
the documented subset is rejected by the compiler rather than silently
ignored. The runtime does not fetch schemas or access the filesystem or
network. Schema composition is resolved locally by the host compiler.

The library is useful when deterministic deployment and bounded resources
matter more than accepting every possible XSD document. Consult
:doc:`xsd-support` before selecting it for an existing schema corpus.

