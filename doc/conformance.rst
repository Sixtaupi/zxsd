Conformance and test coverage
#############################

mxsd uses two complementary suites. They answer different questions and their
results must not be conflated.

Project conformance suite
*************************

``tests/conformance`` maps documented features to project-owned positive and
negative tests. A feature is marked supported only when both acceptance and
rejection behavior pass. Its JSON report separates supported, unsupported,
expected-failure, passed, and failed entries. Compiler acceptance alone is
never a pass.

Run the normal build and tests to generate ``conformance-report.json``:

.. code-block:: console

   $ cmake -S . -B build -DMXSD_USE_MXML=OFF
   $ cmake --build build
   $ ctest --test-dir build --output-on-failure

W3C XSD 1.0 suite
*****************

The optional harness under ``tests/w3c`` inventories all **39,420** published
cases in the W3C XML Schema 1.0 Test Suite release ``xsts-2007-06-20``. For
schemas mxsd accepts, it generates a C validator and executes associated
instances. Results remain classified as ``passed``, ``failed``,
``unsupported``, ``expected-failure``, or ``skipped``.

The W3C suite is downloaded separately because it is large and carries its own
test-suite licence. Normal builds never access the network.

.. code-block:: console

   $ python3 tests/w3c/fetch.py build/w3c-suite
   $ python3 tests/w3c/run.py \
       --suite build/w3c-suite/xmlschema2006-11-06 \
       --output build/w3c-xsd10-report.json

Use ``--inventory-only`` to check discovery, ``--limit 500`` for a smoke run,
and ``--status all`` to include cases whose W3C publication status is not
``accepted``.

What “coverage” means here
**************************

The harness can inventory 100% of the published W3C cases, but this does not
mean mxsd passes them all, nor that every possible XSD behavior is represented.
The W3C describes its suite as comprehensive rather than complete. Therefore:

* the support matrix is the product contract;
* the internal suite gates that contract;
* the W3C report measures interoperability and exposes gaps;
* unsupported cases are never counted as successful conformance.

CI runs a W3C smoke selection on pull requests and can run all accepted cases
on scheduled or manual jobs. Internal conformance failures fail CI; W3C
discrepancies are archived separately while the documented subset remains
smaller than complete XSD 1.0.

