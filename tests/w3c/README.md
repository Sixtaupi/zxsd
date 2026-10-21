# W3C XML Schema 1.0 conformance

This harness inventories every case in the W3C XML Schema 1.0 Test Suite
release `xsts-2007-06-20`. It then tests schema acceptance and, for schemas
that mxsd supports, compiles a C validator and executes every instance case.
Results distinguish `passed`, `failed`, `unsupported`, `expected-failure`, and
`skipped`; an unsupported feature is never reported as a pass. A reviewed JSON
array of exact case identifiers can be supplied with `--expected-failures`.

The suite is downloaded separately because it contains roughly 40,000 files
and has its own W3C test-suite license. The normal build never uses the network.
See <https://www.w3.org/copyright/test-suites-licenses/> for the applicable
W3C test-suite licensing terms.

```sh
python3 tests/w3c/fetch.py build/w3c-suite
python3 tests/w3c/run.py \
  --suite build/w3c-suite/xmlschema2006-11-06 \
  --output build/w3c-xsd10-report.json \
  --html build/w3c-xsd10-report.html
```

The JSON report is exhaustive and large. `--html` also writes a standalone page
with per-category totals, a per-test-set table, skip reasons, and a filterable
list of failed, expected-failure, and unsupported cases with their diagnostics.
An existing JSON report can be rendered without rerunning the suite:

```sh
python3 tests/w3c/report.py build/w3c-xsd10-report.json -o build/w3c-xsd10-report.html
```

Use `--inventory-only` to verify discovery without compiling cases, `--limit N`
for a smoke run, and `--status all` to include cases whose W3C status is not
`accepted`. By default, every published case is inventoried while only accepted
cases are executed.

The runtime is compiled once per run, schema sets are processed in parallel
(`--jobs N`, defaulting to the CPU count), and generated validators are linked
in batches into shared runner executables. Batching matters on macOS, where the
first launch of every new executable is scanned by the system.

Two reviewed rules keep the categories honest:

* An instance case whose `xsi:schemaLocation` hint names a local file that is
  not a well-formed schema document is marked as requiring instance-driven
  schema discovery. mxsd never loads schemas from instance hints, so a
  mismatch on such a case is `unsupported` (for example `addB063.i`).
* `DISPUTED_CASES` in `run.py` lists accepted cases whose published expectation
  encodes one processor's implementation limit instead of an XSD 1.0 rule
  (`particlesZ036_b1.i` and `particlesZ036_b2.i`). Each entry records the
  result the Recommendation requires and the reason. Such a case is still
  executed and becomes `skipped` with that reason only when mxsd produces the
  recorded result; any other outcome remains `failed`.

The W3C describes this suite as comprehensive rather than complete. Therefore
the inventory can reach 100% of published cases, but it cannot prove that every
possible behavior of the XSD 1.0 Recommendations has been exercised.
