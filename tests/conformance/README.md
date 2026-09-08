# mxsd XSD 1.0 conformance suite

This Apache-2.0 suite maps XSD 1.0 feature groups to host tests containing
both valid and invalid instances. `conformance.py` executes every supported
group and writes a deterministic JSON support report. A construct is listed as
supported only when its positive and negative cases pass.

Unsupported and expected-failure entries are deliberately retained in the
manifest so acceptance by the compiler cannot be mistaken for conformance.
