W3C XSD 1.0 baseline
####################

The complete accepted-case run on 21 October 2026 produced the following
baseline against ``xsts-2007-06-20``:

.. list-table::
   :header-rows: 1

   * - Classification
     - Cases
   * - Passed
     - 38,457
   * - Failed
     - 7
   * - Unsupported
     - 214
   * - Skipped by the accepted-status filter
     - 740
   * - Skipped as disputed suite expectations
     - 2
   * - Total inventory
     - 39,420

This is not a claim of complete XSD 1.0 conformance. Of the 38,680 accepted
cases, 38,678 were classified from an executed result; 221 of them either
expose unsupported behavior or fail. The project support matrix remains the
product contract.

Failure clusters
****************

The seven remaining failures comprise:

* five invalid schemas accepted by incomplete particle restriction checks
  (``elemZ026``, ``particlesHa161``, ``particlesZ023``, ``particlesZ024``, and
  ``particlesZ028``);
* one invalid particle instance accepted because its restricted type was
  accepted (``particlesZ001.i``);
* one validator timeout on the large ``CJKUnifiedIdeographs.v`` regex case. Its
  generated validator returns the expected result but needs roughly 16 seconds
  of unoptimized CPU time, which exceeds the harness's ten-second limit.

Invalid instances that depend on runtime ``xsi:schemaLocation`` discovery,
including hints that name a local file that is not a well-formed schema, are
classified as unsupported: mxsd never loads schemas from instance hints.

``particlesZ036_b1.i`` and ``particlesZ036_b2.i`` are skipped as disputed suite
expectations. Their documentation describes a Microsoft engine behavior for
``maxOccurs`` values above 4096. XSD 1.0 defines no such limit, and both
instances are valid against ``particlesZ036_b.xsd``. The harness records the
required result and reason, and reports a failure if mxsd's result changes.

The compiler now applies the order-preserving XSD 1.0 *Recurse* rule to
``xs:all`` restrictions of ``xs:all`` content, including ``xs:redefine`` group
restrictions. It also rejects constraining facets on ``simpleContent`` whose base
is ``xs:anySimpleType``.

The 18 compiler exceptions in an earlier baseline are fixed and now produce
normal schema diagnostics. The harness also discovers locally referenced schema
documents whose filename does not end in ``.xsd``. A compiler exception remains
classified as ``failed`` and is never reported as an unsupported feature.

Unsupported clusters
********************

The dominant unsupported group remains the Microsoft regular-expression suite
(158 cases). The most frequent diagnostics concern character escapes,
operators, repetition syntax, and character-range endpoints. The public support
contract now describes regex support as a bounded subset instead of claiming
the complete grammar. Other visible clusters are direct ``xs:NOTATION`` use
(22 cases), runtime schema discovery from instance hints (13 cases),
complex-content restriction checks (11 cases), and a small number of
schema-composition checks.

The weakest accepted test sets in this baseline are Microsoft ``MS-Schema``
(94.5%), ``MS-SimpleType`` (95.0%), ``MS-Regex`` (95.7%), and
``MS-Additional`` (97.7%). Percentages exclude cases skipped by the W3C
publication-status filter.

Reproducing the analysis
************************

Run the complete suite and generate the compact analysis with:

.. code-block:: console

   $ python3 tests/w3c/fetch.py build/w3c-suite
   $ python3 tests/w3c/run.py \
       --suite build/w3c-suite/xmlschema2006-11-06 \
       --work-dir build/w3c \
       --output build/w3c-xsd10-report.json \
       --html build/w3c-xsd10-report.html
   $ python3 tests/w3c/summarize.py build/w3c-xsd10-report.json \
       --json build/w3c-xsd10-summary.json \
       --markdown build/w3c-xsd10-summary.md

The scheduled and manually dispatched conformance workflow performs the same
complete run and publishes all four files. Pull requests use a 500-case smoke
selection and must not be compared numerically with this full baseline.
