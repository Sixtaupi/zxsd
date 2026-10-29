Interactive weather client
##########################

This sample asks for a city on the Zephyr console, then performs two HTTPS
requests:

* Nominatim resolves the city to latitude and longitude;
* MET Norway's Locationforecast ``classic`` endpoint returns the forecast based on the coordinates.

Both XML responses are validated through generated zxsd schema descriptors
before the sample displays values. Typed callbacks are provisional until the
complete response passes validation.

It demonstrates basic typed bindings for Nominatim and single-pass streaming
typed bindings for MET Norway, without keeping the roughly 140 KB forecast XML.

Nominatim (city to coordinates)
*******************************

The small Nominatim service uses the basic typed
binding API and validation.
It uses ``nominatim.xsd`` to generate binding code and validation.

MET Norway (coordinates to forecast)
*******************************

``zxsd_generate(... STREAM_BIND weatherdata/product/time)`` generates a typed
reader for one forecast at a time. The HTTP callback feeds body fragments to
``met_weather_weatherdata_product_time_reader_feed()``. A callback receives a
``met_weather_timeType_t`` with typed locations and measurements; application
code never searches XML tags or dispatches attribute names.

A fixed 4096-byte arena is reused for each selected forecast. Once a location
contains all displayed measurements, its values are copied into a fixed
summary and later forecasts are validated without materialization. The entire
response must still pass ``*_reader_finish()`` before that summary is displayed.
An oversized selected forecast reports an arena limit error; display strings
also have explicit capacities and are never silently truncated. The arena
bounds one forecast, not the complete document or number of forecasts.

The sample keeps Zephyr's system heap disabled, while TLS uses mbedTLS's
separately configured fixed 64 KiB arena.

The requests identify the application through a ``User-Agent`` as required by
both public services. Forks should replace its project URL. Nominatim data is
copyright OpenStreetMap contributors and available under the ODbL.

Building
********

From an initialized Zephyr workspace, build for QEMU with network access::

   west build -b qemu_x86 samples/zephyr/weather

For example, the NXP FRDM-MCXN947 build uses the board's hardware entropy::

   west build -p always -b frdm_mcxn947/mcxn947/cpu0 samples/zephyr/weather

The Twister entry is build-only because it depends on interactive input and
live third-party services::

   west twister -T samples/zephyr/weather -p qemu_x86 --inline-logs

Offline validation and the exact typed summary callback are exercised under
``tests/`` (not by running the interactive sample)::

   west twister -T tests/zephyr -p qemu_x86 --inline-logs

For ``native_sim``, apply ``overlay-nsos.conf`` to use native socket offload.
Runtime TLS support depends on the selected board and network stack.

Usage
*****

At the prompt, type a city and press Enter::

   City (Press Enter to validate): Lyon

Coordinates sent to MET Norway are limited to four decimal places as
recommended by its API documentation. Altitude is fixed to 90 metres for this
example.

