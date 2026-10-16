import pathlib, subprocess, sys, tempfile

compiler, schema, expected, build = map(pathlib.Path, sys.argv[1:])
with tempfile.TemporaryDirectory(dir=build) as a, tempfile.TemporaryDirectory(dir=build) as b:
    for out in (a, b):
        subprocess.run([sys.executable, str(compiler), str(schema), "-o", out], check=True)
    for name in ("device.mxsd.c", "device.mxsd.h"):
        assert (pathlib.Path(a) / name).read_bytes() == (pathlib.Path(b) / name).read_bytes(), name
        assert (pathlib.Path(a) / name).read_text(encoding="utf-8") == (expected / name).read_text(
            encoding="utf-8"
        ), ("golden " + name)
with tempfile.TemporaryDirectory(dir=build) as d:
    bad = pathlib.Path(d) / "bad.xsd"
    bad.write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema"><xs:keyref name="x"/></xs:schema>'
    )
    p = subprocess.run(
        [sys.executable, str(compiler), str(bad), "-o", str(pathlib.Path(d) / "out")],
        capture_output=True,
        text=True,
    )
    assert p.returncode != 0
    assert "unsupported XSD construct 'xs:keyref'" in p.stderr

with tempfile.TemporaryDirectory(dir=build) as d:
    bad = pathlib.Path(d) / "bad-annotation.xsd"
    bad.write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema"><xs:annotation><xs:element name="not-metadata"/></xs:annotation><xs:element name="root" type="xs:string"/></xs:schema>'
    )
    p = subprocess.run(
        [sys.executable, str(compiler), str(bad), "-o", str(pathlib.Path(d) / "out")],
        capture_output=True,
        text=True,
    )
    assert p.returncode != 0
    assert "annotation accepts only" in p.stderr

with tempfile.TemporaryDirectory(dir=build) as d:
    directory = pathlib.Path(d)
    first = directory / "first.xsd"
    second = directory / "second.xsd"
    first.write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema"><xs:include schemaLocation="second.xsd"/><xs:element name="root" type="xs:string"/></xs:schema>'
    )
    second.write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema"><xs:include schemaLocation="first.xsd"/></xs:schema>'
    )
    p = subprocess.run(
        [sys.executable, str(compiler), str(first), "-o", str(directory / "out")],
        capture_output=True,
        text=True,
    )
    assert p.returncode != 0
    assert "circular schema dependency" in p.stderr

with tempfile.TemporaryDirectory(dir=build) as d:
    directory = pathlib.Path(d)
    network = directory / "network.xsd"
    network.write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema"><xs:include schemaLocation="https://example.invalid/schema.xsd"/><xs:element name="root" type="xs:string"/></xs:schema>'
    )
    p = subprocess.run(
        [sys.executable, str(compiler), str(network), "-o", str(directory / "out")],
        capture_output=True,
        text=True,
    )
    assert p.returncode != 0
    assert "network schema locations are not resolved implicitly" in p.stderr

with tempfile.TemporaryDirectory(dir=build) as d:
    directory = pathlib.Path(d)
    registry = directory / "registry"
    registry.mkdir()
    (registry / "urn_test.xsd").write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema" targetNamespace="urn:test"><xs:simpleType name="Code"><xs:restriction base="xs:string"/></xs:simpleType></xs:schema>'
    )
    root = directory / "registry-root.xsd"
    root.write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:t="urn:test"><xs:import namespace="urn:test"/><xs:element name="root" type="t:Code"/></xs:schema>'
    )
    subprocess.run(
        [
            sys.executable,
            str(compiler),
            str(root),
            "-I",
            str(registry),
            "-o",
            str(directory / "out"),
        ],
        check=True,
    )

with tempfile.TemporaryDirectory(dir=build) as d:
    directory = pathlib.Path(d)
    final_schema = directory / "final.xsd"
    final_schema.write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema"><xs:complexType name="Base" final="extension"><xs:sequence><xs:element name="x" type="xs:string"/></xs:sequence></xs:complexType><xs:complexType name="Derived"><xs:complexContent><xs:extension base="Base"><xs:sequence><xs:element name="y" type="xs:string"/></xs:sequence></xs:extension></xs:complexContent></xs:complexType><xs:element name="root" type="Derived"/></xs:schema>'
    )
    p = subprocess.run(
        [sys.executable, str(compiler), str(final_schema), "-o", str(directory / "out")],
        capture_output=True,
        text=True,
    )
    assert p.returncode != 0
    assert "final blocks extension" in p.stderr

with tempfile.TemporaryDirectory(dir=build) as d:
    p = subprocess.run(
        [
            sys.executable,
            str(compiler),
            str(schema),
            "--max-schema-complexity",
            "1",
            "-o",
            str(pathlib.Path(d) / "out"),
        ],
        capture_output=True,
        text=True,
    )
    assert p.returncode != 0
    assert "schema complexity exceeds configured limit" in p.stderr
