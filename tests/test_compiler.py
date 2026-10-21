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
    assert p.returncode == 0, p.stderr
    assert (directory / "out" / "first.mxsd.c").is_file()

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
    p = subprocess.run(
        [
            sys.executable,
            str(compiler),
            str(network),
            "-o",
            str(directory / "warning-out"),
            "--allow-unresolved-schema-locations",
        ],
        capture_output=True,
        text=True,
    )
    assert p.returncode == 0, p.stderr
    assert "warning: schema location" in p.stderr

with tempfile.TemporaryDirectory(dir=build) as d:
    directory = pathlib.Path(d)
    first = directory / "multi-first.xsd"
    second = directory / "multi-second.xsd"
    first.write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema" targetNamespace="urn:first">'
        '<xs:element name="first" type="xs:string"/></xs:schema>'
    )
    second.write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema" targetNamespace="urn:second">'
        '<xs:element name="second" type="xs:int"/></xs:schema>'
    )
    output = directory / "multi-out"
    subprocess.run(
        [sys.executable, str(compiler), str(first), str(second), "-o", str(output)],
        check=True,
    )

    for filename, declaration, diagnostic in (
        (
            "duplicate-wildcard.xsd",
            '<xs:complexType name="Bad"><xs:anyAttribute/><xs:anyAttribute/></xs:complexType>',
            "only one xs:anyAttribute",
        ),
        (
            "mixed-extension.xsd",
            '<xs:complexType name="Base"><xs:sequence><xs:element name="x"/>'
            '</xs:sequence></xs:complexType><xs:complexType name="Bad" mixed="true">'
            '<xs:complexContent><xs:extension base="Base"/></xs:complexContent>'
            "</xs:complexType>",
            "cannot add mixed character content",
        ),
    ):
        invalid = directory / filename
        invalid.write_text(
            '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">' + declaration + "</xs:schema>"
        )
        result = subprocess.run(
            [sys.executable, str(compiler), str(invalid), "-o", str(directory / "bad-out")],
            capture_output=True,
            text=True,
        )
        assert result.returncode != 0
        assert diagnostic in result.stderr
    generated = (output / "multi_first.mxsd.c").read_text(encoding="utf-8")
    assert '"first"' in generated and '"second"' in generated

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
    supported = directory / "schema-edge-cases.xsd"
    supported.write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema" '
        'xmlns:xml="http://www.w3.org/XML/1998/namespace" '
        'finalDefault="extension restriction list union">'
        '<xs:import namespace="http://www.w3.org/XML/1998/namespace"/>'
        '<xs:simpleType name="Digits"><xs:restriction base="xs:decimal">'
        '<xs:totalDigits value="4"/><xs:fractionDigits value="2"/>'
        "</xs:restriction></xs:simpleType>"
        '<xs:complexType name="Empty"><xs:sequence/></xs:complexType>'
        '<xs:element name="root"><xs:complexType><xs:choice minOccurs="0"/>'
        '<xs:attribute ref="xml:lang"/><xs:anyAttribute namespace="relative" '
        'processContents="skip"/></xs:complexType></xs:element></xs:schema>'
    )
    subprocess.run(
        [sys.executable, str(compiler), str(supported), "-o", str(directory / "out")],
        check=True,
    )
    declarations_only = directory / "declarations-only.xsd"
    declarations_only.write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">'
        '<xs:simpleType name="Code"><xs:restriction base="xs:token"/>'
        "</xs:simpleType></xs:schema>"
    )
    subprocess.run(
        [
            sys.executable,
            str(compiler),
            str(declarations_only),
            "-o",
            str(directory / "declarations-out"),
        ],
        check=True,
    )
    declarations_source = (directory / "declarations-out" / "declarations_only.mxsd.c").read_text(
        encoding="utf-8"
    )
    assert "schema_unbound" in declarations_source
    assert "mxsd_validate(NULL" not in declarations_source

    repeated_derivation = directory / "repeated-derivation.xsd"
    repeated_derivation.write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">'
        '<xs:complexType name="Base" block="restriction restriction"/>'
        '<xs:complexType name="Empty"><xs:complexContent>'
        '<xs:restriction base="xs:anyType"><xs:attribute name="id" type="xs:int"/>'
        "</xs:restriction></xs:complexContent></xs:complexType>"
        "</xs:schema>"
    )
    subprocess.run(
        [
            sys.executable,
            str(compiler),
            str(repeated_derivation),
            "-o",
            str(directory / "repeated-out"),
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

for construct, declaration, reference, diagnostic in (
    (
        "group",
        '<xs:group name="loop"><xs:sequence><xs:group ref="loop"/></xs:sequence></xs:group>',
        '<xs:sequence><xs:group ref="loop"/></xs:sequence>',
        "circular model group reference",
    ),
    (
        "attributeGroup",
        '<xs:attributeGroup name="loop"><xs:attributeGroup ref="loop"/></xs:attributeGroup>',
        '<xs:attributeGroup ref="loop"/>',
        "circular attributeGroup reference",
    ),
):
    with tempfile.TemporaryDirectory(dir=build) as d:
        bad = pathlib.Path(d) / (construct + "-cycle.xsd")
        bad.write_text(
            '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">'
            + declaration
            + '<xs:element name="root"><xs:complexType>'
            + reference
            + "</xs:complexType></xs:element></xs:schema>"
        )
        p = subprocess.run(
            [sys.executable, str(compiler), str(bad), "-o", str(pathlib.Path(d) / "out")],
            capture_output=True,
            text=True,
        )
        assert p.returncode != 0
        assert diagnostic in p.stderr

for filename, declaration, diagnostic in (
    (
        "missing-attribute-group.xsd",
        '<xs:element name="root"><xs:complexType><xs:attributeGroup ref="missing"/>'
        "</xs:complexType></xs:element>",
        "invalid attributeGroup ref 'missing'",
    ),
    (
        "circular-substitution-group.xsd",
        '<xs:element name="first" substitutionGroup="second"/>'
        '<xs:element name="second" substitutionGroup="first"/>',
        "circular substitutionGroup declarations",
    ),
):
    with tempfile.TemporaryDirectory(dir=build) as d:
        bad = pathlib.Path(d) / filename
        bad.write_text(
            '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">' + declaration + "</xs:schema>"
        )
        p = subprocess.run(
            [sys.executable, str(compiler), str(bad), "-o", str(pathlib.Path(d) / "out")],
            capture_output=True,
            text=True,
        )
        assert p.returncode != 0
        assert diagnostic in p.stderr
        assert "Traceback" not in p.stderr

for name, redefinition, diagnostic in (
    (
        "unknown-group",
        '<xs:group name="missing"><xs:sequence><xs:element name="x"/></xs:sequence></xs:group>',
        "redefines unknown component",
    ),
    (
        "repeated-self-group",
        '<xs:group name="items"><xs:sequence><xs:group ref="items"/>'
        '<xs:group ref="items"/></xs:sequence></xs:group>',
        "may reference itself at most once",
    ),
    (
        "repeated-self-attribute-group",
        '<xs:attributeGroup name="properties"><xs:attributeGroup ref="properties"/>'
        '<xs:attributeGroup ref="properties"/></xs:attributeGroup>',
        "may reference itself at most once",
    ),
    (
        "reordered-all-group",
        '<xs:group name="fields"><xs:all><xs:element name="b"/><xs:element name="a"/>'
        "</xs:all></xs:group>",
        "redefinition widens its original content model",
    ),
):
    with tempfile.TemporaryDirectory(dir=build) as d:
        directory = pathlib.Path(d)
        base = directory / "base.xsd"
        base.write_text(
            '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">'
            '<xs:group name="items"><xs:sequence><xs:element name="item"/></xs:sequence>'
            "</xs:group>"
            '<xs:group name="fields"><xs:all><xs:element name="a"/><xs:element name="b"/>'
            "</xs:all></xs:group>"
            '<xs:attributeGroup name="properties"><xs:attribute name="code"/>'
            "</xs:attributeGroup></xs:schema>"
        )
        schema_path = directory / (name + ".xsd")
        schema_path.write_text(
            '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">'
            '<xs:redefine schemaLocation="base.xsd">'
            + redefinition
            + '</xs:redefine><xs:element name="root"/></xs:schema>'
        )
        p = subprocess.run(
            [sys.executable, str(compiler), str(schema_path), "-o", str(directory / "out")],
            capture_output=True,
            text=True,
        )
        assert p.returncode != 0
        assert diagnostic in p.stderr

with tempfile.TemporaryDirectory(dir=build) as d:
    bad = pathlib.Path(d) / "bad-identity-union.xsd"
    bad.write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">'
        '<xs:element name="root"><xs:complexType><xs:sequence>'
        '<xs:element name="item" minOccurs="0" maxOccurs="unbounded"/>'
        '</xs:sequence></xs:complexType><xs:unique name="bad">'
        '<xs:selector xpath="item | "/><xs:field xpath="."/>'
        "</xs:unique></xs:element></xs:schema>"
    )
    p = subprocess.run(
        [sys.executable, str(compiler), str(bad), "-o", str(pathlib.Path(d) / "out")],
        capture_output=True,
        text=True,
    )
    assert p.returncode != 0
    assert "invalid identity XPath union" in p.stderr

with tempfile.TemporaryDirectory(dir=build) as d:
    bad = pathlib.Path(d) / "bad-mixed.xsd"
    bad.write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">'
        '<xs:element name="root"><xs:complexType mixed="sometimes"/>'
        "</xs:element></xs:schema>"
    )
    p = subprocess.run(
        [sys.executable, str(compiler), str(bad), "-o", str(pathlib.Path(d) / "out")],
        capture_output=True,
        text=True,
    )
    assert p.returncode != 0
    assert "invalid mixed value" in p.stderr

with tempfile.TemporaryDirectory(dir=build) as d:
    bad = pathlib.Path(d) / "type-derivation-cycle.xsd"
    bad.write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">'
        '<xs:complexType name="A"><xs:complexContent><xs:extension base="B"/>'
        "</xs:complexContent></xs:complexType>"
        '<xs:complexType name="B"><xs:complexContent><xs:extension base="A"/>'
        "</xs:complexContent></xs:complexType>"
        '<xs:element name="root" type="A"/></xs:schema>'
    )
    p = subprocess.run(
        [sys.executable, str(compiler), str(bad), "-o", str(pathlib.Path(d) / "out")],
        capture_output=True,
        text=True,
    )
    assert p.returncode != 0
    assert "circular type derivation" in p.stderr

for name, declarations, diagnostic in (
    (
        "list-of-list",
        '<xs:simpleType name="Items"><xs:list itemType="xs:string"/></xs:simpleType>'
        '<xs:simpleType name="Bad"><xs:list itemType="Items"/></xs:simpleType>',
        "cannot itself be a list",
    ),
    (
        "wider-max-length",
        '<xs:simpleType name="Base"><xs:restriction base="xs:string">'
        '<xs:maxLength value="5"/></xs:restriction></xs:simpleType>'
        '<xs:simpleType name="Bad"><xs:restriction base="Base">'
        '<xs:maxLength value="6"/></xs:restriction></xs:simpleType>',
        "maxLength widens",
    ),
    (
        "wider-total-digits",
        '<xs:simpleType name="Base"><xs:restriction base="xs:decimal">'
        '<xs:totalDigits value="4"/></xs:restriction></xs:simpleType>'
        '<xs:simpleType name="Bad"><xs:restriction base="Base">'
        '<xs:totalDigits value="5"/></xs:restriction></xs:simpleType>',
        "totalDigits widens",
    ),
    (
        "any-simple-type-content-facet",
        '<xs:complexType name="Base"><xs:simpleContent>'
        '<xs:extension base="xs:anySimpleType"/></xs:simpleContent></xs:complexType>'
        '<xs:complexType name="Bad"><xs:simpleContent><xs:restriction base="Base">'
        '<xs:minLength value="1"/></xs:restriction></xs:simpleContent></xs:complexType>',
        "facets cannot restrict xs:anySimpleType content",
    ),
    (
        "reordered-all-restriction",
        '<xs:complexType name="Base"><xs:all><xs:element name="a"/>'
        '<xs:element name="b"/></xs:all></xs:complexType>'
        '<xs:complexType name="Bad"><xs:complexContent><xs:restriction base="Base">'
        '<xs:all><xs:element name="b"/><xs:element name="a"/></xs:all>'
        "</xs:restriction></xs:complexContent></xs:complexType>",
        "widens the base content model",
    ),
):
    with tempfile.TemporaryDirectory(dir=build) as d:
        bad = pathlib.Path(d) / (name + ".xsd")
        bad.write_text(
            '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">'
            + declarations
            + "</xs:schema>"
        )
        p = subprocess.run(
            [sys.executable, str(compiler), str(bad), "-o", str(pathlib.Path(d) / "out")],
            capture_output=True,
            text=True,
        )
        assert p.returncode != 0
        assert diagnostic in p.stderr

for name, declarations in (
    (
        "any-simple-type-content-without-facets",
        '<xs:complexType name="Base"><xs:simpleContent>'
        '<xs:extension base="xs:anySimpleType"/></xs:simpleContent></xs:complexType>'
        '<xs:complexType name="Good"><xs:simpleContent><xs:restriction base="Base">'
        "</xs:restriction></xs:simpleContent></xs:complexType>",
    ),
    (
        "ordered-all-restriction",
        '<xs:complexType name="Base"><xs:all><xs:element name="a"/>'
        '<xs:element name="b" minOccurs="0"/></xs:all></xs:complexType>'
        '<xs:complexType name="Good"><xs:complexContent><xs:restriction base="Base">'
        '<xs:all><xs:element name="a"/></xs:all>'
        "</xs:restriction></xs:complexContent></xs:complexType>",
    ),
    (
        "unordered-sequence-from-all",
        '<xs:complexType name="Base"><xs:all><xs:element name="a"/>'
        '<xs:element name="b"/></xs:all></xs:complexType>'
        '<xs:complexType name="Good"><xs:complexContent><xs:restriction base="Base">'
        '<xs:sequence><xs:element name="b"/><xs:element name="a"/></xs:sequence>'
        "</xs:restriction></xs:complexContent></xs:complexType>",
    ),
):
    with tempfile.TemporaryDirectory(dir=build) as d:
        good = pathlib.Path(d) / (name + ".xsd")
        good.write_text(
            '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">'
            + declarations
            + "</xs:schema>"
        )
        p = subprocess.run(
            [sys.executable, str(compiler), str(good), "-o", str(pathlib.Path(d) / "out")],
            capture_output=True,
            text=True,
        )
        assert p.returncode == 0, (name, p.stderr)

for name, pattern in (
    ("unclosed-group", "(abc"),
    ("reversed-range", "[z-a]"),
    ("unknown-property", r"\p{IsNotAUnicodeBlock}"),
    ("bad-repetition", "a{4,2}"),
):
    with tempfile.TemporaryDirectory(dir=build) as d:
        bad = pathlib.Path(d) / (name + ".xsd")
        bad.write_text(
            '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">'
            '<xs:simpleType name="Bad"><xs:restriction base="xs:string">'
            f'<xs:pattern value="{pattern}"/></xs:restriction></xs:simpleType>'
            '<xs:element name="root" type="Bad"/></xs:schema>'
        )
        p = subprocess.run(
            [sys.executable, str(compiler), str(bad), "-o", str(pathlib.Path(d) / "out")],
            capture_output=True,
            text=True,
        )
        assert p.returncode != 0
        assert "invalid xs:pattern" in p.stderr

for length, succeeds in ((4096, True), (4097, False)):
    with tempfile.TemporaryDirectory(dir=build) as d:
        boundary = pathlib.Path(d) / ("pattern-length-%d.xsd" % length)
        boundary.write_text(
            '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">'
            '<xs:simpleType name="Boundary"><xs:restriction base="xs:string">'
            f'<xs:pattern value="{"a" * length}"/></xs:restriction></xs:simpleType>'
            '<xs:element name="root" type="Boundary"/></xs:schema>'
        )
        p = subprocess.run(
            [sys.executable, str(compiler), str(boundary), "-o", str(pathlib.Path(d) / "out")],
            capture_output=True,
            text=True,
        )
        assert (p.returncode == 0) == succeeds, (length, p.stderr)
        if not succeeds:
            assert "pattern is too long" in p.stderr

for depth, succeeds in ((32, True), (33, False)):
    with tempfile.TemporaryDirectory(dir=build) as d:
        boundary = pathlib.Path(d) / ("pattern-depth-%d.xsd" % depth)
        pattern = "(" * depth + "a" + ")" * depth
        boundary.write_text(
            '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">'
            '<xs:simpleType name="Boundary"><xs:restriction base="xs:string">'
            f'<xs:pattern value="{pattern}"/></xs:restriction></xs:simpleType>'
            '<xs:element name="root" type="Boundary"/></xs:schema>'
        )
        p = subprocess.run(
            [sys.executable, str(compiler), str(boundary), "-o", str(pathlib.Path(d) / "out")],
            capture_output=True,
            text=True,
        )
        assert (p.returncode == 0) == succeeds, (depth, p.stderr)
        if not succeeds:
            assert "MXSD_MAX_REGEX_DEPTH" in p.stderr

for name, declarations, simple_type, diagnostic in (
    (
        "notation-no-identifier",
        '<xs:notation name="jpeg"/>',
        '<xs:simpleType name="Format"><xs:restriction base="xs:NOTATION">'
        '<xs:enumeration value="jpeg"/></xs:restriction></xs:simpleType>',
        "requires public or system",
    ),
    (
        "notation-duplicate",
        '<xs:notation name="jpeg" public="image/jpeg"/>'
        '<xs:notation name="jpeg" public="image/jpeg-alt"/>',
        '<xs:simpleType name="Format"><xs:restriction base="xs:NOTATION">'
        '<xs:enumeration value="jpeg"/></xs:restriction></xs:simpleType>',
        "duplicate notation",
    ),
    (
        "notation-undeclared-enumeration",
        '<xs:notation name="jpeg" public="image/jpeg"/>',
        '<xs:simpleType name="Format"><xs:restriction base="xs:NOTATION">'
        '<xs:enumeration value="png"/></xs:restriction></xs:simpleType>',
        "undeclared notation",
    ),
    (
        "notation-missing-enumeration",
        '<xs:notation name="jpeg" public="image/jpeg"/>',
        '<xs:simpleType name="Format"><xs:restriction base="xs:NOTATION"/>' "</xs:simpleType>",
        "require at least one enumeration",
    ),
    (
        "notation-direct-use",
        '<xs:notation name="jpeg" public="image/jpeg"/>',
        "",
        "cannot be used directly",
    ),
    (
        "notation-invalid-facet",
        '<xs:notation name="jpeg" public="image/jpeg"/>',
        '<xs:simpleType name="Format"><xs:restriction base="xs:NOTATION">'
        '<xs:enumeration value="jpeg"/><xs:minInclusive value="jpeg"/>'
        "</xs:restriction></xs:simpleType>",
        "not a valid facet",
    ),
    (
        "notation-invalid-qname",
        '<xs:notation name="jpeg" public="image/jpeg"/>',
        '<xs:simpleType name="Format"><xs:restriction base="xs:NOTATION">'
        '<xs:enumeration value=":jpeg"/></xs:restriction></xs:simpleType>',
        "invalid NOTATION enumeration QName",
    ),
    (
        "notation-widened-enumeration",
        '<xs:notation name="jpeg" public="image/jpeg"/>'
        '<xs:notation name="png" public="image/png"/>',
        '<xs:simpleType name="Base"><xs:restriction base="xs:NOTATION">'
        '<xs:enumeration value="jpeg"/></xs:restriction></xs:simpleType>'
        '<xs:simpleType name="Format"><xs:restriction base="Base">'
        '<xs:enumeration value="png"/></xs:restriction></xs:simpleType>',
        "widens its base type",
    ),
):
    with tempfile.TemporaryDirectory(dir=build) as d:
        bad = pathlib.Path(d) / (name + ".xsd")
        element_type = "xs:NOTATION" if name == "notation-direct-use" else "Format"
        bad.write_text(
            '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">'
            + declarations
            + simple_type
            + f'<xs:element name="root" type="{element_type}"/></xs:schema>'
        )
        p = subprocess.run(
            [sys.executable, str(compiler), str(bad), "-o", str(pathlib.Path(d) / "out")],
            capture_output=True,
            text=True,
        )
        assert p.returncode != 0
        assert diagnostic in p.stderr

for depth, succeeds in ((32, True), (33, False)):
    with tempfile.TemporaryDirectory(dir=build) as d:
        boundary = pathlib.Path(d) / ("class-subtraction-depth-%d.xsd" % depth)
        pattern = "[a]"
        for _ in range(depth):
            pattern = "[a-" + pattern + "]"
        boundary.write_text(
            '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">'
            '<xs:simpleType name="Boundary"><xs:restriction base="xs:string">'
            f'<xs:pattern value="{pattern}"/></xs:restriction></xs:simpleType>'
            '<xs:element name="root" type="Boundary"/></xs:schema>'
        )
        p = subprocess.run(
            [sys.executable, str(compiler), str(boundary), "-o", str(pathlib.Path(d) / "out")],
            capture_output=True,
            text=True,
        )
        assert (p.returncode == 0) == succeeds, (depth, p.stderr)
        if not succeeds:
            assert "MXSD_MAX_REGEX_DEPTH" in p.stderr

# Typed bindings: deterministic, golden, opt-in, and explicit about unsupported constructs.
with tempfile.TemporaryDirectory(dir=build) as a, tempfile.TemporaryDirectory(dir=build) as b:
    for out in (a, b):
        subprocess.run(
            [sys.executable, str(compiler), str(schema), "-o", out, "--bindings"], check=True
        )
    for name in ("device.mxsd.c", "device.mxsd.h", "device.bind.c", "device.bind.h"):
        assert (pathlib.Path(a) / name).read_bytes() == (pathlib.Path(b) / name).read_bytes(), name
        assert (pathlib.Path(a) / name).read_text(encoding="utf-8") == (expected / name).read_text(
            encoding="utf-8"
        ), ("golden " + name)

with tempfile.TemporaryDirectory(dir=build) as d:
    subprocess.run([sys.executable, str(compiler), str(schema), "-o", d], check=True)
    assert not list(pathlib.Path(d).glob("*.bind.*")), "bindings must be opt-in"


def compile_bindings(directory, body, *options):
    """Compile an inline schema with --bindings and return the completed process."""
    source = pathlib.Path(directory) / "bind.xsd"
    source.write_text(
        '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema" '
        'xmlns:t="urn:t" targetNamespace="urn:t">' + body + "</xs:schema>"
    )
    return subprocess.run(
        [
            sys.executable,
            str(compiler),
            str(source),
            "-o",
            str(pathlib.Path(directory) / "out"),
            "--bindings",
            *options,
        ],
        capture_output=True,
        text=True,
    )


ITEM = '<xs:element name="item" type="xs:string"/>'
for body, diagnostic in (
    (
        '<xs:element name="r"><xs:complexType><xs:sequence><xs:any/></xs:sequence>'
        "</xs:complexType></xs:element>",
        "element wildcard",
    ),
    (
        '<xs:element name="r"><xs:complexType><xs:anyAttribute/></xs:complexType></xs:element>',
        "attribute wildcard",
    ),
    (
        '<xs:element name="r"><xs:complexType mixed="true"><xs:sequence>'
        + ITEM
        + "</xs:sequence></xs:complexType></xs:element>",
        "mixed content",
    ),
    (
        '<xs:element name="r"><xs:complexType><xs:sequence maxOccurs="2">'
        + ITEM
        + "</xs:sequence></xs:complexType></xs:element>",
        "repeating xs:sequence group",
    ),
    (
        '<xs:element name="r"><xs:complexType><xs:sequence>'
        + ITEM
        + '<xs:element name="other" type="xs:int"/>'
        + ITEM
        + "</xs:sequence></xs:complexType></xs:element>",
        "occurs at several places",
    ),
    (
        '<xs:element name="r"><xs:complexType><xs:sequence>'
        '<xs:element name="item" type="xs:string" nillable="true"/>'
        "</xs:sequence></xs:complexType></xs:element>",
        "is nillable",
    ),
    (
        '<xs:element name="head" type="xs:string"/>'
        '<xs:element name="member" type="xs:string" substitutionGroup="t:head"/>'
        '<xs:element name="r"><xs:complexType><xs:sequence><xs:element ref="t:head"/>'
        "</xs:sequence></xs:complexType></xs:element>",
        "head of a substitution group",
    ),
    (
        '<xs:element name="r"><xs:complexType><xs:sequence>'
        '<xs:element name="q" type="xs:QName"/></xs:sequence></xs:complexType></xs:element>',
        "QName or NOTATION",
    ),
    (
        '<xs:complexType name="A" abstract="true"><xs:sequence>'
        + ITEM
        + '</xs:sequence></xs:complexType><xs:element name="r" type="t:A"/>',
        "is abstract",
    ),
    (
        '<xs:element name="r"><xs:complexType><xs:sequence><xs:element name="x"/>'
        "</xs:sequence></xs:complexType></xs:element>",
        "xs:anyType",
    ),
    ('<xs:element name="r" type="xs:string"/>', "no global element with a complex type"),
):
    with tempfile.TemporaryDirectory(dir=build) as d:
        p = compile_bindings(d, body)
        assert p.returncode != 0, body
        assert "error: bindings: " in p.stderr and diagnostic in p.stderr, (diagnostic, p.stderr)
        # A failed binding generation leaves no partial output behind.
        out = pathlib.Path(d) / "out"
        assert not out.exists() or not list(out.iterdir()), body

# Root selection, and the validator for the remaining roots is still generated.
TWO_ROOTS = (
    '<xs:element name="a"><xs:complexType><xs:sequence>' + ITEM + "</xs:sequence>"
    '</xs:complexType></xs:element><xs:element name="b"><xs:complexType><xs:sequence>'
    "<xs:any/></xs:sequence></xs:complexType></xs:element>"
    '<xs:element name="s" type="xs:string"/>'
)
with tempfile.TemporaryDirectory(dir=build) as d:
    assert compile_bindings(d, TWO_ROOTS).returncode != 0
    p = compile_bindings(d, TWO_ROOTS, "--bind-root", "a")
    assert p.returncode == 0, p.stderr
    header = (pathlib.Path(d) / "out" / "bind.bind.h").read_text(encoding="utf-8")
    assert "bind_a_read(" in header and "bind_b_read(" not in header
    p = compile_bindings(d, TWO_ROOTS, "--bind-root", "{urn:t}a")
    assert p.returncode == 0, p.stderr
    for root, diagnostic in (("missing", "unknown root element"), ("s", "has a simple type")):
        p = compile_bindings(d, TWO_ROOTS, "--bind-root", root)
        assert p.returncode != 0 and diagnostic in p.stderr, p.stderr
with tempfile.TemporaryDirectory(dir=build) as d:
    p = subprocess.run(
        [sys.executable, str(compiler), str(schema), "-o", d, "--bind-root", "device"],
        capture_output=True,
        text=True,
    )
    assert p.returncode != 0 and "--bind-root requires --bindings" in p.stderr

# Generated names avoid C/C++ keywords and collisions between members and helpers.
with tempfile.TemporaryDirectory(dir=build) as d:
    p = compile_bindings(
        d,
        '<xs:simpleType name="E"><xs:restriction base="xs:token"><xs:enumeration value="a-b"/>'
        '<xs:enumeration value="a_b"/><xs:enumeration value=""/><xs:enumeration value=" a-b "/>'
        "</xs:restriction></xs:simpleType>"
        '<xs:element name="r"><xs:complexType><xs:sequence>'
        '<xs:element name="class" type="xs:int" minOccurs="0"/>'
        '<xs:element name="has_class" type="xs:int"/>'
        '<xs:element name="e" type="t:E" maxOccurs="2"/>'
        '</xs:sequence><xs:attribute name="e" type="xs:int"/>'
        '<xs:attribute name="_X" type="xs:int"/></xs:complexType></xs:element>',
    )
    assert p.returncode == 0, p.stderr
    header = (pathlib.Path(d) / "out" / "bind.bind.h").read_text(encoding="utf-8")
    for declaration in (
        "int32_t class_;",
        "bool has_class_;",
        "int32_t has_class;",
        "int32_t e;",
        "const bind_E_t *e_elem;",
        "size_t e_elem_count;",
        "int32_t x_X;",
        "BIND_E_A_B = 0,",
        "BIND_E_A_B_2 = 1,",
        "BIND_E_EMPTY = 2,",
    ):
        assert declaration in header, (declaration, header)
    # Whitespace-equivalent enumeration values collapse to a single constant.
    assert "BIND_E_A_B_3" not in header
