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
  --output build/w3c-xsd10-report.json
```

Use `--inventory-only` to verify discovery without compiling cases, `--limit N`
for a smoke run, and `--status all` to include cases whose W3C status is not
`accepted`. By default, every published case is inventoried while only accepted
cases are executed.

The W3C describes this suite as comprehensive rather than complete. Therefore
the inventory can reach 100% of published cases, but it cannot prove that every
possible behavior of the XSD 1.0 Recommendations has been exercised.
