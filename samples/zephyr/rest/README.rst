.. _mxsd_rest_sample:

mxsd REST XML client
####################

Overview
********

This sample uses XML as the representation of a REST resource. It calls the
public `httpbin <https://httpbin.org/>`_ test service with Zephyr's HTTP client:

#. ``GET /base64/<resource>`` receives a deterministic XML slideshow resource;
#. the generated reader validates the body against ``slideshow.xsd`` before
   deserializing it into ``slideshow_slideshow_t``;
#. the application changes the author in the typed C structure;
#. the generated writer serializes the structure and validates the resulting
   XML before it can leave the device;
#. ``POST /anything`` sends that XML with ``Content-Type: application/xml``.

httpbin returns a JSON diagnostic envelope for ``POST /anything``. It is used
only to confirm the HTTP exchange; the sample does not mistake that envelope
for the XML resource. The incoming and outgoing XML documents are both
validated locally from immutable tables generated at build time.

The complete sample runs with ``CONFIG_HEAP_MEM_POOL_SIZE=0``. It uses
Zephyr's asynchronous DNS resolver, backed by fixed query and ``net_buf``
pools, instead of ``getaddrinfo()``, which allocates its result list. Socket,
HTTP receive, XML document, binding arena, and DNS result storage are all
bounded at build time or owned directly by the application. The
``BUILD_ASSERT`` in the application prevents an unnoticed system-heap
requirement from entering this example.

This public endpoint is convenient for a demo, not a production dependency.
For a real service, change ``REST_HOST``, ``REST_PORT`` and the resource paths,
and replace ``slideshow.xsd`` with the service's schema. The sample deliberately
uses plain HTTP to keep the transport setup visible; production applications
must enable Zephyr TLS and provision a trusted CA certificate.

Building and running
********************

The board needs IPv4, DNS and Internet connectivity. From an initialized
Zephyr workspace:

.. code-block:: console

   $ west build -b qemu_x86 samples/zephyr/rest
   $ west build -t run

When using QEMU, configure the Zephyr networking environment described by the
Zephyr networking samples. On a network-capable board, adapt its overlay and
network interface configuration as usual.

For a local Linux or Docker run, ``native_sim`` can use the host socket stack
directly, without a TAP interface. The overlay configures a DNS server because
this mode does not obtain one through Zephyr DHCP:

.. code-block:: console

   $ west build -b native_sim/native/64 samples/zephyr/rest -- \
       -DEXTRA_CONF_FILE=overlay-nsos.conf
   $ build/zephyr/zephyr.exe

Expected application output is:

.. code-block:: console

   GET XML valid: "Zephyr REST demo", 1 slide(s)
   POST XML valid: 237 bytes
   POST accepted: HTTP 200
   mxsd REST sample done

The ``/base64`` endpoint simply decodes the resource embedded in the URL. This
keeps the public test deterministic without requiring a private mock-server
account.
