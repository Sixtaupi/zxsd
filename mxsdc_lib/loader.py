# Copyright (c) 2026 mxsd contributors
# SPDX-License-Identifier: Apache-2.0

"""Load and resolve a bounded graph of local XML Schema documents."""

import io
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

from .common import (
    InvalidSchemaError,
    MAX_XSD,
    SchemaError,
    XML_URI,
    XS,
    XSD_URI,
    check,
    lname,
    valid_ncname,
)


def parse_xml_with_nsmaps(data):
    """Parse XML and retain the in-scope namespace map for every element."""
    pending = []
    stack = []
    mappings = {}
    root = None
    try:
        iterator = ET.iterparse(io.BytesIO(data), events=("start-ns", "start", "end"))
        for event, item in iterator:
            if event == "start-ns":
                pending.append((item[0] or "", item[1]))
            elif event == "start":
                current = dict(stack[-1]) if stack else {"xml": XML_URI}
                current.update(pending)
                pending.clear()
                mappings[id(item)] = current
                stack.append(current)
                if root is None:
                    root = item
            else:
                stack.pop()
    except ET.ParseError as e:
        raise SchemaError("malformed XSD: %s" % e)
    return root, mappings


def remove_annotations(root):
    """Validate and discard XSD annotations before semantic compilation."""

    def validate(annotation):
        check(annotation, {"id"})
        for child in annotation:
            kind = lname(child.tag)
            if kind == "documentation":
                check(child, {"source", "{" + XML_URI + "}lang"})
                language = child.get("{" + XML_URI + "}lang")
                if language is not None and not re.fullmatch(
                    r"[A-Za-z]{1,8}(?:-[A-Za-z0-9]{1,8})*", language
                ):
                    raise InvalidSchemaError("invalid xml:lang on xs:documentation")
            elif kind == "appinfo":
                check(child, {"source"})
            else:
                raise SchemaError("xs:annotation accepts only xs:documentation and xs:appinfo")
            # documentation/appinfo have intentionally unconstrained mixed
            # content, including foreign and XSD-namespaced markup.

    def visit(parent):
        children = list(parent)
        annotations = [
            index for index, child in enumerate(children) if child.tag == XS + "annotation"
        ]
        # The schema document itself permits annotations between top-level
        # declarations. Every other XSD component has at most one annotation,
        # and it is the first child in that component's content model.
        if parent.tag != XS + "schema" and annotations:
            if len(annotations) != 1 or annotations[0] != 0:
                raise InvalidSchemaError(
                    "xs:%s permits at most one leading annotation" % lname(parent.tag)
                )
        for child in children:
            if child.tag == XS + "annotation":
                validate(child)
                parent.remove(child)
            else:
                visit(child)

    visit(root)


def default_resolver(namespace, location, base_path, search_paths):
    """Resolve one schema reference without performing network access."""
    if location and re.match(r"^[A-Za-z][A-Za-z0-9+.-]*://", location):
        raise SchemaError("network schema locations are not resolved implicitly")
    candidates = []
    if location:
        candidates.append(base_path.parent / location)
        candidates.extend(directory / location for directory in search_paths)
    else:
        safe = re.sub(r"[^A-Za-z0-9_.-]", "_", namespace or "")
        candidates.extend(directory / (safe + ".xsd") for directory in search_paths)
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise SchemaError("cannot resolve schema %r" % (location or namespace))


def load_documents(
    path,
    resolver=None,
    search_paths=(),
    additional_paths=(),
    allow_unresolved_locations=False,
):
    """Load an explicitly connected include/import/redefine graph."""
    resolver = resolver or (
        lambda namespace, location, base: default_resolver(namespace, location, base, search_paths)
    )
    documents = []
    loaded = {}
    loading = []
    in_progress = {}
    xml_namespace_document = None
    explicit_xml_namespace = False

    def builtin_xml_namespace():
        """Return the standard declarations from the reserved XML namespace."""
        nonlocal xml_namespace_document
        if xml_namespace_document is None:
            root = ET.fromstring(
                (
                    '<xs:schema xmlns:xs="%s" targetNamespace="%s">'
                    '<xs:attribute name="lang" type="xs:language"/>'
                    '<xs:attribute name="space"><xs:simpleType><xs:restriction base="xs:NCName">'
                    '<xs:enumeration value="default"/><xs:enumeration value="preserve"/>'
                    "</xs:restriction></xs:simpleType></xs:attribute>"
                    '<xs:attribute name="base" type="xs:anyURI"/>'
                    '<xs:attribute name="id" type="xs:ID"/>'
                    "</xs:schema>"
                )
                % (XSD_URI, XML_URI)
            )
            xml_namespace_document = {
                "path": pathlib.Path("<builtin-xml-namespace>"),
                "root": root,
                "nsmap": {"xs": XSD_URI},
                "target": XML_URI,
                "efd": "unqualified",
                "afd": "unqualified",
                "block_default": "",
                "final_default": "",
                "redefinitions": [],
                "dependencies": [],
            }
            documents.append(xml_namespace_document)
        return xml_namespace_document

    def visit(current, expected_namespace=None, relation=None):
        nonlocal explicit_xml_namespace
        current = pathlib.Path(current).resolve()
        for (loading_path, _), existing in in_progress.items():
            if loading_path != current:
                continue
            if expected_namespace is None or existing["target"] == expected_namespace:
                return existing
            chain = " -> ".join(str(x[0]) for x in loading + [(current, expected_namespace)])
            raise SchemaError("circular schema dependency: " + chain)
        for (loaded_path, _), existing in loaded.items():
            if (
                loaded_path == current
                and existing["root"].get("targetNamespace") is not None
                and (expected_namespace is None or existing["target"] == expected_namespace)
            ):
                return existing
        load_key = (current, expected_namespace)
        if load_key in loading:
            if load_key in in_progress:
                return in_progress[load_key]
            chain = " -> ".join(str(x[0]) for x in loading + [load_key])
            raise SchemaError("circular schema dependency: " + chain)
        if load_key in loaded:
            return loaded[load_key]
        data = current.read_bytes()
        if len(data) > MAX_XSD:
            raise SchemaError("XSD input too large")
        if b"<!DOCTYPE" in data.upper() or b"<!ENTITY" in data.upper():
            raise SchemaError("DTD and entities are not supported")
        root, element_nsmaps = parse_xml_with_nsmaps(data)
        if root.tag != XS + "schema":
            raise SchemaError("root must be xs:schema")
        ids = set()
        for element in root.iter():
            identifier = element.get("id")
            if identifier is None:
                continue
            if not valid_ncname(identifier):
                raise SchemaError("invalid xs:ID value %r" % identifier)
            if identifier in ids:
                raise SchemaError("duplicate xs:ID value %r" % identifier)
            ids.add(identifier)
        remove_annotations(root)
        declared = root.get("targetNamespace")
        if declared == "":
            raise InvalidSchemaError("targetNamespace must not be empty")
        if relation in ("include", "redefine") and declared not in (
            None,
            expected_namespace,
        ):
            raise SchemaError("included schema has a different targetNamespace")
        if (
            relation == "import"
            and expected_namespace is not None
            and declared != expected_namespace
        ):
            raise SchemaError("import namespace does not match imported schema")
        effective = declared if declared is not None else expected_namespace
        if effective == XML_URI:
            explicit_xml_namespace = True
        document = {
            "path": current,
            "root": root,
            "nsmap": element_nsmaps[id(root)],
            "element_nsmaps": element_nsmaps,
            "target": effective,
            "chameleon": declared is None and effective is not None,
            "efd": root.get("elementFormDefault", "unqualified"),
            "afd": root.get("attributeFormDefault", "unqualified"),
            "block_default": root.get("blockDefault", ""),
            "final_default": root.get("finalDefault", ""),
            "redefinitions": [],
            "dependencies": [],
            "ids": ids,
        }
        in_progress[load_key] = document
        loading.append(load_key)
        for child in root:
            kind = lname(child.tag)
            if kind not in ("include", "import", "redefine"):
                continue
            check(
                child,
                (
                    {"id", "namespace", "schemaLocation"}
                    if kind == "import"
                    else {"id", "schemaLocation"}
                ),
            )
            location = child.get("schemaLocation")
            if kind != "import" and not location:
                raise SchemaError("xs:%s requires schemaLocation" % kind)
            namespace = child.get("namespace") if kind == "import" else effective
            if kind == "import" and namespace == "":
                raise InvalidSchemaError("xs:import namespace must not be empty")
            if kind == "import" and namespace is None and effective is None:
                raise SchemaError(
                    "xs:import without namespace requires an enclosing targetNamespace"
                )
            if kind == "import" and namespace == effective:
                raise InvalidSchemaError(
                    "xs:import namespace must differ from the enclosing targetNamespace"
                )
            if kind == "import" and namespace == XML_URI and not location:
                if explicit_xml_namespace:
                    continue
                dependency = builtin_xml_namespace()
            elif kind == "import" and not location:
                # An import location is only a hint. Components from such a
                # namespace may still be supplied explicitly as another root.
                try:
                    resolved = resolver(namespace, location, current)
                except SchemaError:
                    continue
                dependency = visit(resolved, namespace, kind)
            else:
                may_ignore_retrieval_failure = allow_unresolved_locations and (
                    kind != "redefine" or all(lname(item.tag) == "annotation" for item in child)
                )
                if location and re.match(r"^[A-Za-z][A-Za-z0-9+.-]*://", location):
                    if may_ignore_retrieval_failure:
                        print(
                            "warning: schema location %r was not resolved" % location,
                            file=sys.stderr,
                        )
                        continue
                    raise SchemaError("network schema locations are not resolved implicitly")
                try:
                    resolved = resolver(namespace, location, current)
                except SchemaError:
                    # A schemaLocation is a URI reference and retrieval may fail.
                    # Missing components still fail later when actually referenced.
                    if kind != "import" and not may_ignore_retrieval_failure:
                        raise
                    if may_ignore_retrieval_failure:
                        print(
                            "warning: schema location %r was not resolved" % location,
                            file=sys.stderr,
                        )
                    continue
                dependency = visit(resolved, namespace, kind)
            document["dependencies"].append((kind, dependency))
            if kind == "redefine":
                document["redefinitions"].append((child, dependency))
        loading.pop()
        in_progress.pop(load_key, None)
        loaded[load_key] = document
        documents.append(document)
        return document

    main = visit(path)
    for additional in additional_paths:
        visit(additional)
    return main, documents
