.. _mxsd_bindings_sample:

mxsd typed bindings
###################

Overview
********

This sample handles device settings exchanged as XML without touching XML text
in the application. ``settings.xsd`` is compiled on the host with
the ``mxsd_generate(... BINDINGS)`` CMake helper, which runs ``mxsdc`` on the
build host and generates a validator and C structures. On the target, the
sample:

#. validates a received document and reads it into ``settings_settings_t``,
   with strings and arrays stored in a static arena;
#. updates the settings in C: changes the mode and adds a sensor;
#. writes the settings back into a buffer, then validates the output against
   ``settings.xsd`` with the generated validator;
#. shows that an invalid document is rejected before any structure is filled;
#. shows that the writer refuses a structure with a missing required value.

No heap is used. The stack peak is reached during validation and depends on
``CONFIG_MXSD_MAX_TEXT_LENGTH``: ``prj.conf`` lowers it to 128 bytes, which
gives a measured peak of about 15.7 KiB on ``qemu_x86``. Measure your own
documents with ``CONFIG_INIT_STACKS`` and ``k_thread_stack_space_get()``, and
keep ``CONFIG_HW_STACK_PROTECTION`` enabled while sizing the stack.

Building and running
********************

From an initialized Zephyr environment:

.. code-block:: console

   $ west build -b qemu_x86 samples/zephyr/bindings
   $ west build -t run

Expected output (the written document is shown on one line):

.. code-block:: console

   read: version 3, mode performance, 2 sensor(s)
     sensor 1 "Boiler temperature": every 500 ms, enabled, threshold -20
     sensor 2 "Room & hallway humidity": every 2000 ms, disabled
   arena: 91 of 512 bytes used
   write: ok, 519 bytes
   <?xml version="1.0" encoding="UTF-8"?>
   <ns1:settings xmlns:ns1="urn:mxsd:sample:settings" version="4">...</ns1:settings>
   validate: ok
   rejected: incorrect element order (settings)
   refused: missing required element (label)
   mxsd bindings sample done

The arena usage depends on pointer size and alignment of the target.
