# Fuzzing

The C target exercises the portable XML parser and descriptor-driven validator
under libFuzzer, AddressSanitizer, and UndefinedBehaviorSanitizer:

```sh
CC=clang cmake -S . -B build-fuzz \
  -DMXSD_USE_MXML=OFF -DMXSD_BUILD_TESTS=OFF -DMXSD_BUILD_FUZZERS=ON
cmake --build build-fuzz --parallel
build-fuzz/bin/mxsd_xml_fuzzer fuzz/corpus/xml -max_len=4096
```

The Python target instruments `mxsdc` with Atheris and exercises schema parsing
plus descriptor generation. Atheris 2.3.0 requires Python 3.11 or older:

```sh
python3.11 -m pip install atheris==2.3.0
python3.11 fuzz/fuzz_mxsdc.py fuzz/corpus/xsd -max_len=262144
```

CI performs bounded smoke runs. Longer local or scheduled runs can reuse the
same seed corpora and preserve newly discovered inputs as regression tests.
