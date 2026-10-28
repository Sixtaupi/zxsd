.. _mxsd_bindings_stream_sample:

mxsd streaming typed bindings
#############################

Overview
********

This sample handles the same device settings as the ``bindings`` sample, but
uses only the fragment-based APIs: no complete XML document is ever held in
memory. ``settings.xsd`` is compiled on the host with
``mxsd_generate(... BINDINGS STREAM_BIND settings/sensor)``, which generates a
validator, the C structures, and a streaming reader that delivers each
``sensor`` element. On the target, the sample:

#. feeds the received document to the streaming reader in 16-byte fragments,
   as they would arrive from a network or a file. Each sensor is delivered to
   a callback as a ``settings_Sensor_t`` and copied into application storage;
   the copies are used only after ``*_reader_finish()`` has validated the whole
   document;
#. builds the settings to send in C, adding a sensor;
#. writes them with ``settings_settings_write()``, which passes the output to a
   sink callback in fragments and validates each item with a streaming
   validator. The sink forwards every fragment to a receiving
   ``mxsd_xml_reader_t``, which validates the raw bytes as they arrive;
#. shows that an invalid document is rejected while it is being fed, before
   any sensor is delivered;
#. shows that the writer refuses a structure with a missing required value,
   after part of the document has already reached the sink.

The documents are in ``src/settings_documents.h``.

Fragment-based APIs
*******************

.. list-table::
   :header-rows: 1

   * - Operation
     - API used here
     - Memory
   * - Read
     - ``settings_settings_sensor_reader_init/feed/finish/dispose()``
     - One sensor in a 64-byte arena, restored after each delivery
   * - Write
     - ``settings_settings_write()`` with a sink and a validator
     - No output buffer; the sink receives each fragment
   * - Validate
     - ``mxsd_xml_reader_init/feed/finish/dispose()``
     - One incomplete token, up to ``CONFIG_MXSD_XML_TOKEN_CAPACITY`` bytes

Points to keep in mind:

* Fragments can be split anywhere, including inside a tag, an attribute, a
  UTF-8 sequence, or an entity. The input buffer can be reused as soon as
  ``*_feed()`` returns.
* The first error is sticky: later ``*_feed()`` and ``*_finish()`` calls
  return it again. Stop feeding once an error is reported.
* A delivered sensor is provisional. Later input can still make the document
  invalid, so only use the copies after a successful ``*_reader_finish()``.
* This selection delivers ``sensor`` elements only. The root ``version``
  attribute and ``mode`` element are validated but not materialized; select
  another path with an additional ``STREAM_BIND`` entry to receive another
  part of the document with its own reader.
* Bytes already passed to the sink cannot be recalled when writing fails. The
  receiver must only use a document for which both the writer and its own
  ``mxsd_xml_reader_finish()`` succeeded.

No heap is used. Readers and validators embed fixed-capacity parsing state and
are declared ``static``.

Resource limits
***************

``prj.conf`` sets the runtime limits in two groups, as recommended in the
Kconfig reference:

* schema-derived limits (attributes, depth, particles, names, regex and
  content-model capacities), copied after review from the fragment printed by
  ``python3 mxsdc settings.xsd -o generated --bindings --stream-bind
  settings/sensor --zephyr-config``;
* application budgets that the XSD cannot bound: ``CONFIG_MXSD_MAX_INPUT``,
  which also limits the total size of a fragmented document,
  ``CONFIG_MXSD_XML_TOKEN_CAPACITY`` and ``CONFIG_MXSD_MAX_TEXT_LENGTH``.

``CMakeLists.txt`` passes ``ZEPHYR_CONFIG`` to ``mxsd_generate()``, so the
generated validator checks the schema-derived values at compile time: the
build fails if one of them drops below what ``settings.xsd`` requires. When the
schema changes, regenerate the fragment and update ``prj.conf``.

Building and running
********************

From an initialized Zephyr environment:

.. code-block:: console

   $ west build -b qemu_x86 samples/zephyr/bindings-stream
   $ west build -t run

Expected output:

.. code-block:: console

   read: 2 sensor(s) from 24 fragments of 16 bytes
     sensor 1 "Boiler temperature": every 500 ms
     sensor 2 "Room & hallway humidity": every 2000 ms
   write: ok, mode eco, 519 bytes in 166 fragments
   peer: ok (-)
   rejected after 5 fragment(s), 0 sensor(s) delivered: incorrect element order (settings)
   refused after 422 bytes: missing required element (label)
   mxsd streaming bindings sample done

The number of fragments passed to the sink depends on the writer and may
change between releases.
