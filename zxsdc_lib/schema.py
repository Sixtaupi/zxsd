# Copyright (c) 2026 zxsd contributors
# SPDX-License-Identifier: Apache-2.0

"""Compile a resolved XML Schema document graph into the generator model."""

import itertools
import pathlib
import re
import xml.etree.ElementTree as ET
from decimal import Decimal, InvalidOperation

from .common import (
    InvalidSchemaError,
    LIST_BUILTINS,
    MAX_SCHEMA_COMPLEXITY,
    MAX_XSD,
    PRIMITIVES,
    SchemaError,
    WHITESPACE_PRESERVE,
    WHITESPACE_REPLACE,
    XML_URI,
    XS,
    XSD_URI,
    XSI_URI,
    check,
    compare_ordered_values,
    ident,
    lname,
    normalize_token,
    primitive_lexical_form,
    valid_ncname,
    valid_qname,
)
from .loader import load_documents
from .patterns import validate_pattern


def occurs(n):
    """Parse and validate minOccurs/maxOccurs on a particle."""

    def get(k, d):
        x = n.get(k, d)
        if k == "maxOccurs" and x == "unbounded":
            return None
        if not re.fullmatch(r"[0-9]+", x):
            raise SchemaError("invalid %s %r" % (k, x))
        v = int(x)
        # Generated descriptors reserve ULONG_MAX for "unbounded" and must
        # remain portable to Windows, where unsigned long is 32 bits. Values
        # above this ceiling are observationally equivalent for an input
        # already bounded by ZXSD_MAX_INPUT.
        return min(v, 4294967294)

    lo, hi = get("minOccurs", "1"), get("maxOccurs", "1")
    if hi is not None and lo > hi:
        raise SchemaError("minOccurs exceeds maxOccurs")
    return lo, hi


def parse(
    path,
    resolver=None,
    search_paths=(),
    max_schema_complexity=MAX_SCHEMA_COMPLEXITY,
    additional_paths=(),
    allow_unresolved_locations=False,
):
    """Compile a connected XSD document graph into the generator model.

    The nested helpers deliberately share symbol tables and namespace context.
    Keeping that state local makes a compilation independent from every other
    invocation and avoids hidden module-level mutable state.
    """
    main, documents = load_documents(
        path,
        resolver,
        tuple(pathlib.Path(x) for x in search_paths),
        tuple(pathlib.Path(x) for x in additional_paths),
        allow_unresolved_locations,
    )
    complexity = sum(1 for document in documents for _ in document["root"].iter())
    if max_schema_complexity < 1 or complexity > max_schema_complexity:
        raise SchemaError("schema complexity exceeds configured limit")
    root = main["root"]
    data = main["path"].read_bytes()
    if len(data) > MAX_XSD:
        raise SchemaError("XSD input too large")
    if b"<!DOCTYPE" in data.upper() or b"<!ENTITY" in data.upper():
        raise SchemaError("DTD and entities are not supported")
    nsmap = main["nsmap"]
    check(
        root,
        {
            "targetNamespace",
            "elementFormDefault",
            "attributeFormDefault",
            "blockDefault",
            "finalDefault",
            "version",
            "id",
        },
    )
    target = main["target"]
    schema_for_schemas = target == XSD_URI
    efd = main["efd"]
    afd = main["afd"]
    if efd not in ("qualified", "unqualified") or afd not in (
        "qualified",
        "unqualified",
    ):
        raise SchemaError("invalid form default")
    type_nodes = {}
    global_nodes = {}
    global_attribute_nodes = {}
    group_nodes = {}
    attribute_group_nodes = {}
    notation_nodes = {}
    notation_order = []
    order = []
    attribute_order = []
    contexts = {}

    def schema_key(uri, name):
        return (uri or "") + "|" + name

    redefinition_nodes = {}
    redefined_bases = {}
    redefined_self_references = {}
    for document in documents:
        for redefine, dependency in document["redefinitions"]:
            declared = set()
            for child in redefine:
                kind = lname(child.tag)
                if kind not in ("simpleType", "complexType", "group", "attributeGroup"):
                    raise SchemaError("xs:redefine cannot contain xs:%s" % kind)
                identity = (kind, normalize_token(child.get("name", "")))
                if identity in declared:
                    raise InvalidSchemaError(
                        "xs:redefine cannot contain duplicate xs:%s declarations" % kind
                    )
                declared.add(identity)
                redefinition_nodes[id(child)] = (document, dependency)

    def dependency_declares(dependency, kind, name, seen=None):
        """Return whether a redefine target exists in the referenced schema graph."""
        seen = set() if seen is None else seen
        marker = id(dependency)
        if marker in seen:
            return False
        seen.add(marker)
        for child in dependency["root"]:
            child_kind = lname(child.tag)
            if child_kind == kind and normalize_token(child.get("name", "")) == name:
                return True
            if child_kind == "redefine":
                if any(
                    lname(candidate.tag) == kind
                    and normalize_token(candidate.get("name", "")) == name
                    for candidate in child
                ):
                    return True
        return any(
            relation in ("include", "redefine") and dependency_declares(candidate, kind, name, seen)
            for relation, candidate in dependency.get("dependencies", [])
        )

    def dependency_contains_node(dependency, node, seen=None):
        """Return whether a declaration belongs to the referenced schema graph."""
        seen = set() if seen is None else seen
        marker = id(dependency)
        if marker in seen:
            return False
        seen.add(marker)
        if any(candidate is node for candidate in dependency["root"].iter()):
            return True
        return any(
            relation in ("include", "redefine") and dependency_contains_node(candidate, node, seen)
            for relation, candidate in dependency.get("dependencies", [])
        )

    def referenced_key(value, document, node=None):
        """Resolve a component QName while collecting redefine declarations."""
        if not value:
            return None
        mapping = document.get("element_nsmaps", {}).get(id(node), document["nsmap"])
        if ":" in value:
            prefix, local = value.split(":", 1)
            if prefix not in mapping:
                raise SchemaError("undeclared QName prefix %r" % prefix)
            uri = mapping[prefix]
        else:
            local = value
            uri = mapping.get("")
            if uri is None and document.get("chameleon"):
                uri = document["target"]
        return schema_key(uri, local)

    def alias_redefined_component(nodes, key, name, node, document, kind):
        """Preserve the old component and retarget legal self references to it."""
        _, dependency = redefinition_nodes[id(node)]
        if not dependency_declares(dependency, kind, name):
            raise InvalidSchemaError("xs:%s redefines unknown component %r" % (kind, name))
        if key not in nodes:
            raise SchemaError("xs:%s redefines unknown component %r" % (kind, name))
        if not dependency_contains_node(dependency, nodes[key]):
            raise InvalidSchemaError(
                "xs:%s redefinition conflicts with another schema document" % kind
            )
        alias = name + "__redefined_base"
        alias_key = schema_key(document["target"], alias)
        while alias_key in nodes:
            alias += "_"
            alias_key = schema_key(document["target"], alias)
        nodes[alias_key] = nodes[key]
        redefined_bases[id(node)] = alias_key
        type_redefinition = kind in ("simpleType", "complexType")
        references = [
            descendant
            for descendant in node.iter()
            if descendant is not node
            and (
                type_redefinition
                and lname(descendant.tag) in ("restriction", "extension")
                and referenced_key(descendant.get("base"), document, descendant) == key
                or not type_redefinition
                and lname(descendant.tag) == kind
                and referenced_key(descendant.get("ref"), document, descendant) == key
            )
        ]
        redefined_self_references[id(node)] = bool(references)
        if type_redefinition and len(references) != 1:
            raise InvalidSchemaError(
                "xs:%s redefinition must derive exactly once from its original definition" % kind
            )
        if len(references) > 1:
            raise SchemaError("xs:%s redefinition may reference itself at most once" % kind)
        if references and kind == "group":
            lo, hi = occurs(references[0])
            if lo != 1 or hi != 1:
                raise SchemaError("self reference in xs:group redefinition must occur exactly once")
        for reference in references:
            attribute = "base" if type_redefinition else "ref"
            original_ref = reference.get(attribute, "")
            prefix = original_ref.rsplit(":", 1)[0] if ":" in original_ref else None
            reference.set(attribute, (prefix + ":" if prefix else "") + alias)

    def derivation_methods(value, allowed, filter_default=False):
        if not value:
            return 0
        tokens = value.split()
        if tokens == ["#all"]:
            return "ZXSD_DERIVATION_ALL"
        tokens = list(dict.fromkeys(tokens))
        known = {"extension", "restriction", "substitution", "list", "union"}
        if not tokens or any(
            token not in (known if filter_default else allowed) for token in tokens
        ):
            raise SchemaError("invalid block/final value %r" % value)
        if filter_default:
            tokens = [token for token in tokens if token in allowed]
            if not tokens:
                return 0
        mapping = {
            "extension": "ZXSD_DERIVATION_EXTENSION",
            "restriction": "ZXSD_DERIVATION_RESTRICTION",
            "substitution": "ZXSD_DERIVATION_SUBSTITUTION",
            "list": "ZXSD_DERIVATION_LIST",
            "union": "ZXSD_DERIVATION_UNION",
        }
        return " | ".join(mapping[token] for token in tokens)

    def method_blocked(flags, method):
        return flags == "ZXSD_DERIVATION_ALL" or method in str(flags)

    # Phase 1: collect global declarations from the complete schema graph.
    declaration_stream = []
    for document in documents:
        for n in document["root"]:
            if lname(n.tag) not in ("include", "import", "redefine"):
                declaration_stream.append((n, document))
    # Redefinitions are applied only after every original declaration in the
    # connected graph is visible. This is required for legal cyclic include /
    # redefine graphs, while the post-order document list still preserves
    # inner-before-outer redefine chains.
    for document in documents:
        for redefine, _ in document["redefinitions"]:
            for n in redefine:
                declaration_stream.append((n, document))
    for n, document in declaration_stream:
        for descendant in n.iter():
            contexts[id(descendant)] = document
        k = lname(n.tag)
        if k in ("complexType", "simpleType"):
            check(
                n,
                (
                    {"name", "id", "mixed", "abstract", "block", "final"}
                    if k == "complexType"
                    else {"name", "id", "final"}
                ),
            )
            name = normalize_token(n.get("name", ""))
            n.set("name", name)
            if not valid_ncname(name):
                raise InvalidSchemaError("global xs:%s requires a valid NCName" % k)
            if document["target"] == XSD_URI and name == "anySimpleType":
                # The schema-for-schemas fixture carries a compatibility
                # declaration for the built-in ur-type; references still
                # denote the built-in component.
                continue
            key = schema_key(document["target"], name)
            is_redefinition = id(n) in redefinition_nodes
            if key in type_nodes and not is_redefinition:
                raise SchemaError("duplicate type %r" % name)
            if is_redefinition:
                _, dependency = redefinition_nodes[id(n)]
                if not dependency_declares(dependency, k, name):
                    raise InvalidSchemaError("xs:%s redefines unknown component %r" % (k, name))
                if key not in type_nodes:
                    raise SchemaError("xs:%s redefines unknown component %r" % (k, name))
                if not dependency_contains_node(dependency, type_nodes[key]):
                    raise InvalidSchemaError(
                        "xs:%s redefinition conflicts with another schema document" % k
                    )
                alias = name + "__redefined_base"
                alias_key = schema_key(document["target"], alias)
                while alias_key in type_nodes:
                    alias += "_"
                    alias_key = schema_key(document["target"], alias)
                type_nodes[alias_key] = type_nodes[key]
                derivation = list(n)[0] if list(n) else None
                base_node = derivation
                if derivation is not None and lname(derivation.tag) in (
                    "complexContent",
                    "simpleContent",
                ):
                    base_node = list(derivation)[0] if list(derivation) else None
                original_base = base_node.get("base") if base_node is not None else None
                if (
                    base_node is None
                    or lname(base_node.tag) not in ("restriction", "extension")
                    or referenced_key(original_base, document, base_node) != key
                ):
                    raise InvalidSchemaError(
                        "xs:%s redefinition must derive from its original definition" % k
                    )
                if derivation is not None and lname(derivation.tag) in (
                    "restriction",
                    "list",
                    "union",
                ):
                    original_base = derivation.get("base", "")
                    if original_base.split(":")[-1] == name:
                        prefix = original_base.rsplit(":", 1)[0] if ":" in original_base else None
                        derivation.set("base", (prefix + ":" if prefix else "") + alias)
                elif (
                    derivation is not None
                    and lname(derivation.tag) in ("complexContent", "simpleContent")
                    and list(derivation)
                ):
                    original_base = list(derivation)[0].get("base", "")
                    if original_base.split(":")[-1] == name:
                        prefix = original_base.rsplit(":", 1)[0] if ":" in original_base else None
                        list(derivation)[0].set("base", (prefix + ":" if prefix else "") + alias)
            type_nodes[key] = n
        elif k == "element":
            check(
                n,
                {
                    "name",
                    "type",
                    "id",
                    "abstract",
                    "default",
                    "fixed",
                    "nillable",
                    "block",
                    "substitutionGroup",
                    "block",
                    "final",
                },
            )
            name = normalize_token(n.get("name", ""))
            n.set("name", name)
            if not valid_ncname(name):
                raise InvalidSchemaError("global xs:element requires a valid NCName")
            key = schema_key(document["target"], name)
            if key in global_nodes:
                raise SchemaError("duplicate global element %r" % name)
            global_nodes[key] = n
            order.append(key)
        elif k == "attribute":
            check(n, {"name", "type", "id", "default", "fixed"})
            name = normalize_token(n.get("name", ""))
            n.set("name", name)
            if not valid_ncname(name):
                raise InvalidSchemaError("global xs:attribute requires a valid NCName")
            if document["target"] == XSI_URI:
                raise InvalidSchemaError(
                    "a global attribute cannot target the XMLSchema-instance namespace"
                )
            key = schema_key(document["target"], name)
            if key in global_attribute_nodes:
                raise SchemaError("duplicate global attribute %r" % name)
            global_attribute_nodes[key] = n
            attribute_order.append(key)
        elif k in ("group", "attributeGroup"):
            check(n, {"name", "id"})
            name = normalize_token(n.get("name", ""))
            n.set("name", name)
            if not valid_ncname(name):
                raise InvalidSchemaError("global xs:%s requires a valid NCName" % k)
            if k == "group":
                group_children = list(n)
                if len(group_children) != 1 or lname(group_children[0].tag) not in (
                    "sequence",
                    "choice",
                    "all",
                ):
                    raise SchemaError("global xs:group requires exactly one content model")
                if (
                    group_children[0].get("minOccurs") is not None
                    or group_children[0].get("maxOccurs") is not None
                ):
                    raise SchemaError("a global model-group compositor cannot specify occurrences")

                def validate_particle_children(particle):
                    """Reject illegal particle content even in otherwise unused groups."""
                    particle_kind = lname(particle.tag)
                    children = list(particle)
                    if particle_kind in ("any", "group"):
                        if children:
                            raise InvalidSchemaError(
                                "xs:%s cannot contain child components" % particle_kind
                            )
                        return
                    if particle_kind == "element":
                        return
                    allowed = (
                        {"element"}
                        if particle_kind == "all"
                        else {"element", "group", "choice", "sequence", "any"}
                    )
                    for child in children:
                        child_kind = lname(child.tag)
                        if child_kind not in allowed:
                            raise InvalidSchemaError(
                                "xs:%s is not legal in xs:%s" % (child_kind, particle_kind)
                            )
                        validate_particle_children(child)

                validate_particle_children(group_children[0])
            key = schema_key(document["target"], name)
            nodes = group_nodes if k == "group" else attribute_group_nodes
            is_redefinition = id(n) in redefinition_nodes
            if key in nodes and not is_redefinition:
                raise SchemaError("duplicate %s %r" % (k, name))
            if is_redefinition:
                alias_redefined_component(nodes, key, name, n, document, k)
            nodes[key] = n
        elif k == "notation":
            check(n, {"name", "id", "public", "system"})
            name = n.get("name")
            if not valid_ncname(name):
                raise SchemaError("xs:notation requires a valid NCName")
            if list(n) or (n.text is not None and n.text.strip()):
                raise SchemaError("xs:notation accepts only an optional annotation")
            public = normalize_token(n.get("public", "")) if n.get("public") is not None else None
            system = normalize_token(n.get("system")) if n.get("system") is not None else None
            if public is None and system is None:
                raise SchemaError("xs:notation requires public or system")
            key = schema_key(document["target"], name)
            if key in notation_nodes:
                raise SchemaError("duplicate notation %r" % name)
            notation_nodes[key] = {
                "name": name,
                "ns": document["target"],
                "public": public,
                "system": system,
            }
            notation_order.append(key)
        else:
            raise SchemaError("unsupported XSD construct 'xs:%s'" % k)
    order.sort(
        key=lambda key: (0 if contexts[id(global_nodes[key])]["path"] == main["path"] else 1)
    )
    types = {}
    visiting = set()
    anon = [0]
    globals_ = {}
    global_visiting = set()
    group_visiting = set()
    anonymous_types = {}
    anonymous_recursive_optional = {}
    attribute_groups = {}
    attribute_group_visiting = set()
    parsed_global_attributes = {}
    global_attribute_visiting = set()

    def qname(q, what, node=None):
        """Resolve one lexical QName in the declaring document's context."""
        document = contexts.get(id(node), main) if node is not None else main
        mapping = document.get("element_nsmaps", {}).get(id(node), document["nsmap"])
        if not q:
            raise SchemaError("missing %s reference" % what)
        q = normalize_token(q)
        if not valid_qname(q):
            raise InvalidSchemaError("invalid QName %r for %s" % (q, what))
        if ":" in q:
            p, n = q.split(":", 1)
            if p not in mapping:
                raise SchemaError("undeclared QName prefix %r" % p)
            return mapping[p], n
        uri = mapping.get("")
        context = document
        if uri is None and context.get("chameleon"):
            uri = context["target"]
        return uri, q

    def ensure_any_type():
        """Create the built-in xs:anyType descriptor when it is referenced."""
        key = "__builtin_anyType"
        if key not in types:
            types[key] = {
                "kind": "complex",
                "model": "ZXSD_SEQUENCE",
                "group_min": 0,
                "group_max": None,
                "elements": [],
                "attributes": [],
                "simple_content": None,
                "abstract": False,
                "particle": {
                    "kind": "any",
                    "min": 0,
                    "max": None,
                    "children": [],
                    "namespace": "##any",
                    "process": "lax",
                },
                "any_attribute": {"namespace": "##any", "process": "lax"},
                "base_ref": None,
                "derivation": 0,
                "final": 0,
                "type_name": None,
                "type_ns": XSD_URI,
                "mixed": True,
                "any_type": True,
            }
        return key

    def ensure_any_simple_type():
        """Create a descriptor that preserves the simple-type ur-type identity."""
        key = "__builtin_anySimpleType"
        if key not in types:
            types[key] = {
                "kind": "simple",
                "base": "ZXSD_T_STRING",
                "facets": {},
                "enums": [],
                "whitespace": "ZXSD_WS_PRESERVE",
                "variety": "ZXSD_SIMPLE_ATOMIC",
                "item": None,
                "members": [],
                "patterns": [],
                "fixed_facets": {},
                "base_ref": None,
                "derivation": 0,
                "final": 0,
                "type_name": "anySimpleType",
                "type_ns": XSD_URI,
                "any_simple_type": True,
            }
        return key

    def resolve_type(q, node=None, defer_complex=False):
        """Resolve a built-in or user-defined type to an internal reference."""
        uri, n = qname(q, "type", node)
        if uri == XSD_URI and schema_key(uri, n) not in type_nodes:
            if n == "anyType":
                key = ensure_any_type()
                return {
                    "base": "ZXSD_T_COMPLEX",
                    "simple": None,
                    "complex": key,
                    "builtin": "anyType",
                    "list_builtin": False,
                }
            if n == "anySimpleType":
                key = ensure_any_simple_type()
                return {
                    "base": "ZXSD_T_STRING",
                    "simple": key,
                    "complex": None,
                    "builtin": "anySimpleType",
                    "list_builtin": False,
                }
            if n not in PRIMITIVES:
                if schema_key(uri, n) not in type_nodes:
                    raise SchemaError("unknown XML Schema type xs:%s" % n)
            elif n in LIST_BUILTINS:
                generated_name = "__builtin_" + n
                if generated_name not in types:
                    singular = {
                        "NMTOKENS": "NMTOKEN",
                        "IDREFS": "IDREF",
                        "ENTITIES": "ENTITY",
                    }[n]
                    item = {
                        "base": PRIMITIVES[singular],
                        "simple": None,
                        "complex": None,
                        "builtin": singular,
                        "list_builtin": False,
                    }
                    types[generated_name] = {
                        "kind": "simple",
                        "base": PRIMITIVES[singular],
                        "facets": {},
                        "enums": [],
                        "whitespace": "ZXSD_WS_COLLAPSE",
                        "variety": "ZXSD_SIMPLE_LIST",
                        "item": item,
                        "members": [],
                    }
                return {
                    "base": PRIMITIVES[n],
                    "simple": generated_name,
                    "complex": None,
                    "builtin": n,
                    "list_builtin": True,
                }
            else:
                return {
                    "base": PRIMITIVES[n],
                    "simple": None,
                    "complex": None,
                    "builtin": n,
                    "list_builtin": n in LIST_BUILTINS,
                }
        key = schema_key(uri, n)
        if q in types:
            key = q
        if key in types:
            t = types[key]
            return {
                "base": t.get("base", "ZXSD_T_COMPLEX"),
                "simple": key if t["kind"] == "simple" else None,
                "complex": key if t["kind"] == "complex" else None,
                "builtin": None,
                "list_builtin": False,
            }
        if key not in type_nodes:
            raise SchemaError("invalid reference to type %r" % q)
        if defer_complex and lname(type_nodes[key].tag) == "complexType":
            return {
                "base": "ZXSD_T_COMPLEX",
                "simple": None,
                "complex": key,
                "builtin": None,
                "list_builtin": False,
            }
        if key in visiting:
            if lname(type_nodes[key].tag) != "complexType":
                raise SchemaError("recursive simple type definition involving %r" % key)
            return {
                "base": "ZXSD_T_COMPLEX",
                "simple": None,
                "complex": key,
                "builtin": None,
                "list_builtin": False,
            }
        parse_named(key)
        t = types[key]
        return {
            "base": t.get("base", "ZXSD_T_COMPLEX"),
            "simple": key if t["kind"] == "simple" else None,
            "complex": key if t["kind"] == "complex" else None,
            "builtin": None,
            "list_builtin": False,
        }

    def parse_simple(n, name):
        """Compile a simple type, including restrictions, lists, and unions."""
        check(n, {"name", "id", "final"})
        children = list(n)
        context = contexts.get(id(n), main)
        declared_name = n.get("name")
        if declared_name is not None and not any(
            candidate is n for candidate in type_nodes.values()
        ):
            raise InvalidSchemaError("an anonymous xs:simpleType cannot have a name")
        final = derivation_methods(
            n.get("final", context["final_default"]),
            {"restriction", "list", "union"},
            n.get("final") is None,
        )
        type_name = n.get("name")
        type_namespace = context["target"] if type_name else None
        if len(children) != 1:
            raise SchemaError("xs:simpleType requires exactly one variety")
        variety = lname(children[0].tag)
        if variety not in ("restriction", "list", "union"):
            raise SchemaError("unsupported XSD construct 'xs:%s'" % variety)

        if variety == "list":
            node = children[0]
            check(node, {"itemType", "id"})
            nested = list(node)
            if bool(node.get("itemType")) == bool(nested):
                raise SchemaError("xs:list requires exactly one itemType or nested simpleType")
            if nested:
                if len(nested) != 1 or lname(nested[0].tag) != "simpleType":
                    raise SchemaError("xs:list requires a simpleType child")
                anon[0] += 1
                item_name = ident(name) + "__item_%d" % anon[0]
                parse_simple(nested[0], item_name)
                item = {
                    "base": types[item_name]["base"],
                    "simple": item_name,
                    "complex": None,
                    "builtin": None,
                    "list_builtin": False,
                }
            else:
                item = resolve_type(node.get("itemType"), node)
            if item["complex"]:
                raise SchemaError("xs:list item type must be simple")
            if item.get("list_builtin") or (
                item.get("simple") and types[item["simple"]].get("variety") == "ZXSD_SIMPLE_LIST"
            ):
                raise InvalidSchemaError("xs:list item type cannot itself be a list")
            if item["base"] == "ZXSD_T_NOTATION" and not item.get("simple"):
                raise SchemaError("xs:NOTATION cannot be used directly")
            if item.get("simple") and method_blocked(
                types[item["simple"]].get("final", 0), "ZXSD_DERIVATION_LIST"
            ):
                raise SchemaError("base type final blocks list derivation")
            types[name] = {
                "kind": "simple",
                "base": item["base"],
                "facets": {},
                "enums": [],
                "whitespace": "ZXSD_WS_COLLAPSE",
                "variety": "ZXSD_SIMPLE_LIST",
                "item": item,
                "members": [],
                "base_ref": None,
                "derivation": "ZXSD_DERIVATION_LIST",
                "final": final,
                "type_name": type_name,
                "type_ns": type_namespace,
            }
            return

        if variety == "union":
            node = children[0]
            check(node, {"memberTypes", "id"})
            members = []
            for q in node.get("memberTypes", "").split():
                members.append(resolve_type(q, node))
            for child in node:
                if lname(child.tag) != "simpleType":
                    raise SchemaError("xs:union accepts only simpleType children")
                anon[0] += 1
                member_name = ident(name) + "__member_%d" % anon[0]
                parse_simple(child, member_name)
                members.append(
                    {
                        "base": types[member_name]["base"],
                        "simple": member_name,
                        "complex": None,
                        "builtin": None,
                        "list_builtin": False,
                    }
                )
            if not members or any(x["complex"] for x in members):
                raise SchemaError("xs:union requires simple member types")
            if any(x["base"] == "ZXSD_T_NOTATION" and not x.get("simple") for x in members):
                raise SchemaError("xs:NOTATION cannot be used directly")
            if any(
                x.get("simple")
                and method_blocked(types[x["simple"]].get("final", 0), "ZXSD_DERIVATION_UNION")
                for x in members
            ):
                raise SchemaError("member type final blocks union derivation")
            types[name] = {
                "kind": "simple",
                "base": "ZXSD_T_STRING",
                "facets": {},
                "enums": [],
                "whitespace": "ZXSD_WS_COLLAPSE",
                "variety": "ZXSD_SIMPLE_UNION",
                "item": None,
                "members": members,
                "base_ref": None,
                "derivation": "ZXSD_DERIVATION_UNION",
                "final": final,
                "type_name": type_name,
                "type_ns": type_namespace,
            }
            return

        r = children[0]
        check(r, {"base", "id"})
        nested = [x for x in r if lname(x.tag) == "simpleType"]
        if bool(r.get("base")) == bool(nested):
            raise SchemaError("xs:restriction requires exactly one base or nested simpleType")
        if nested:
            if len(nested) != 1:
                raise SchemaError("xs:restriction permits one nested simpleType")
            anon[0] += 1
            base_name = ident(name) + "__base_%d" % anon[0]
            parse_simple(nested[0], base_name)
            base = {
                "base": types[base_name]["base"],
                "simple": base_name,
                "complex": None,
                "builtin": None,
                "list_builtin": False,
            }
        else:
            base = resolve_type(r.get("base"), r)
        if base.get("builtin") == "anySimpleType" and not schema_for_schemas:
            raise InvalidSchemaError("xs:anySimpleType cannot be used as a restriction base")
        if base["complex"]:
            raise SchemaError("simple type cannot restrict a complex type")
        inherited = types[base["simple"]] if base["simple"] else None
        if inherited and method_blocked(inherited.get("final", 0), "ZXSD_DERIVATION_RESTRICTION"):
            raise SchemaError("base type final blocks restriction")
        primitive = base["base"]
        if schema_for_schemas and type_namespace == XSD_URI and type_name in PRIMITIVES:
            primitive = PRIMITIVES[type_name]
        facets = (
            {key: value for key, value in inherited["facets"].items() if key != "pattern"}
            if inherited
            else {}
        )
        inherited_facets = dict(facets)
        patterns = list(inherited.get("patterns", [])) if inherited else []
        enums = list(inherited["enums"]) if inherited else []
        enum_namespaces = list(inherited.get("enum_namespaces", [])) if inherited else []
        whitespace = (
            inherited["whitespace"]
            if inherited
            else (
                "ZXSD_WS_PRESERVE"
                if base.get("builtin") in WHITESPACE_PRESERVE
                else (
                    "ZXSD_WS_REPLACE"
                    if base.get("builtin") in WHITESPACE_REPLACE
                    else "ZXSD_WS_COLLAPSE"
                )
            )
        )
        simple_variety = (
            inherited["variety"]
            if inherited
            else ("ZXSD_SIMPLE_LIST" if base.get("list_builtin") else "ZXSD_SIMPLE_ATOMIC")
        )
        item = inherited.get("item") if inherited else None
        if base.get("list_builtin"):
            singular = {"NMTOKENS": "NMTOKEN", "IDREFS": "IDREF", "ENTITIES": "ENTITY"}[
                base["builtin"]
            ]
            item = {
                "base": PRIMITIVES[singular],
                "simple": None,
                "complex": None,
                "builtin": singular,
                "list_builtin": False,
            }
            facets["minLength"] = max(1, facets.get("minLength", 0))
        members = list(inherited.get("members", [])) if inherited else []
        fixed_facets = dict(inherited.get("fixed_facets", {})) if inherited else {}
        if primitive in {
            "ZXSD_T_INTEGER",
            "ZXSD_T_LONG",
            "ZXSD_T_INT",
            "ZXSD_T_SHORT",
            "ZXSD_T_BYTE",
            "ZXSD_T_NON_POSITIVE_INTEGER",
            "ZXSD_T_NEGATIVE_INTEGER",
            "ZXSD_T_NON_NEGATIVE_INTEGER",
            "ZXSD_T_POSITIVE_INTEGER",
            "ZXSD_T_UNSIGNED_LONG",
            "ZXSD_T_UNSIGNED_INT",
            "ZXSD_T_UNSIGNED_SHORT",
            "ZXSD_T_UNSIGNED_BYTE",
        }:
            facets.setdefault("fractionDigits", 0)
            fixed_facets["fractionDigits"] = True
        local_enums = []
        local_patterns = []
        local_facet_values = {}
        allowed = {
            "enumeration",
            "length",
            "minLength",
            "maxLength",
            "minInclusive",
            "maxInclusive",
            "minExclusive",
            "maxExclusive",
            "pattern",
            "whiteSpace",
            "totalDigits",
            "fractionDigits",
        }
        length_facets = {"length", "minLength", "maxLength"}
        bound_facets = {"minInclusive", "maxInclusive", "minExclusive", "maxExclusive"}
        common_facets = {"enumeration", "pattern", "whiteSpace"}
        decimal_bases = {
            "ZXSD_T_DECIMAL",
            "ZXSD_T_INTEGER",
            "ZXSD_T_LONG",
            "ZXSD_T_INT",
            "ZXSD_T_SHORT",
            "ZXSD_T_BYTE",
            "ZXSD_T_NON_POSITIVE_INTEGER",
            "ZXSD_T_NEGATIVE_INTEGER",
            "ZXSD_T_NON_NEGATIVE_INTEGER",
            "ZXSD_T_POSITIVE_INTEGER",
            "ZXSD_T_UNSIGNED_LONG",
            "ZXSD_T_UNSIGNED_INT",
            "ZXSD_T_UNSIGNED_SHORT",
            "ZXSD_T_UNSIGNED_BYTE",
        }
        ordered_non_decimal_bases = {
            "ZXSD_T_FLOAT",
            "ZXSD_T_DOUBLE",
            "ZXSD_T_DURATION",
            "ZXSD_T_DATE",
            "ZXSD_T_DATE_TIME",
            "ZXSD_T_TIME",
            "ZXSD_T_GYEAR",
            "ZXSD_T_GYEAR_MONTH",
            "ZXSD_T_GMONTH",
            "ZXSD_T_GMONTH_DAY",
            "ZXSD_T_GDAY",
        }
        length_bases = {
            "ZXSD_T_STRING",
            "ZXSD_T_NORMALIZED_STRING",
            "ZXSD_T_TOKEN",
            "ZXSD_T_LANGUAGE",
            "ZXSD_T_NAME",
            "ZXSD_T_NCNAME",
            "ZXSD_T_NMTOKEN",
            "ZXSD_T_ID",
            "ZXSD_T_IDREF",
            "ZXSD_T_ENTITY",
            "ZXSD_T_QNAME",
            "ZXSD_T_ANY_URI",
            "ZXSD_T_HEX_BINARY",
            "ZXSD_T_BASE64_BINARY",
            "ZXSD_T_NOTATION",
        }
        for f in r:
            k = lname(f.tag)
            if k == "simpleType":
                continue
            check(f, {"value", "id", "fixed"})
            if list(f):
                raise InvalidSchemaError("xs:%s cannot contain child components" % k)
            if k not in allowed:
                raise SchemaError("unsupported XSD construct 'xs:%s'" % k)
            if simple_variety == "ZXSD_SIMPLE_UNION" and k not in {
                "enumeration",
                "pattern",
            }:
                raise InvalidSchemaError("xs:%s is not a valid union facet" % k)
            if simple_variety == "ZXSD_SIMPLE_LIST" and k not in (common_facets | length_facets):
                raise InvalidSchemaError("xs:%s is not a valid list facet" % k)
            if simple_variety == "ZXSD_SIMPLE_ATOMIC":
                primitive_facets = set(common_facets)
                if primitive in length_bases:
                    primitive_facets.update(length_facets)
                if primitive in decimal_bases:
                    primitive_facets.update(bound_facets | {"totalDigits", "fractionDigits"})
                if primitive in ordered_non_decimal_bases:
                    primitive_facets.update(bound_facets)
                if k not in primitive_facets:
                    raise InvalidSchemaError("xs:%s is not a valid facet for this atomic type" % k)
            if primitive == "ZXSD_T_NOTATION" and k not in (
                "enumeration",
                "length",
                "minLength",
                "maxLength",
                "pattern",
                "whiteSpace",
            ):
                raise SchemaError("xs:%s is not a valid NOTATION facet" % k)
            if f.get("value") is None:
                raise SchemaError("xs:%s requires value" % k)
            v = f.get("value")
            fixed_value = f.get("fixed", "false")
            if fixed_value not in ("true", "false", "1", "0"):
                raise InvalidSchemaError("invalid fixed value on xs:%s" % k)
            if k not in ("enumeration", "pattern") and k in local_facet_values:
                raise InvalidSchemaError("duplicate xs:%s facet" % k)
            if k not in ("enumeration", "pattern"):
                local_facet_values[k] = v
            if fixed_facets.get(k) and k in facets and str(facets[k]) != v:
                raise InvalidSchemaError("xs:%s is fixed in the base type" % k)
            if fixed_value in ("true", "1"):
                fixed_facets[k] = True
            if k == "enumeration":
                local_enums.append((v, f))
            elif k in (
                "length",
                "minLength",
                "maxLength",
                "totalDigits",
                "fractionDigits",
            ):
                if not re.fullmatch(r"0|[1-9][0-9]*", v):
                    raise SchemaError("invalid xs:%s value" % k)
                if k == "totalDigits" and v == "0":
                    raise SchemaError("xs:totalDigits must be positive")
                if k in ("totalDigits", "fractionDigits") and primitive not in (
                    "ZXSD_T_DECIMAL",
                    "ZXSD_T_INTEGER",
                    "ZXSD_T_LONG",
                    "ZXSD_T_INT",
                    "ZXSD_T_SHORT",
                    "ZXSD_T_BYTE",
                    "ZXSD_T_NON_POSITIVE_INTEGER",
                    "ZXSD_T_NEGATIVE_INTEGER",
                    "ZXSD_T_NON_NEGATIVE_INTEGER",
                    "ZXSD_T_POSITIVE_INTEGER",
                    "ZXSD_T_UNSIGNED_LONG",
                    "ZXSD_T_UNSIGNED_INT",
                    "ZXSD_T_UNSIGNED_SHORT",
                    "ZXSD_T_UNSIGNED_BYTE",
                ):
                    raise SchemaError("xs:%s requires a decimal-derived type" % k)
                facets[k] = int(v)
            elif k == "whiteSpace":
                values = {
                    "preserve": "ZXSD_WS_PRESERVE",
                    "replace": "ZXSD_WS_REPLACE",
                    "collapse": "ZXSD_WS_COLLAPSE",
                }
                if v not in values:
                    raise SchemaError("invalid xs:whiteSpace value")
                ranks = {
                    "ZXSD_WS_PRESERVE": 0,
                    "ZXSD_WS_REPLACE": 1,
                    "ZXSD_WS_COLLAPSE": 2,
                }
                if ranks[values[v]] < ranks[whitespace]:
                    raise SchemaError("xs:whiteSpace cannot weaken the base of %r" % name)
                whitespace = values[v]
            else:
                if k == "pattern":
                    validate_pattern(v)
                    local_patterns.append(v)
                else:
                    facets[k] = v
        if "length" in local_facet_values and (
            "minLength" in local_facet_values or "maxLength" in local_facet_values
        ):
            raise InvalidSchemaError("xs:length cannot be combined with minLength or maxLength")
        if "minInclusive" in local_facet_values and "minExclusive" in local_facet_values:
            raise InvalidSchemaError("minInclusive and minExclusive are mutually exclusive")
        if "maxInclusive" in local_facet_values and "maxExclusive" in local_facet_values:
            raise InvalidSchemaError("maxInclusive and maxExclusive are mutually exclusive")
        for facet_name in (
            "minInclusive",
            "maxInclusive",
            "minExclusive",
            "maxExclusive",
        ):
            if facet_name in local_facet_values and not value_matches_type(
                base, local_facet_values[facet_name]
            ):
                raise InvalidSchemaError("invalid xs:%s value for its base type" % facet_name)
        if local_patterns:
            combined = (
                local_patterns[0]
                if len(local_patterns) == 1
                else "|".join("(" + value + ")" for value in local_patterns)
            )
            validate_pattern(combined)
            patterns.append(combined)
        if patterns:
            facets["pattern"] = patterns[-1]
        if local_enums:
            local_values = [value for value, _ in local_enums]
            if whitespace == "ZXSD_WS_REPLACE":
                local_values = [re.sub(r"[\t\r\n]", " ", x) for x in local_values]
            elif whitespace == "ZXSD_WS_COLLAPSE":
                local_values = [normalize_token(x) for x in local_values]
            if primitive == "ZXSD_T_NOTATION":
                resolved_values = []
                resolved_namespaces = []
                for value, facet_node in zip(local_values, (node for _, node in local_enums)):
                    if not valid_qname(value):
                        raise SchemaError("invalid NOTATION enumeration QName %r" % value)
                    uri, local_name = qname(value, "notation", facet_node)
                    key = schema_key(uri, local_name)
                    if key not in notation_nodes:
                        raise SchemaError(
                            "NOTATION enumeration refers to undeclared notation %r" % value
                        )
                    resolved_values.append(local_name)
                    resolved_namespaces.append(uri)
                if inherited and inherited["enums"]:
                    allowed_notations = set(
                        zip(inherited["enums"], inherited.get("enum_namespaces", []))
                    )
                    if any(
                        item not in allowed_notations
                        for item in zip(resolved_values, resolved_namespaces)
                    ):
                        raise SchemaError("NOTATION enumeration widens its base type")
                enums = resolved_values
                enum_namespaces = resolved_namespaces
            else:
                base_type = dict(base)
                for value in local_values:
                    if not value_matches_type(base_type, value):
                        raise InvalidSchemaError("enumeration value is invalid for its base type")
                enums = local_values
                enum_namespaces = []
        if (
            primitive == "ZXSD_T_NOTATION"
            and not enums
            and not (schema_for_schemas and type_namespace == XSD_URI and type_name == "NOTATION")
        ):
            raise SchemaError("NOTATION restrictions require at least one enumeration")
        if "length" in facets and (
            facets["length"] < facets.get("minLength", 0)
            or facets["length"] > facets.get("maxLength", 2**63)
        ):
            raise SchemaError("length is outside inherited length bounds")
        if (
            "minLength" in local_facet_values
            and "maxLength" in local_facet_values
            and facets["minLength"] > facets["maxLength"]
        ):
            raise SchemaError("minLength exceeds maxLength")
        if (
            "minLength" in local_facet_values
            and "minLength" in inherited_facets
            and facets["minLength"] < inherited_facets["minLength"]
        ):
            raise InvalidSchemaError("minLength widens its base type")
        if (
            "maxLength" in local_facet_values
            and "maxLength" in inherited_facets
            and facets["maxLength"] > inherited_facets["maxLength"]
        ):
            raise InvalidSchemaError("maxLength widens its base type")
        if (
            "totalDigits" in local_facet_values
            and "totalDigits" in inherited_facets
            and facets["totalDigits"] > inherited_facets["totalDigits"]
        ):
            raise InvalidSchemaError("totalDigits widens its base type")
        if (
            "fractionDigits" in local_facet_values
            and "fractionDigits" in inherited_facets
            and facets["fractionDigits"] > inherited_facets["fractionDigits"]
        ):
            raise InvalidSchemaError("fractionDigits widens its base type")
        if facets.get("fractionDigits", 0) > facets.get("totalDigits", 2**63):
            raise SchemaError("fractionDigits exceeds totalDigits")
        lower = facets.get("minInclusive", facets.get("minExclusive"))
        upper = facets.get("maxInclusive", facets.get("maxExclusive"))
        ordered_bases = {
            "ZXSD_T_INT",
            "ZXSD_T_UNSIGNED_INT",
            "ZXSD_T_INTEGER",
            "ZXSD_T_DECIMAL",
            "ZXSD_T_FLOAT",
            "ZXSD_T_DOUBLE",
            "ZXSD_T_LONG",
            "ZXSD_T_SHORT",
            "ZXSD_T_BYTE",
            "ZXSD_T_NON_POSITIVE_INTEGER",
            "ZXSD_T_NEGATIVE_INTEGER",
            "ZXSD_T_NON_NEGATIVE_INTEGER",
            "ZXSD_T_POSITIVE_INTEGER",
            "ZXSD_T_UNSIGNED_LONG",
            "ZXSD_T_UNSIGNED_SHORT",
            "ZXSD_T_UNSIGNED_BYTE",
            "ZXSD_T_DURATION",
            "ZXSD_T_DATE",
            "ZXSD_T_DATE_TIME",
            "ZXSD_T_TIME",
            "ZXSD_T_GYEAR",
            "ZXSD_T_GYEAR_MONTH",
            "ZXSD_T_GMONTH",
            "ZXSD_T_GMONTH_DAY",
            "ZXSD_T_GDAY",
        }
        if lower is not None and upper is not None and primitive in ordered_bases:
            comparison = compare_ordered_values(primitive, lower, upper)
            if comparison is not None and (
                comparison > 0
                or (comparison == 0 and ("minExclusive" in facets or "maxExclusive" in facets))
            ):
                raise SchemaError("lower value bound is not below the upper bound")
        for local_name in ("maxInclusive", "maxExclusive"):
            if local_name not in local_facet_values:
                continue
            for inherited_name in ("maxInclusive", "maxExclusive"):
                if inherited_name not in inherited_facets:
                    continue
                comparison = compare_ordered_values(
                    primitive,
                    local_facet_values[local_name],
                    inherited_facets[inherited_name],
                )
                if comparison is not None and (
                    comparison > 0
                    or comparison == 0
                    and inherited_name == "maxExclusive"
                    and local_name == "maxInclusive"
                ):
                    raise InvalidSchemaError("upper bound widens its base type")
        for local_name in ("minInclusive", "minExclusive"):
            if local_name not in local_facet_values:
                continue
            for inherited_name in ("minInclusive", "minExclusive"):
                if inherited_name not in inherited_facets:
                    continue
                comparison = compare_ordered_values(
                    primitive,
                    local_facet_values[local_name],
                    inherited_facets[inherited_name],
                )
                if comparison is not None and (
                    comparison < 0
                    or comparison == 0
                    and inherited_name == "minExclusive"
                    and local_name == "minInclusive"
                ):
                    raise InvalidSchemaError("lower bound widens its base type")
        types[name] = {
            "kind": "simple",
            "base": primitive,
            "facets": facets,
            "enums": enums,
            "enum_namespaces": enum_namespaces,
            "whitespace": whitespace,
            "variety": simple_variety,
            "item": item,
            "members": members,
            "patterns": patterns,
            "fixed_facets": fixed_facets,
            "base_ref": base.get("simple"),
            "derivation": "ZXSD_DERIVATION_RESTRICTION",
            "final": final,
            "type_name": type_name,
            "type_ns": type_namespace,
        }

    def value_matches_type(typ, value):
        """Check schema defaults/fixed values against compiled simple facets."""
        if typ.get("complex"):
            definition = types[typ["complex"]]
            if definition.get("simple_content"):
                return value_matches_type(definition["simple_content"], value)
            return definition.get("mixed", False)
        definition = types.get(typ.get("simple")) if typ.get("simple") else None
        whitespace = definition.get("whitespace") if definition else None
        if whitespace == "ZXSD_WS_REPLACE":
            normalized = re.sub(r"[\t\r\n]", " ", value)
        elif whitespace == "ZXSD_WS_COLLAPSE" or typ["base"] != "ZXSD_T_STRING":
            normalized = " ".join(value.split())
        else:
            normalized = value
        if typ.get("list_builtin"):
            items = normalized.split()
            singular = {"NMTOKENS": "NMTOKEN", "IDREFS": "IDREF", "ENTITIES": "ENTITY"}[
                typ["builtin"]
            ]
            item_type = {
                "base": PRIMITIVES[singular],
                "simple": None,
                "complex": None,
                "builtin": singular,
                "list_builtin": False,
            }
            return bool(items) and all(value_matches_type(item_type, item) for item in items)
        if definition and definition.get("variety") == "ZXSD_SIMPLE_LIST":
            items = normalized.split()
            if not all(value_matches_type(definition["item"], item) for item in items):
                return False
            facets = definition.get("facets", {})
            if "length" in facets and len(items) != facets["length"]:
                return False
            if "minLength" in facets and len(items) < facets["minLength"]:
                return False
            if "maxLength" in facets and len(items) > facets["maxLength"]:
                return False
            return True
        if definition and definition.get("variety") == "ZXSD_SIMPLE_UNION":
            return any(value_matches_type(member, normalized) for member in definition["members"])
        base = typ["base"]
        if not primitive_lexical_form(base, normalized):
            return False
        if not definition:
            return True
        if definition.get("enums") and normalized not in definition["enums"]:
            return False
        facets = definition.get("facets", {})
        length = len(normalized)
        if "length" in facets and length != facets["length"]:
            return False
        if "minLength" in facets and length < facets["minLength"]:
            return False
        if "maxLength" in facets and length > facets["maxLength"]:
            return False
        for lower_name in ("minInclusive", "minExclusive"):
            if lower_name in facets:
                comparison = compare_ordered_values(base, normalized, facets[lower_name])
                if comparison is not None and (
                    comparison < 0 or comparison == 0 and lower_name == "minExclusive"
                ):
                    return False
        for upper_name in ("maxInclusive", "maxExclusive"):
            if upper_name in facets:
                comparison = compare_ordered_values(base, normalized, facets[upper_name])
                if comparison is not None and (
                    comparison > 0 or comparison == 0 and upper_name == "maxExclusive"
                ):
                    return False
        if "totalDigits" in facets or "fractionDigits" in facets:
            try:
                decimal = Decimal(normalized).normalize()
                digits = len(decimal.as_tuple().digits) + max(decimal.as_tuple().exponent, 0)
                fraction = max(-decimal.as_tuple().exponent, 0)
                if digits > facets.get("totalDigits", 2**63):
                    return False
                if fraction > facets.get("fractionDigits", 2**63):
                    return False
            except InvalidOperation:
                return False
        for pattern in definition.get("patterns", []):
            try:
                if re.fullmatch(pattern, normalized) is None:
                    return False
            except re.error:
                # The C engine validates XSD-only escapes such as \p and \i.
                pass
        return True

    def comparable_value(typ, value):
        """Return a compact value-space key for schema value constraints."""
        definition = types.get(typ.get("simple")) if typ.get("simple") else None
        whitespace = definition.get("whitespace") if definition else None
        if whitespace == "ZXSD_WS_REPLACE":
            normalized = re.sub(r"[\t\r\n]", " ", value)
        elif whitespace == "ZXSD_WS_COLLAPSE" or typ["base"] != "ZXSD_T_STRING":
            normalized = " ".join(value.split())
        else:
            normalized = value
        if definition and definition.get("variety") == "ZXSD_SIMPLE_LIST":
            return tuple(comparable_value(definition["item"], item) for item in normalized.split())
        if definition and definition.get("variety") == "ZXSD_SIMPLE_UNION":
            for member in definition["members"]:
                if value_matches_type(member, normalized):
                    return comparable_value(member, normalized)
        if typ["base"] == "ZXSD_T_BOOLEAN":
            return normalized in ("true", "1")
        if typ["base"] in {
            "ZXSD_T_INT",
            "ZXSD_T_UNSIGNED_INT",
            "ZXSD_T_INTEGER",
            "ZXSD_T_LONG",
            "ZXSD_T_SHORT",
            "ZXSD_T_BYTE",
            "ZXSD_T_NON_POSITIVE_INTEGER",
            "ZXSD_T_NEGATIVE_INTEGER",
            "ZXSD_T_NON_NEGATIVE_INTEGER",
            "ZXSD_T_POSITIVE_INTEGER",
            "ZXSD_T_UNSIGNED_LONG",
            "ZXSD_T_UNSIGNED_SHORT",
            "ZXSD_T_UNSIGNED_BYTE",
            "ZXSD_T_DECIMAL",
        }:
            try:
                return Decimal(normalized)
            except InvalidOperation:
                pass
        return normalized

    def anon_type(node, parent):
        """Compile the single anonymous type owned by a declaration."""
        kids = [x for x in node if lname(x.tag) in ("simpleType", "complexType")]
        if len(kids) != 1:
            raise SchemaError("element requires exactly one anonymous type when type is omitted")
        if kids[0].get("name") is not None:
            raise InvalidSchemaError("an anonymous type cannot have a name")
        cached = anonymous_types.get(id(kids[0]))
        if cached is not None:
            return {
                "base": "ZXSD_T_COMPLEX",
                "simple": None,
                "complex": cached,
            }
        anon[0] += 1
        name = ident(parent) + "__anon_%d" % anon[0]
        if lname(kids[0].tag) == "complexType":
            anonymous_types[id(kids[0])] = name
            anonymous_recursive_optional[name] = occurs(node)[0] == 0
            parse_complex(kids[0], name)
        else:
            parse_simple(kids[0], name)
        t = types[name]
        return {
            "base": t.get("base", "ZXSD_T_COMPLEX"),
            "simple": name if t["kind"] == "simple" else None,
            "complex": name if t["kind"] == "complex" else None,
        }

    def attribute_type(node, parent):
        """Resolve an attribute type, defaulting to xs:anySimpleType."""
        if node.get("type"):
            if list(node):
                raise SchemaError("xs:attribute type cannot have an anonymous type")
            resolved = resolve_type(node.get("type"), node)
            if resolved["base"] == "ZXSD_T_NOTATION" and not resolved.get("simple"):
                raise SchemaError("xs:NOTATION cannot be used directly")
            return resolved
        children = list(node)
        if not children:
            return {
                "base": "ZXSD_T_STRING",
                "simple": None,
                "complex": None,
                "builtin": "anySimpleType",
                "list_builtin": False,
            }
        if len(children) != 1 or lname(children[0].tag) != "simpleType":
            raise SchemaError("xs:attribute accepts only one anonymous simpleType")
        anon[0] += 1
        type_name = ident(parent) + "__anon_%d" % anon[0]
        parse_simple(children[0], type_name)
        return {
            "base": types[type_name]["base"],
            "simple": type_name,
            "complex": None,
            "builtin": None,
            "list_builtin": False,
        }

    def parse_element(n, parent, global_=False):
        """Compile a local or global element declaration."""
        context = contexts.get(id(n), main)
        local_target = context["target"]
        local_efd = context["efd"]
        child_kinds = [lname(child.tag) for child in n]
        if any(
            kind not in ("simpleType", "complexType", "unique", "key", "keyref")
            for kind in child_kinds
        ):
            raise InvalidSchemaError("illegal child of xs:element")
        type_positions = [
            index for index, kind in enumerate(child_kinds) if kind in ("simpleType", "complexType")
        ]
        if len(type_positions) > 1 or (type_positions and type_positions[0] != 0):
            raise InvalidSchemaError("an anonymous element type must be its first child")
        if type_positions and n.get("type") is not None:
            raise InvalidSchemaError("xs:element cannot have both type and an anonymous type")
        check(
            n,
            (
                {
                    "name",
                    "type",
                    "ref",
                    "minOccurs",
                    "maxOccurs",
                    "id",
                    "form",
                    "default",
                    "fixed",
                    "nillable",
                    "block",
                }
                if not global_
                else {
                    "name",
                    "type",
                    "id",
                    "abstract",
                    "default",
                    "fixed",
                    "nillable",
                    "substitutionGroup",
                    "block",
                    "final",
                }
            ),
        )
        if n.get("ref"):
            if any(
                n.get(attribute) is not None
                for attribute in (
                    "name",
                    "type",
                    "form",
                    "default",
                    "fixed",
                    "nillable",
                    "block",
                )
            ) or list(n):
                raise SchemaError(
                    "xs:element ref cannot also define name, type, form, value, or nillable"
                )
            uri, rn = qname(n.get("ref"), "element", n)
            ref_key = schema_key(uri, rn)
            if ref_key not in global_nodes:
                raise SchemaError("invalid element ref %r" % n.get("ref"))
            base = parse_global(ref_key)
            name = base["name"]
            ens = base["ns"]
            typ = base["type"]
            metadata = {
                key: base[key]
                for key in (
                    "default",
                    "fixed",
                    "nillable",
                    "abstract",
                    "block",
                    "substitution_head",
                )
            }
        else:
            name = normalize_token(n.get("name", ""))
            n.set("name", name)
            if not valid_ncname(name):
                raise InvalidSchemaError("xs:element requires a valid NCName or ref")
            substitution_head = None
            if global_ and n.get("substitutionGroup") is not None:
                head_uri, head_name = qname(n.get("substitutionGroup"), "substitutionGroup", n)
                substitution_head = (head_name, head_uri)
            if n.get("type"):
                typ = resolve_type(n.get("type"), n, defer_complex=True)
            elif any(lname(child.tag) in ("simpleType", "complexType") for child in n):
                typ = anon_type(n, parent + "_" + name)
            elif substitution_head is not None:
                head_name, head_uri = substitution_head
                head_key = schema_key(head_uri, head_name)
                if head_key not in global_nodes:
                    raise SchemaError("unknown substitutionGroup head %r" % head_name)
                if head_key in global_visiting:
                    raise InvalidSchemaError("circular substitutionGroup declarations")
                typ = parse_global(head_key)["type"]
            else:
                any_type = ensure_any_type()
                typ = {
                    "base": "ZXSD_T_COMPLEX",
                    "simple": None,
                    "complex": any_type,
                    "builtin": "anyType",
                    "list_builtin": False,
                }
            if typ["base"] == "ZXSD_T_NOTATION" and not typ.get("simple"):
                raise SchemaError("xs:NOTATION cannot be used directly")
            form = n.get("form", local_efd)
            if form not in ("qualified", "unqualified"):
                raise SchemaError("invalid element form %r" % form)
            ens = local_target if global_ or form == "qualified" else None
            if n.get("default") is not None and n.get("fixed") is not None:
                raise SchemaError("element cannot have both default and fixed")
            boolean = n.get("nillable", "false")
            abstract = n.get("abstract", "false") if global_ else "false"
            if boolean not in ("true", "false", "1", "0") or abstract not in (
                "true",
                "false",
                "1",
                "0",
            ):
                raise SchemaError("invalid element boolean property")
            block_value = n.get("block", context["block_default"])
            block = derivation_methods(block_value, {"extension", "restriction", "substitution"})
            if global_:
                derivation_methods(
                    n.get("final", context["final_default"]),
                    {"extension", "restriction"},
                    n.get("final") is None,
                )
            metadata = {
                "default": n.get("default"),
                "fixed": n.get("fixed"),
                "nillable": boolean in ("true", "1"),
                "abstract": abstract in ("true", "1"),
                "block": block,
                "substitution_head": substitution_head,
            }
            for value in (metadata["default"], metadata["fixed"]):
                if value is not None and not value_matches_type(typ, value):
                    raise SchemaError("element default/fixed value is invalid for its type")
            if typ["base"] == "ZXSD_T_ID" and (
                metadata["default"] is not None or metadata["fixed"] is not None
            ):
                raise InvalidSchemaError("an ID-typed element cannot have default or fixed")
            if (
                typ["complex"]
                and (metadata["default"] is not None or metadata["fixed"] is not None)
                and not types.get(typ["complex"], {}).get("simple_content")
                and not types.get(typ["complex"], {}).get("mixed")
            ):
                raise SchemaError("element-only complex element cannot have default/fixed")
        lo, hi = (1, 1) if global_ else occurs(n)
        return {
            "name": name,
            "ns": ens,
            "type": typ,
            "min": lo,
            "max": hi,
            "node": n,
            **metadata,
        }

    def parse_attribute(n, parent):
        """Compile an attribute use in its owning complex type."""
        context = contexts.get(id(n), main)
        local_target = context["target"]
        local_afd = context["afd"]
        check(n, {"name", "type", "ref", "use", "id", "form", "default", "fixed"})
        use = n.get("use", "optional")
        if use not in ("optional", "required", "prohibited"):
            raise SchemaError("unsupported attribute use %r" % use)
        if n.get("default") is not None and n.get("fixed") is not None:
            raise SchemaError("attribute cannot have both default and fixed")
        if use == "required" and n.get("default") is not None:
            raise SchemaError("required attribute cannot have a default")
        if use == "prohibited" and n.get("default") is not None:
            raise InvalidSchemaError("a prohibited attribute cannot have a default")
        if n.get("ref"):
            if n.get("name") or n.get("type") or n.get("form") or list(n):
                raise SchemaError("xs:attribute ref cannot also define name, type, or form")
            uri, referenced_name = qname(n.get("ref"), "attribute", n)
            key = schema_key(uri, referenced_name)
            base = parse_global_attribute(key)
            default = n.get("default", base["default"])
            fixed = n.get("fixed", base["fixed"])
            if default is not None and fixed is not None:
                raise SchemaError("attribute ref cannot combine default and fixed values")
            if base["fixed"] is not None and n.get("fixed") not in (
                None,
                base["fixed"],
            ):
                raise SchemaError("attribute ref changes the global fixed value")
            if use == "required" and default is not None:
                raise SchemaError("required attribute cannot have a default")
            for value in (default, fixed):
                if value is not None and not value_matches_type(base["type"], value):
                    raise SchemaError(
                        "attribute %r default/fixed value %r is invalid for its type"
                        % (base["name"], value)
                    )
            return {
                "name": base["name"],
                "ns": base["ns"],
                "type": None if use == "prohibited" else base["type"],
                "required": use == "required",
                "prohibited": use == "prohibited",
                "default": None if use == "prohibited" else default,
                "fixed": None if use == "prohibited" else fixed,
            }
        attribute_name = normalize_token(n.get("name", ""))
        n.set("name", attribute_name)
        if not valid_ncname(attribute_name) or attribute_name == "xmlns":
            raise InvalidSchemaError("xs:attribute requires a valid non-xmlns NCName or ref")
        form = n.get("form", local_afd)
        if form not in ("qualified", "unqualified"):
            raise SchemaError("invalid attribute form %r" % form)
        if form == "qualified" and local_target == XSI_URI:
            raise InvalidSchemaError(
                "a local qualified attribute cannot target the XMLSchema-instance namespace"
            )
        if use == "prohibited":
            return {
                "name": n.get("name"),
                "ns": local_target if form == "qualified" else None,
                "type": None,
                "required": False,
                "prohibited": True,
                "default": None,
                "fixed": None,
            }
        typ = attribute_type(n, parent + "_" + n.get("name"))
        if typ["complex"]:
            raise SchemaError("attribute type must be simple")
        if (
            (typ.get("simple") and types[typ["simple"]]["base"] == "ZXSD_T_ID")
            or typ["base"] == "ZXSD_T_ID"
        ) and (n.get("default") is not None or n.get("fixed") is not None):
            raise InvalidSchemaError("an ID-typed attribute cannot have default or fixed")
        for value in (n.get("default"), n.get("fixed")):
            if value is not None and not value_matches_type(typ, value):
                raise SchemaError(
                    "attribute %r default/fixed value %r is invalid for its type"
                    % (n.get("name"), value)
                )
        return {
            "name": n.get("name"),
            "ns": local_target if form == "qualified" else None,
            "type": typ,
            "required": use == "required",
            "prohibited": use == "prohibited",
            "default": n.get("default"),
            "fixed": n.get("fixed"),
        }

    def parse_global_attribute(key):
        """Compile one global attribute declaration on demand."""
        if key in parsed_global_attributes:
            return parsed_global_attributes[key]
        if key not in global_attribute_nodes:
            raise SchemaError("invalid attribute ref %r" % key.split("|", 1)[-1])
        if key in global_attribute_visiting:
            raise SchemaError("recursive global attribute involving %r" % key)
        global_attribute_visiting.add(key)
        node = global_attribute_nodes[key]
        context = contexts[id(node)]
        typ = attribute_type(node, "global_attribute_" + node.get("name"))
        if typ["complex"]:
            raise SchemaError("global attribute type must be simple")
        if (
            (typ.get("simple") and types[typ["simple"]]["base"] == "ZXSD_T_ID")
            or typ["base"] == "ZXSD_T_ID"
        ) and (node.get("default") is not None or node.get("fixed") is not None):
            raise InvalidSchemaError("an ID-typed attribute cannot have default or fixed")
        if node.get("default") is not None and node.get("fixed") is not None:
            raise SchemaError("global attribute cannot have both default and fixed")
        for value in (node.get("default"), node.get("fixed")):
            if value is not None and not value_matches_type(typ, value):
                raise SchemaError(
                    "global attribute %r default/fixed value %r is invalid for its type"
                    % (node.get("name"), value)
                )
        result = {
            "name": node.get("name"),
            "ns": context["target"],
            "type": typ,
            "default": node.get("default"),
            "fixed": node.get("fixed"),
        }
        parsed_global_attributes[key] = result
        global_attribute_visiting.remove(key)
        return result

    builtin_simple_parents = {
        "ZXSD_T_INTEGER": "ZXSD_T_DECIMAL",
        "ZXSD_T_LONG": "ZXSD_T_INTEGER",
        "ZXSD_T_INT": "ZXSD_T_LONG",
        "ZXSD_T_SHORT": "ZXSD_T_INT",
        "ZXSD_T_BYTE": "ZXSD_T_SHORT",
        "ZXSD_T_NON_POSITIVE_INTEGER": "ZXSD_T_INTEGER",
        "ZXSD_T_NEGATIVE_INTEGER": "ZXSD_T_NON_POSITIVE_INTEGER",
        "ZXSD_T_NON_NEGATIVE_INTEGER": "ZXSD_T_INTEGER",
        "ZXSD_T_POSITIVE_INTEGER": "ZXSD_T_NON_NEGATIVE_INTEGER",
        "ZXSD_T_UNSIGNED_LONG": "ZXSD_T_NON_NEGATIVE_INTEGER",
        "ZXSD_T_UNSIGNED_INT": "ZXSD_T_UNSIGNED_LONG",
        "ZXSD_T_UNSIGNED_SHORT": "ZXSD_T_UNSIGNED_INT",
        "ZXSD_T_UNSIGNED_BYTE": "ZXSD_T_UNSIGNED_SHORT",
        "ZXSD_T_NORMALIZED_STRING": "ZXSD_T_STRING",
        "ZXSD_T_TOKEN": "ZXSD_T_NORMALIZED_STRING",
        "ZXSD_T_LANGUAGE": "ZXSD_T_TOKEN",
        "ZXSD_T_NAME": "ZXSD_T_TOKEN",
        "ZXSD_T_NMTOKEN": "ZXSD_T_TOKEN",
        "ZXSD_T_NCNAME": "ZXSD_T_NAME",
        "ZXSD_T_ID": "ZXSD_T_NCNAME",
        "ZXSD_T_IDREF": "ZXSD_T_NCNAME",
        "ZXSD_T_ENTITY": "ZXSD_T_NCNAME",
    }

    def simple_type_restricts(left, right, seen=None):
        """Return whether one simple type's value space validly restricts another."""
        if left.get("complex") or right.get("complex"):
            return False
        if right.get("builtin") == "anySimpleType":
            return True
        if left.get("simple") == right.get("simple") and left.get("base") == right.get("base"):
            return True
        seen = set() if seen is None else seen
        right_name = right.get("simple")
        left_name = left.get("simple")
        current = left_name
        local_seen = set()
        while current and current not in local_seen:
            if current == right_name:
                return True
            local_seen.add(current)
            definition = types.get(current)
            if not definition or definition.get("derivation") != "ZXSD_DERIVATION_RESTRICTION":
                break
            current = definition.get("base_ref")
        if right_name and right_name not in seen:
            seen.add(right_name)
            definition = types.get(right_name)
            if definition and definition.get("variety") == "ZXSD_SIMPLE_UNION":
                return any(
                    simple_type_restricts(left, member, seen) for member in definition["members"]
                )
        if left_name and types.get(left_name, {}).get("variety") != "ZXSD_SIMPLE_ATOMIC":
            return False
        value = left.get("base")
        target = right.get("base")
        while value != target and value in builtin_simple_parents:
            value = builtin_simple_parents[value]
        return right_name is None and value == target

    def normalized_wildcard_namespace(node):
        """Validate and normalize an XSD wildcard namespace constraint."""
        tokens = node.get("namespace", "##any").split()
        if ("##any" in tokens or "##other" in tokens) and len(tokens) != 1:
            raise InvalidSchemaError("invalid wildcard namespace constraint")
        for token in tokens:
            if token.startswith("##") and token not in (
                "##any",
                "##other",
                "##targetNamespace",
                "##local",
            ):
                raise InvalidSchemaError("invalid wildcard namespace token %r" % token)
        return " ".join(tokens)

    def parse_complex(n, name):
        """Compile complex content, particles, attributes, and derivation."""
        check(n, {"name", "id", "mixed", "abstract", "block", "final"})
        context = contexts.get(id(n), main)
        declared_name = n.get("name")
        if declared_name is not None and not any(
            candidate is n for candidate in type_nodes.values()
        ):
            raise InvalidSchemaError("an anonymous xs:complexType cannot have a name")
        final = derivation_methods(
            n.get("final", context["final_default"]),
            {"extension", "restriction"},
            n.get("final") is None,
        )
        block = derivation_methods(n.get("block", ""), {"extension", "restriction"})
        type_name = n.get("name")
        type_namespace = context["target"] if type_name else None
        mixed_value = n.get("mixed", "false")
        if mixed_value not in ("true", "false", "1", "0"):
            raise SchemaError("invalid mixed value")
        declared_mixed = mixed_value in ("true", "1")
        abstract = n.get("abstract", "false")
        if abstract not in ("true", "false", "1", "0"):
            raise SchemaError("invalid abstract value")

        def wildcard_constraint(node):
            """Validate and normalize an element or attribute wildcard namespace."""
            return normalized_wildcard_namespace(node)

        def parse_particle(node, parent_kind=None, top_level=False):
            """Compile a recursive sequence, choice, all, element, or wildcard."""
            kind = lname(node.tag)
            lo, hi = occurs(node)
            if kind == "group":
                check(node, {"ref", "id", "minOccurs", "maxOccurs"})
                if not node.get("ref") or list(node):
                    raise SchemaError("local xs:group requires ref and no children")
                uri, group_name = qname(node.get("ref"), "group", node)
                key = schema_key(uri, group_name)
                if key not in group_nodes:
                    raise SchemaError("invalid group ref %r" % node.get("ref"))
                visit_key = (key, id(n))
                if visit_key in group_visiting:
                    raise SchemaError("circular model group reference involving %r" % group_name)
                group_visiting.add(visit_key)
                definition = group_nodes[key]
                children = list(definition)
                if len(children) != 1 or lname(children[0].tag) not in (
                    "sequence",
                    "choice",
                    "all",
                ):
                    raise SchemaError("global xs:group requires exactly one content model")
                if (
                    children[0].get("minOccurs") is not None
                    or children[0].get("maxOccurs") is not None
                ):
                    raise SchemaError("a global model-group compositor cannot specify occurrences")
                expanded = dict(parse_particle(children[0], parent_kind, top_level))

                def validate_redefinition_chain(current_definition, current_particle, seen):
                    """Validate every link in a chain of model-group redefinitions."""
                    marker = id(current_definition)
                    if marker in seen:
                        raise InvalidSchemaError("circular xs:group redefinition")
                    seen.add(marker)
                    base_key = redefined_bases.get(marker)
                    if base_key is not None:
                        base_definition = group_nodes[base_key]
                        base_children = list(base_definition)
                        # A model-group definition is always checked in its own
                        # top-level context. Its reference site must not make
                        # an xs:all in the redefined base look nested.
                        base_particle = parse_particle(base_children[0], None, True)
                        validate_redefinition_chain(base_definition, base_particle, seen)
                        if not redefined_self_references.get(
                            marker, False
                        ) and particle_restriction_is_invalid(current_particle, base_particle):
                            raise InvalidSchemaError(
                                "xs:group redefinition widens its original content model"
                            )
                    seen.remove(marker)

                validate_redefinition_chain(definition, expanded, set())
                if expanded["kind"] == "all" and (lo not in (0, 1) or hi != 1):
                    raise InvalidSchemaError(
                        "a reference to an xs:all group must occur 0..1 or 1..1"
                    )
                group_visiting.remove(visit_key)
                expanded["min"] = lo
                expanded["max"] = hi
                return expanded
            if kind == "element":
                element = parse_element(node, name)
                return {
                    "kind": "element",
                    "min": lo,
                    "max": hi,
                    "element": element,
                    "children": [],
                }
            if kind == "any":
                check(
                    node,
                    {"id", "minOccurs", "maxOccurs", "namespace", "processContents"},
                )
                process = node.get("processContents", "strict")
                if process not in ("strict", "lax", "skip"):
                    raise SchemaError("invalid processContents %r" % process)
                namespace = wildcard_constraint(node)
                return {
                    "kind": "any",
                    "min": lo,
                    "max": hi,
                    "children": [],
                    "namespace": namespace,
                    "target": contexts.get(id(node), context)["target"],
                    "process": process,
                }
            if kind not in ("sequence", "choice", "all"):
                if kind in {
                    "attribute",
                    "attributeGroup",
                    "anyAttribute",
                    "simpleType",
                    "complexType",
                }:
                    raise InvalidSchemaError("xs:%s is not legal in a content particle" % kind)
                raise SchemaError("unsupported content particle xs:%s" % kind)
            check(node, {"id", "minOccurs", "maxOccurs"})
            if kind == "all":
                if not top_level or parent_kind is not None:
                    raise SchemaError("xs:all is permitted only as the top-level content model")
                if lo not in (0, 1) or hi != 1:
                    raise SchemaError("xs:all occurrences must be 0..1 or 1..1")
                if any(lname(child.tag) != "element" for child in node):
                    raise SchemaError("xs:all accepts only element particles")
                for child in node:
                    child_lo, child_hi = occurs(child)
                    if child_lo not in (0, 1) or child_hi not in (0, 1):
                        raise SchemaError("an xs:all element must have occurrences 0..1 or 1..1")
            children = [parse_particle(child, kind, False) for child in node]
            return {"kind": kind, "min": lo, "max": hi, "children": children}

        def leaves(particle):
            if not particle:
                return []
            if particle["kind"] == "element":
                return [particle["element"]]
            result = []
            for child in particle["children"]:
                result.extend(leaves(child))
            return result

        def particle_empty(particle):
            if particle["min"] == 0:
                return True
            if particle["kind"] in ("element", "any"):
                return False
            values = [particle_empty(child) for child in particle["children"]]
            return any(values) if particle["kind"] == "choice" else all(values)

        def particle_first(particle):
            if particle["max"] == 0:
                return []
            if particle["kind"] == "element":
                element = particle["element"]
                return [("element", element["ns"], element["name"])]
            if particle["kind"] == "any":
                return [("any", particle["namespace"], particle.get("target"))]
            result = []
            for child in particle["children"]:
                result.extend(particle_first(child))
                if particle["kind"] == "sequence" and not particle_empty(child):
                    break
            return result

        def terms_overlap(left, right):
            if left[0] == right[0] == "element":
                return left[1:] == right[1:]
            if left[0] == right[0] == "any":

                def accepts(constraint, target, namespace):
                    tokens = constraint.split()
                    return (
                        "##any" in tokens
                        or "##local" in tokens
                        and namespace is None
                        or "##targetNamespace" in tokens
                        and namespace == target
                        or "##other" in tokens
                        and namespace is not None
                        and namespace != target
                        or namespace in tokens
                    )

                candidates = {None, left[2], right[2], "urn:zxsd:wildcard-other"}
                candidates.update(
                    token
                    for constraint in (left[1], right[1])
                    for token in constraint.split()
                    if not token.startswith("##")
                )
                return any(
                    accepts(left[1], left[2], value) and accepts(right[1], right[2], value)
                    for value in candidates
                )
            wildcard, element = (left, right) if left[0] == "any" else (right, left)
            namespace = element[1]
            tokens = wildcard[1].split()
            return (
                "##any" in tokens
                or ("##local" in tokens and namespace is None)
                or ("##targetNamespace" in tokens and namespace == wildcard[2])
                or ("##other" in tokens and namespace is not None and namespace != wildcard[2])
                or namespace in tokens
            )

        def first_sets_overlap(left, right):
            return any(
                terms_overlap(a, b) for a in particle_first(left) for b in particle_first(right)
            )

        def validate_particle_determinism(particle):
            children = particle.get("children", [])
            for child in children:
                validate_particle_determinism(child)
            if particle["kind"] == "choice":
                for index, child in enumerate(children):
                    if any(first_sets_overlap(child, other) for other in children[index + 1 :]):
                        raise SchemaError("non-deterministic choice content model")
            elif particle["kind"] == "sequence":
                for index, child in enumerate(children[:-1]):
                    following = children[index + 1]
                    if (
                        child["max"] is None or child["max"] > child["min"] or particle_empty(child)
                    ) and first_sets_overlap(child, following):
                        raise SchemaError("non-deterministic sequence content model")
                    if any(
                        terms_overlap(term, candidate)
                        for term in particle_repeatable_tail(child)
                        for candidate in particle_first(following)
                    ):
                        raise SchemaError("non-deterministic nested sequence content model")
            elif particle["kind"] == "all":
                for index, child in enumerate(children):
                    if any(first_sets_overlap(child, other) for other in children[index + 1 :]):
                        raise SchemaError("non-deterministic xs:all content model")

        def particle_repeatable_tail(particle):
            """Return terms that can consume another token at a particle's end."""
            result = []
            if particle["max"] is None or particle["max"] > max(1, particle["min"]):
                result.extend(particle_first(particle))
            if particle["kind"] in ("sequence", "choice", "all"):
                children = particle["children"]
                candidates = children if particle["kind"] != "sequence" else children[-1:]
                for child in candidates:
                    result.extend(particle_repeatable_tail(child))
                if particle["kind"] == "sequence":
                    for child in reversed(children[:-1]):
                        if not particle_empty(child):
                            break
                        result.extend(particle_repeatable_tail(child))
            return result

        def validate_element_consistency(elements):
            declarations = {}
            for element in elements:
                key = (element["ns"], element["name"])
                typ = element["type"]
                signature = (
                    typ["base"],
                    typ.get("simple"),
                    typ.get("complex"),
                )
                if key in declarations and declarations[key] != signature:
                    raise SchemaError(
                        "element declarations with the same name must have consistent types"
                    )
                declarations[key] = signature

        def particle_has_matching_wildcard(particle, element):
            if particle is None:
                return False
            if particle["kind"] == "any":
                return terms_overlap(
                    ("any", particle["namespace"], particle.get("target")),
                    ("element", element["ns"], element["name"]),
                )
            return any(
                particle_has_matching_wildcard(child, element)
                for child in particle.get("children", [])
            )

        def particle_occurrences_narrow(derived, base):
            derived_max = float("inf") if derived["max"] is None else derived["max"]
            base_max = float("inf") if base["max"] is None else base["max"]
            if derived_max == 0:
                return True
            return derived["min"] >= base["min"] and derived_max <= base_max

        def particle_restriction_is_obviously_invalid(derived, base):
            if derived["max"] == 0:
                return False
            if derived["kind"] != base["kind"]:
                return False
            if not particle_occurrences_narrow(derived, base):
                return True
            if derived["kind"] == "element":
                if (derived["element"]["ns"], derived["element"]["name"]) != (
                    base["element"]["ns"],
                    base["element"]["name"],
                ):
                    return False
                return False
            if derived["kind"] == "any":
                process_rank = {"skip": 0, "lax": 1, "strict": 2}
                if process_rank[derived["process"]] < process_rank[base["process"]]:
                    return True
                base_tokens = base["namespace"].split()
                derived_tokens = derived["namespace"].split()
                if "##any" in derived_tokens and "##any" not in base_tokens:
                    return True
                if "##other" in derived_tokens and not (
                    "##any" in base_tokens or "##other" in base_tokens
                ):
                    return True
                return False
            if len(derived["children"]) != len(base["children"]):
                return False
            for left, right in zip(derived["children"], base["children"]):
                if left["kind"] != right["kind"]:
                    return False
                if left["kind"] == "element" and (
                    left["element"]["ns"],
                    left["element"]["name"],
                ) != (right["element"]["ns"], right["element"]["name"]):
                    return False
            return any(
                particle_restriction_is_obviously_invalid(child, base["children"][index])
                for index, child in enumerate(derived["children"])
            )

        def particle_cardinality(particle):
            """Return the minimum and maximum number of element terms in a particle."""
            if particle["kind"] in ("element", "any"):
                one_min = one_max = 1
            else:
                children = particle["children"]
                if particle["kind"] == "choice":
                    # A particle whose maximum is zero is absent from its
                    # containing model group; it is not an empty alternative.
                    children = [child for child in children if child["max"] != 0]
                values = [particle_cardinality(child) for child in children]
                if particle["kind"] == "choice":
                    one_min = min((value[0] for value in values), default=0)
                    finite = [value[1] for value in values if value[1] is not None]
                    one_max = None if len(finite) != len(values) else max(finite, default=0)
                else:
                    one_min = sum(value[0] for value in values)
                    one_max = (
                        None
                        if any(value[1] is None for value in values)
                        else sum(value[1] for value in values)
                    )
            minimum = one_min * particle["min"]
            maximum = (
                None if particle["max"] is None or one_max is None else one_max * particle["max"]
            )
            return minimum, maximum

        def wildcard_accepts_term(wildcard, term):
            namespace = term[0]
            tokens = wildcard["namespace"].split()
            return (
                "##any" in tokens
                or "##local" in tokens
                and namespace is None
                or "##targetNamespace" in tokens
                and namespace == wildcard.get("target")
                or "##other" in tokens
                and namespace is not None
                and namespace != wildcard.get("target")
                or namespace in tokens
            )

        def particle_alphabet(*particles):
            """Build representatives for every wildcard membership partition."""
            namespaces = {None, context["target"], "urn:zxsd:wildcard-other"}
            names = {"__zxsd_other"}

            def collect(particle):
                if particle["kind"] == "element":
                    namespaces.add(particle["element"]["ns"])
                    names.add(particle["element"]["name"])
                elif particle["kind"] == "any":
                    namespaces.add(particle.get("target"))
                    for token in particle["namespace"].split():
                        if not token.startswith("##"):
                            namespaces.add(token)
                else:
                    for child in particle["children"]:
                        collect(child)

            for particle in particles:
                collect(particle)
            return {(namespace, name) for namespace in namespaces for name in names}

        def particle_words(particle, alphabet, length_limit=8, word_limit=8192):
            """Enumerate a bounded complete prefix of a particle language."""
            if particle["kind"] == "element":
                element = particle["element"]
                accepted = {(element["ns"], element["name"])}
                if not method_blocked(element.get("block", 0), "ZXSD_DERIVATION_SUBSTITUTION"):
                    target = (element["name"], element["ns"])
                    for declaration in globals_.values():
                        current = declaration.get("substitution_head")
                        seen = set()
                        while current is not None and current not in seen:
                            if current == target:
                                accepted.add((declaration["ns"], declaration["name"]))
                                break
                            seen.add(current)
                            parent = globals_.get(schema_key(current[1], current[0]))
                            current = (
                                parent.get("substitution_head") if parent is not None else None
                            )
                one = {(name,) for name in accepted}
            elif particle["kind"] == "any":
                one = {(term,) for term in alphabet if wildcard_accepts_term(particle, term)}
            else:
                children = particle["children"]
                if particle["kind"] == "choice":
                    children = [child for child in children if child["max"] != 0]
                child_words = [
                    particle_words(child, alphabet, length_limit, word_limit) for child in children
                ]
                if any(words is None for words in child_words):
                    return None
                if particle["kind"] == "choice":
                    one = set().union(*child_words) if child_words else {()}
                elif particle["kind"] == "all":
                    one = set()
                    for order in itertools.permutations(range(len(child_words))):
                        words = {()}
                        for index in order:
                            words = {
                                left + right
                                for left in words
                                for right in child_words[index]
                                if len(left) + len(right) <= length_limit
                            }
                            if len(words) > word_limit:
                                return None
                        one.update(words)
                else:
                    one = {()}
                    for values in child_words:
                        one = {
                            left + right
                            for left in one
                            for right in values
                            if len(left) + len(right) <= length_limit
                        }
                        if len(one) > word_limit:
                            return None
            result = set()
            current = {()}
            maximum = particle["max"]
            repeat_limit = length_limit + 1 if maximum is None else min(maximum, length_limit + 1)
            for count in range(repeat_limit + 1):
                if count >= particle["min"]:
                    result.update(current)
                if count == repeat_limit:
                    break
                current = {
                    left + right
                    for left in current
                    for right in one
                    if len(left) + len(right) <= length_limit
                }
                if len(current) > word_limit or len(result) > word_limit:
                    return None
                if not current:
                    break
            return result

        def particle_restriction_is_invalid(derived, base):
            """Check occurrence, term, and ordering inclusion for a restriction."""
            derived_min, derived_max = particle_cardinality(derived)
            base_min, base_max = particle_cardinality(base)
            if derived_min < base_min or (
                base_max is not None and (derived_max is None or derived_max > base_max)
            ):
                return True
            alphabet = particle_alphabet(derived, base)
            base_terms = []

            def collect_terms(particle, result):
                if particle["max"] == 0:
                    return
                if particle["kind"] in ("element", "any"):
                    result.append(particle)
                else:
                    for child in particle["children"]:
                        collect_terms(child, result)

            collect_terms(base, base_terms)
            derived_terms = []
            collect_terms(derived, derived_terms)

            def substitutes_term(element, candidate):
                target = (candidate["element"]["ns"], candidate["element"]["name"])
                current = element.get("substitution_head")
                seen = set()
                while current is not None and current not in seen:
                    if (current[1], current[0]) == target:
                        return not method_blocked(
                            candidate["element"].get("block", 0),
                            "ZXSD_DERIVATION_SUBSTITUTION",
                        )
                    seen.add(current)
                    declaration = globals_.get(schema_key(current[1], current[0]))
                    current = (
                        declaration.get("substitution_head") if declaration is not None else None
                    )
                return False

            for term in derived_terms:
                if term["kind"] == "element":
                    key = (term["element"]["ns"], term["element"]["name"])
                    if not any(
                        candidate["kind"] == "element"
                        and (
                            (candidate["element"]["ns"], candidate["element"]["name"]) == key
                            or substitutes_term(term["element"], candidate)
                        )
                        or candidate["kind"] == "any"
                        and wildcard_accepts_term(candidate, key)
                        for candidate in base_terms
                    ):
                        return True
                elif not any(
                    candidate["kind"] == "any"
                    and all(
                        not wildcard_accepts_term(term, symbol)
                        or wildcard_accepts_term(candidate, symbol)
                        for symbol in alphabet
                    )
                    for candidate in base_terms
                ):
                    return True

            builtin_parents = {
                "ZXSD_T_INTEGER": "ZXSD_T_DECIMAL",
                "ZXSD_T_LONG": "ZXSD_T_INTEGER",
                "ZXSD_T_INT": "ZXSD_T_LONG",
                "ZXSD_T_SHORT": "ZXSD_T_INT",
                "ZXSD_T_BYTE": "ZXSD_T_SHORT",
                "ZXSD_T_NON_POSITIVE_INTEGER": "ZXSD_T_INTEGER",
                "ZXSD_T_NEGATIVE_INTEGER": "ZXSD_T_NON_POSITIVE_INTEGER",
                "ZXSD_T_NON_NEGATIVE_INTEGER": "ZXSD_T_INTEGER",
                "ZXSD_T_POSITIVE_INTEGER": "ZXSD_T_NON_NEGATIVE_INTEGER",
                "ZXSD_T_UNSIGNED_LONG": "ZXSD_T_NON_NEGATIVE_INTEGER",
                "ZXSD_T_UNSIGNED_INT": "ZXSD_T_UNSIGNED_LONG",
                "ZXSD_T_UNSIGNED_SHORT": "ZXSD_T_UNSIGNED_INT",
                "ZXSD_T_UNSIGNED_BYTE": "ZXSD_T_UNSIGNED_SHORT",
                "ZXSD_T_NORMALIZED_STRING": "ZXSD_T_STRING",
                "ZXSD_T_TOKEN": "ZXSD_T_NORMALIZED_STRING",
                "ZXSD_T_LANGUAGE": "ZXSD_T_TOKEN",
                "ZXSD_T_NAME": "ZXSD_T_TOKEN",
                "ZXSD_T_NMTOKEN": "ZXSD_T_TOKEN",
                "ZXSD_T_NCNAME": "ZXSD_T_NAME",
                "ZXSD_T_ID": "ZXSD_T_NCNAME",
                "ZXSD_T_IDREF": "ZXSD_T_NCNAME",
                "ZXSD_T_ENTITY": "ZXSD_T_NCNAME",
            }

            def builtin_restricts(left, right):
                while left != right and left in builtin_parents:
                    left = builtin_parents[left]
                return left == right

            def type_restricts(left, right):
                for key in (right.get("complex"), left.get("complex")):
                    if key in type_nodes and key not in types and key not in visiting:
                        parse_named(key)
                if not left.get("complex") and not right.get("complex"):
                    return simple_type_restricts(left, right)
                if right.get("complex"):
                    base_definition = types.get(right["complex"])
                    if base_definition is None:
                        return left.get("complex") == right["complex"]
                    if base_definition.get("any_type"):
                        return True
                    current = left.get("complex")
                    seen = set()
                    while current is not None and current not in seen:
                        if current == right["complex"]:
                            return True
                        seen.add(current)
                        definition = types.get(current)
                        if (
                            definition is None
                            or definition.get("derivation") != "ZXSD_DERIVATION_RESTRICTION"
                        ):
                            return False
                        current = definition.get("base_ref")
                    return False
                if left.get("complex"):
                    return False
                if right.get("simple"):
                    current = left.get("simple")
                    seen = set()
                    while current is not None and current not in seen:
                        if current == right["simple"]:
                            return True
                        seen.add(current)
                        definition = types.get(current)
                        if (
                            definition is None
                            or definition.get("derivation") != "ZXSD_DERIVATION_RESTRICTION"
                        ):
                            return False
                        current = definition.get("base_ref")
                    return False
                if left.get("simple"):
                    current = left["simple"]
                    seen = set()
                    while current is not None and current not in seen:
                        seen.add(current)
                        definition = types.get(current)
                        if definition is None:
                            return False
                        if not builtin_restricts(definition["base"], right["base"]):
                            return False
                        if definition.get("base_ref") is None:
                            return True
                        if definition.get("derivation") != "ZXSD_DERIVATION_RESTRICTION":
                            return False
                        current = definition.get("base_ref")
                    return False
                return builtin_restricts(left["base"], right["base"])

            def element_restriction_invalid(left, right):
                if left.get("nillable") and not right.get("nillable"):
                    return True

                # The XSD 1.0 particle-restriction rule treats the special
                # value #all distinctly: an explicit list does not preserve
                # a base element's #all constraint, even when it names the
                # three methods currently defined by the recommendation.
                if (
                    right.get("block") == "ZXSD_DERIVATION_ALL"
                    and left.get("block") != "ZXSD_DERIVATION_ALL"
                ):
                    return True

                def blocked_methods(value):
                    if value == "ZXSD_DERIVATION_ALL":
                        return {
                            "ZXSD_DERIVATION_EXTENSION",
                            "ZXSD_DERIVATION_RESTRICTION",
                            "ZXSD_DERIVATION_SUBSTITUTION",
                        }
                    return set(str(value).split(" | ")) - {"", "0"}

                left_block = blocked_methods(left.get("block", ""))
                right_block = blocked_methods(right.get("block", ""))
                if not right_block <= left_block:
                    return True
                if right.get("fixed") is not None:
                    if left.get("fixed") is None:
                        return True
                    if comparable_value(right["type"], left["fixed"]) != comparable_value(
                        right["type"], right["fixed"]
                    ):
                        return True
                return not type_restricts(left["type"], right["type"])

            def element_matches(left, right):
                target = (right["name"], right["ns"])
                if (left["name"], left["ns"]) == target:
                    return True
                if method_blocked(right.get("block", 0), "ZXSD_DERIVATION_SUBSTITUTION"):
                    return False
                current = left.get("substitution_head")
                seen = set()
                while current is not None and current not in seen:
                    if current == target:
                        return True
                    seen.add(current)
                    declaration = globals_.get(schema_key(current[1], current[0]))
                    current = (
                        declaration.get("substitution_head") if declaration is not None else None
                    )
                return False

            def structurally_forbidden(left, right):
                if left["max"] == 0:
                    return False

                def repeated_child(child, parent):
                    result = dict(child)
                    result["min"] *= parent["min"]
                    result["max"] = (
                        None
                        if result["max"] is None or parent["max"] is None
                        else result["max"] * parent["max"]
                    )
                    return result

                def pointless_child(particle):
                    if (
                        particle["kind"] not in ("sequence", "choice", "all")
                        or len(particle["children"]) != 1
                        or particle["min"] != 1
                        or particle["max"] != 1
                    ):
                        return None
                    return repeated_child(particle["children"][0], particle)

                left_child = pointless_child(left)
                if left_child is not None:
                    return structurally_forbidden(left_child, right)
                right_child = pointless_child(right)
                if right_child is not None:
                    return structurally_forbidden(left, right_child)
                if right["kind"] == "choice":
                    if left["kind"] == "choice":
                        position = 0
                        for value in left["children"]:
                            while position < len(right["children"]) and structurally_forbidden(
                                value, right["children"][position]
                            ):
                                position += 1
                            if position == len(right["children"]):
                                return True
                            position += 1
                        return False
                    if left["kind"] == "sequence":
                        # RecurseLax maps each member into a branch.  The
                        # enclosing occurrence range is checked once by the
                        # cardinality/language tests, not once per member.
                        candidates = right["children"]
                        values = left["children"]
                    else:
                        candidates = [repeated_child(child, right) for child in right["children"]]
                        values = [left]
                    return any(
                        not any(
                            not structurally_forbidden(value, candidate) for candidate in candidates
                        )
                        for value in values
                    )
                if right["kind"] == "element":
                    return (
                        left["kind"] != "element"
                        or not element_matches(left["element"], right["element"])
                        or not particle_occurrences_narrow(left, right)
                        or element_restriction_invalid(left["element"], right["element"])
                    )
                if right["kind"] == "any":
                    if left["kind"] == "element":
                        return not wildcard_accepts_term(
                            right, (left["element"]["ns"], left["element"]["name"])
                        )
                    if left["kind"] == "any":
                        return any(
                            wildcard_accepts_term(left, symbol)
                            and not wildcard_accepts_term(right, symbol)
                            for symbol in alphabet
                        )
                    return False
                if right["kind"] in ("sequence", "all") and left["kind"] in (
                    "sequence",
                    "all",
                ):
                    unused = list(right["children"])
                    position = 0
                    # XSD 1.0 maps all:all with the order-preserving Recurse
                    # rule; only sequence:all uses RecurseUnordered.
                    ordered = right["kind"] == "sequence" or left["kind"] == "all"
                    for value in left["children"]:
                        matched = None
                        candidates = range(position, len(unused)) if ordered else range(len(unused))
                        for candidate_index in candidates:
                            candidate = unused[candidate_index]
                            if not structurally_forbidden(value, candidate):
                                matched = candidate_index
                                break
                            if ordered and not particle_empty(candidate):
                                break
                        if matched is None:
                            return True
                        if ordered:
                            if any(not particle_empty(item) for item in unused[position:matched]):
                                return True
                            position = matched + 1
                        else:
                            unused.pop(matched)
                    remaining = unused[position:] if ordered else unused
                    return any(not particle_empty(item) for item in remaining)
                if right["kind"] in ("sequence", "all"):
                    for index, candidate in enumerate(right["children"]):
                        if not structurally_forbidden(left, candidate) and all(
                            position == index or particle_empty(item)
                            for position, item in enumerate(right["children"])
                        ):
                            return False
                    return True
                return left["kind"] != right["kind"]

            derived_words = particle_words(derived, alphabet)
            base_words = particle_words(base, alphabet)
            if (
                derived_words is not None
                and base_words is not None
                and not derived_words <= base_words
            ):
                return True
            if not schema_for_schemas and structurally_forbidden(derived, base):
                return True
            for element in leaves(derived):
                key = (element["ns"], element["name"])
                candidates = [
                    candidate
                    for candidate in leaves(base)
                    if (candidate["ns"], candidate["name"]) == key
                ]
                if (
                    candidates
                    and element.get("nillable")
                    and not any(candidate.get("nillable") for candidate in candidates)
                ):
                    return True
                if candidates and all(
                    element_restriction_invalid(element, candidate) for candidate in candidates
                ):
                    return True
            return particle_restriction_is_obviously_invalid(derived, base)

        def parse_any_attribute(node):
            """Compile an attribute wildcard shared by groups and complex types."""
            check(node, {"id", "namespace", "processContents"})
            if list(node):
                raise InvalidSchemaError("xs:anyAttribute cannot contain child components")
            process = node.get("processContents", "strict")
            if process not in ("strict", "lax", "skip"):
                raise SchemaError("invalid processContents %r" % process)
            return {
                "namespace": wildcard_constraint(node),
                "process": process,
                "target": contexts.get(id(node), context)["target"],
            }

        def combine_attribute_wildcards(base, derived):
            """Compute the wildcard union required by complex type extension."""
            if base is None:
                return derived
            if derived is None:
                return base
            rank = {"skip": 0, "lax": 1, "strict": 2}
            process = min((base["process"], derived["process"]), key=rank.get)

            def shape(wildcard):
                tokens = wildcard["namespace"].split()
                if "##any" in tokens:
                    return "any", None
                if "##other" in tokens:
                    return "other", wildcard.get("target")
                values = set()
                for token in tokens:
                    if token == "##local":
                        values.add(None)
                    elif token == "##targetNamespace":
                        values.add(wildcard.get("target"))
                    else:
                        values.add(token)
                return "finite", values

            def result(kind, values=None, target=None):
                if kind == "any":
                    namespace = "##any"
                elif kind == "other":
                    namespace = "##other"
                else:
                    namespace = " ".join(
                        "##local" if value is None else value
                        for value in sorted(values, key=lambda value: value or "")
                    )
                return {"namespace": namespace, "process": process, "target": target}

            left_kind, left_value = shape(base)
            right_kind, right_value = shape(derived)
            if "any" in (left_kind, right_kind):
                return result("any", target=context["target"])
            if left_kind == right_kind == "finite":
                return result("finite", left_value | right_value, target=context["target"])
            if left_kind == right_kind == "other":
                return result("other", target=left_value if left_value == right_value else None)
            other_target, finite = (
                (left_value, right_value) if left_kind == "other" else (right_value, left_value)
            )
            adds_local = None in finite
            adds_target = other_target in finite
            if adds_local and adds_target:
                return result("any", target=context["target"])
            if adds_target:
                return result("other", target=None)
            if adds_local:
                raise InvalidSchemaError("attribute wildcard union is not expressible")
            return result("other", target=other_target)

        def intersect_attribute_wildcards(left, right):
            """Compute the wildcard intersection contributed by attribute groups."""
            if left is None:
                return right
            if right is None:
                return left
            rank = {"skip": 0, "lax": 1, "strict": 2}
            process = min((left["process"], right["process"]), key=rank.get)

            def accepted_values(wildcard):
                tokens = wildcard["namespace"].split()
                if "##any" in tokens:
                    return "any", None
                if "##other" in tokens:
                    return "other", wildcard.get("target")
                values = {
                    (
                        None
                        if token == "##local"
                        else (wildcard.get("target") if token == "##targetNamespace" else token)
                    )
                    for token in tokens
                }
                return "finite", values

            def finite_result(values):
                namespace = " ".join(
                    "##local" if value is None else value
                    for value in sorted(values, key=lambda value: value or "")
                )
                return {
                    "namespace": namespace,
                    "process": process,
                    "target": context["target"],
                }

            left_kind, left_value = accepted_values(left)
            right_kind, right_value = accepted_values(right)
            if left_kind == "any":
                return {**right, "process": process}
            if right_kind == "any":
                return {**left, "process": process}
            if left_kind == right_kind == "finite":
                return finite_result(left_value & right_value)
            if left_kind == right_kind == "other":
                if left_value != right_value and None not in (left_value, right_value):
                    raise InvalidSchemaError("attribute wildcard intersection is not expressible")
                target = right_value if left_value is None else left_value
                return {"namespace": "##other", "process": process, "target": target}
            other_target, finite = (
                (left_value, right_value) if left_kind == "other" else (right_value, left_value)
            )
            return finite_result(finite - {None, other_target})

        def attribute_wildcard_accepts(wildcard, namespace):
            """Evaluate an attribute wildcard against an expanded namespace name."""
            if wildcard is None:
                return False
            tokens = wildcard["namespace"].split()
            if "##any" in tokens:
                return True
            if "##other" in tokens:
                return namespace not in (None, wildcard.get("target"))
            return (
                namespace is None
                and "##local" in tokens
                or namespace == wildcard.get("target")
                and "##targetNamespace" in tokens
                or namespace in tokens
            )

        def attribute_wildcard_is_subset(derived, base):
            """Check the namespace/processContents restriction rules for anyAttribute."""
            if derived is None:
                return True
            if base is None:
                return False
            rank = {"skip": 0, "lax": 1, "strict": 2}
            if rank[derived["process"]] < rank[base["process"]]:
                return False
            derived_tokens = derived["namespace"].split()
            if "##any" in derived_tokens:
                return "##any" in base["namespace"].split()
            if "##other" in derived_tokens:
                base_tokens = base["namespace"].split()
                return "##any" in base_tokens or "##other" in base_tokens
            namespaces = []
            for token in derived_tokens:
                if token == "##local":
                    namespaces.append(None)
                elif token == "##targetNamespace":
                    namespaces.append(derived.get("target"))
                else:
                    namespaces.append(token)
            return all(attribute_wildcard_accepts(base, namespace) for namespace in namespaces)

        def parse_attribute_group(node):
            """Expand one global attribute-group reference."""
            check(node, {"ref", "id"})
            if not node.get("ref") or list(node):
                raise SchemaError("local xs:attributeGroup requires ref and no children")
            uri, group_name = qname(node.get("ref"), "attributeGroup", node)
            key = schema_key(uri, group_name)
            return expand_attribute_group(key, group_name)

        def expand_attribute_group(key, group_name):
            """Validate and expand a global attribute-group definition."""
            if key in attribute_groups:
                cached_attributes, cached_wildcard = attribute_groups[key]
                return list(cached_attributes), cached_wildcard
            if key not in attribute_group_nodes:
                raise SchemaError("invalid attributeGroup ref %r" % group_name)
            if key in attribute_group_visiting:
                raise SchemaError("circular attributeGroup reference involving %r" % group_name)
            attribute_group_visiting.add(key)
            attributes = []
            wildcard = None
            for child in attribute_group_nodes[key]:
                child_kind = lname(child.tag)
                if child_kind == "attribute":
                    attributes.append(parse_attribute(child, name + "_" + group_name))
                elif child_kind == "attributeGroup":
                    nested_attributes, nested_wildcard = parse_attribute_group(child)
                    attributes.extend(nested_attributes)
                    if nested_wildcard is not None:
                        wildcard = intersect_attribute_wildcards(wildcard, nested_wildcard)
                elif child_kind == "anyAttribute":
                    wildcard = intersect_attribute_wildcards(wildcard, parse_any_attribute(child))
                else:
                    raise SchemaError(
                        "unsupported XSD construct 'xs:%s' in attributeGroup" % child_kind
                    )
            if len(attributes) > max_schema_complexity:
                raise SchemaError("too many attributes in attributeGroup %r" % group_name)
            keys = [(attribute["name"], attribute["ns"]) for attribute in attributes]
            if len(set(keys)) != len(keys):
                raise SchemaError("duplicate attribute in attributeGroup %r" % group_name)
            attribute_group_visiting.remove(key)
            attribute_groups[key] = (list(attributes), wildcard)
            return attributes, wildcard

        def body(
            children,
            base_elements=None,
            base_attributes=None,
            base_particle=None,
            allow_attribute_override=False,
        ):
            """Compile the body shared by direct and derived complex types."""
            model = "ZXSD_SEQUENCE"
            group_min = 0
            group_max = 1
            elements = list(base_elements or [])
            attributes = list(base_attributes or [])
            got = False
            particle = base_particle
            any_attribute = None
            attribute_phase = False
            wildcard_seen = False
            for c in children:
                k = lname(c.tag)
                if k in ("sequence", "choice", "all", "group"):
                    if attribute_phase:
                        raise InvalidSchemaError(
                            "a complex type content model must precede its attributes"
                        )
                    if got:
                        raise SchemaError("complex type may contain only one content model")
                    got = True
                    local_particle = parse_particle(c, None, True)
                    group_min, group_max = local_particle["min"], local_particle["max"]
                    model = {
                        "sequence": "ZXSD_SEQUENCE",
                        "choice": "ZXSD_CHOICE",
                        "all": "ZXSD_ALL",
                    }[local_particle["kind"]]
                    if particle and particle["kind"] == "all":
                        if particle["children"]:
                            raise SchemaError(
                                "non-empty xs:all content cannot be extended by another particle"
                            )
                        particle = local_particle
                        elements.extend(leaves(local_particle))
                        continue
                    if particle:
                        particle = {
                            "kind": "sequence",
                            "min": 1,
                            "max": 1,
                            "children": [particle, local_particle],
                        }
                    else:
                        particle = local_particle
                    elements.extend(leaves(local_particle))
                    if len(elements) > max_schema_complexity:
                        raise SchemaError("too many elements in complex type %r" % name)
                elif k == "attribute":
                    if wildcard_seen:
                        raise InvalidSchemaError("xs:anyAttribute must be last")
                    attribute_phase = True
                    attribute = parse_attribute(c, name)
                    key = (attribute["name"], attribute["ns"])
                    if not allow_attribute_override and any(
                        (x["name"], x["ns"]) == key for x in attributes
                    ):
                        raise InvalidSchemaError("duplicate attribute in complex type")
                    attributes = [x for x in attributes if (x["name"], x["ns"]) != key]
                    if not attribute["prohibited"]:
                        attributes.append(attribute)
                elif k == "attributeGroup":
                    if wildcard_seen:
                        raise InvalidSchemaError("xs:anyAttribute must be last")
                    attribute_phase = True
                    group_attributes, group_wildcard = parse_attribute_group(c)
                    for attribute in group_attributes:
                        key = (attribute["name"], attribute["ns"])
                        if not allow_attribute_override and any(
                            (x["name"], x["ns"]) == key for x in attributes
                        ):
                            raise InvalidSchemaError("duplicate attribute in complex type")
                        attributes = [x for x in attributes if (x["name"], x["ns"]) != key]
                        if not attribute["prohibited"]:
                            attributes.append(attribute)
                    if group_wildcard is not None:
                        any_attribute = intersect_attribute_wildcards(any_attribute, group_wildcard)
                elif k == "anyAttribute":
                    if wildcard_seen:
                        raise InvalidSchemaError(
                            "a complex type may contain only one xs:anyAttribute"
                        )
                    attribute_phase = True
                    any_attribute = intersect_attribute_wildcards(
                        any_attribute, parse_any_attribute(c)
                    )
                    wildcard_seen = True
                else:
                    raise SchemaError("unsupported XSD construct 'xs:%s'" % k)
            if len(attributes) > max_schema_complexity:
                raise SchemaError("too many attributes in complex type %r" % name)
            if len({(x["name"], x["ns"]) for x in attributes}) != len(attributes):
                raise SchemaError("duplicate attribute in %r" % name)
            if sum(attribute["type"]["base"] == "ZXSD_T_ID" for attribute in attributes) > 1:
                raise InvalidSchemaError("a complex type cannot contain more than one ID attribute")
            if particle is not None:
                validate_particle_determinism(particle)
            validate_element_consistency(elements)
            return (
                model,
                group_min,
                group_max,
                elements,
                attributes,
                particle,
                any_attribute,
            )

        children = list(n)
        if len(children) == 1 and lname(children[0].tag) in (
            "complexContent",
            "simpleContent",
        ):
            content = children[0]
            ck = lname(content.tag)
            check(content, {"id", "mixed"})
            if ck == "simpleContent" and (declared_mixed or content.get("mixed") is not None):
                raise SchemaError("simpleContent cannot be mixed")
            derivations = list(content)
            if len(derivations) != 1 or lname(derivations[0].tag) not in (
                "extension",
                "restriction",
            ):
                raise SchemaError("xs:%s requires one extension or restriction" % ck)
            deriv = derivations[0]
            dk = lname(deriv.tag)
            check(deriv, {"base", "id"})
            base = resolve_type(deriv.get("base"), deriv)
            deferred_recursive_base = False
            if base.get("complex") and base["complex"] not in types:
                if (
                    base["complex"] in visiting
                    and name not in type_nodes
                    and anonymous_recursive_optional.get(name, False)
                ):
                    deferred_recursive_base = True
                else:
                    raise SchemaError("circular type derivation involving %r" % base["complex"])
            if ck == "complexContent":
                if not base["complex"]:
                    raise SchemaError("complexContent base must be complex")
                bt = (
                    {
                        "model": "ZXSD_SEQUENCE",
                        "group_min": 0,
                        "group_max": 1,
                        "elements": [],
                        "attributes": [],
                        "particle": None,
                        "simple_content": None,
                        "mixed": False,
                        "any_type": False,
                        "final": 0,
                        "any_attribute": None,
                    }
                    if deferred_recursive_base
                    else types[base["complex"]]
                )
                content_mixed = content.get("mixed")
                if content_mixed is not None and content_mixed not in (
                    "true",
                    "false",
                    "1",
                    "0",
                ):
                    raise SchemaError("invalid complexContent mixed value")
                if content_mixed is not None and n.get("mixed") is not None:
                    if (content_mixed in ("true", "1")) != declared_mixed:
                        raise SchemaError("complexType and complexContent mixed values differ")
                effective_mixed = (
                    content_mixed in ("true", "1") if content_mixed is not None else declared_mixed
                )
                if effective_mixed and not bt.get("mixed", False):
                    raise InvalidSchemaError(
                        "complexContent derivation cannot add mixed character content"
                    )
                if (
                    dk == "extension"
                    and any(
                        lname(child.tag) in ("sequence", "choice", "all", "group")
                        for child in deriv
                    )
                    and bt.get("mixed", False)
                    and not bt.get("any_type", False)
                    and not effective_mixed
                ):
                    raise InvalidSchemaError(
                        "complexContent extension %r must preserve mixed content from %r"
                        % (type_name, bt.get("type_name"))
                    )
                derivation_flag = (
                    "ZXSD_DERIVATION_EXTENSION"
                    if dk == "extension"
                    else "ZXSD_DERIVATION_RESTRICTION"
                )
                if method_blocked(bt.get("final", 0), derivation_flag):
                    raise SchemaError("base type final blocks %s" % dk)
                if bt.get("simple_content"):
                    if dk != "extension":
                        raise SchemaError(
                            "complexContent restriction cannot derive from simpleContent"
                        )
                    model, gmin, gmax, elements, attributes, particle, any_attribute = body(
                        list(deriv), [], bt["attributes"], None
                    )
                    if particle is not None and not particle_empty(particle):
                        raise InvalidSchemaError(
                            "complexContent extension of simpleContent cannot add element content"
                        )
                    types[name] = {
                        "kind": "complex",
                        "model": model,
                        "group_min": gmin,
                        "group_max": gmax,
                        "elements": elements,
                        "attributes": attributes,
                        "simple_content": bt["simple_content"],
                        "abstract": abstract in ("true", "1"),
                        "particle": None,
                        "any_attribute": combine_attribute_wildcards(
                            bt.get("any_attribute"), any_attribute
                        ),
                        "base_ref": base["complex"],
                        "derivation": "ZXSD_DERIVATION_EXTENSION",
                        "final": final,
                        "block": block,
                        "type_name": type_name,
                        "type_ns": type_namespace,
                        "mixed": False,
                        "any_type": False,
                    }
                    return
                inherited_elements = bt["elements"] if dk == "extension" else []
                inherited_attributes = bt["attributes"]
                inherited_particle = bt.get("particle") if dk == "extension" else None
                model, gmin, gmax, elements, attributes, particle, any_attribute = body(
                    list(deriv),
                    inherited_elements,
                    inherited_attributes,
                    inherited_particle,
                    dk == "restriction",
                )
                if dk == "extension" and not any(
                    lname(x.tag) in ("sequence", "choice", "all", "group") for x in deriv
                ):
                    model, gmin, gmax = bt["model"], bt["group_min"], bt["group_max"]
                if (
                    dk == "extension"
                    and bt.get("particle") is not None
                    and not particle_empty(bt["particle"])
                    and particle is not None
                    and particle["kind"] == "sequence"
                    and particle["children"]
                    and particle["children"][-1]["kind"] == "all"
                ):
                    raise InvalidSchemaError("xs:all cannot extend a non-empty content model")
                if dk == "restriction":
                    restricted_attributes = {
                        (attribute["name"], attribute["ns"]): attribute for attribute in attributes
                    }
                    for base_attribute in bt["attributes"]:
                        key = (base_attribute["name"], base_attribute["ns"])
                        restricted = restricted_attributes.get(key)
                        if base_attribute["required"] and (
                            restricted is None or not restricted["required"]
                        ):
                            raise InvalidSchemaError(
                                "complex restriction weakens a required attribute"
                            )
                        if (
                            restricted is not None
                            and not restricted.get("prohibited")
                            and not simple_type_restricts(
                                restricted["type"], base_attribute["type"]
                            )
                        ):
                            raise InvalidSchemaError("complex restriction widens an attribute type")
                        if base_attribute.get("fixed") is not None and (
                            restricted is None
                            or restricted.get("fixed") is None
                            or comparable_value(restricted["type"], restricted["fixed"])
                            != comparable_value(base_attribute["type"], base_attribute["fixed"])
                        ):
                            raise InvalidSchemaError(
                                "complex restriction changes a fixed attribute"
                            )
                    base_attribute_names = {
                        (attribute["name"], attribute["ns"]) for attribute in bt["attributes"]
                    }
                    for restricted in attributes:
                        key = (restricted["name"], restricted["ns"])
                        if key not in base_attribute_names and not attribute_wildcard_accepts(
                            bt.get("any_attribute"), restricted["ns"]
                        ):
                            raise InvalidSchemaError("complex restriction introduces an attribute")
                    if not attribute_wildcard_is_subset(any_attribute, bt.get("any_attribute")):
                        raise InvalidSchemaError(
                            "complex restriction widens its attribute wildcard"
                        )
                    allowed_names = {(x["name"], x["ns"]) for x in bt["elements"]}

                    def substitutes_base_element(element):
                        current = element.get("substitution_head")
                        seen = set()
                        while current is not None and current not in seen:
                            if current in allowed_names:
                                return True
                            seen.add(current)
                            declaration = globals_.get(schema_key(current[1], current[0]))
                            current = (
                                declaration.get("substitution_head")
                                if declaration is not None
                                else None
                            )
                        return False

                    if (
                        not deferred_recursive_base
                        and not bt.get("any_type")
                        and any(
                            x["max"] != 0
                            and (x["name"], x["ns"]) not in allowed_names
                            and not particle_has_matching_wildcard(bt.get("particle"), x)
                            and not substitutes_base_element(x)
                            for x in elements
                        )
                    ):
                        raise SchemaError("complex restriction introduces an element")
                    if (
                        not deferred_recursive_base
                        and particle is not None
                        and bt.get("particle") is not None
                        and particle_restriction_is_invalid(particle, bt["particle"])
                    ):
                        raise SchemaError(
                            "complex restriction %r widens the base content model" % name
                        )
                types[name] = {
                    "kind": "complex",
                    "model": model,
                    "group_min": gmin,
                    "group_max": gmax,
                    "elements": elements,
                    "attributes": attributes,
                    "simple_content": None,
                    "abstract": abstract in ("true", "1"),
                    "particle": particle,
                    "any_attribute": (
                        combine_attribute_wildcards(bt.get("any_attribute"), any_attribute)
                        if dk == "extension"
                        else any_attribute
                    ),
                    "base_ref": base["complex"],
                    "derivation": (
                        "ZXSD_DERIVATION_EXTENSION"
                        if dk == "extension"
                        else "ZXSD_DERIVATION_RESTRICTION"
                    ),
                    "final": final,
                    "block": block,
                    "type_name": type_name,
                    "type_ns": type_namespace,
                    "mixed": effective_mixed,
                    "any_type": False,
                }
                return

            if base["complex"]:
                bt = types[base["complex"]]
                if not bt.get("simple_content"):
                    raise SchemaError("simpleContent base must have simple content")
                simple_ref = bt["simple_content"]
                inherited_attributes = bt["attributes"]
            else:
                simple_ref = base
                inherited_attributes = []
            if simple_ref["base"] == "ZXSD_T_NOTATION" and not simple_ref.get("simple"):
                raise SchemaError("xs:NOTATION cannot be used directly")
            deriv_children = list(deriv)
            facet_tags = {
                "enumeration",
                "length",
                "minLength",
                "maxLength",
                "minInclusive",
                "maxInclusive",
                "minExclusive",
                "maxExclusive",
                "pattern",
                "whiteSpace",
                "totalDigits",
                "fractionDigits",
            }
            seen_attribute = False
            seen_any_attribute = False
            for index, child in enumerate(deriv_children):
                kind = lname(child.tag)
                if kind == "simpleType":
                    if dk != "restriction" or index != 0:
                        raise InvalidSchemaError(
                            "nested simpleType must be first in simpleContent restriction"
                        )
                elif kind in facet_tags:
                    if dk != "restriction" or seen_attribute:
                        raise InvalidSchemaError("misplaced simpleContent facet")
                elif kind in ("attribute", "attributeGroup"):
                    if seen_any_attribute:
                        raise InvalidSchemaError("xs:anyAttribute must be last")
                    seen_attribute = True
                elif kind == "anyAttribute":
                    if seen_any_attribute:
                        raise InvalidSchemaError("multiple xs:anyAttribute declarations")
                    seen_attribute = True
                    seen_any_attribute = True
                else:
                    raise InvalidSchemaError("xs:%s is not legal in simpleContent %s" % (kind, dk))
            if dk == "restriction":
                if not base["complex"]:
                    raise InvalidSchemaError(
                        "simpleContent restriction base must be a complex type"
                    )
                facets = [
                    x
                    for x in deriv_children
                    if lname(x.tag) in facet_tags or lname(x.tag) == "simpleType"
                ]
                attrs = [
                    x
                    for x in deriv_children
                    if lname(x.tag) in ("attribute", "attributeGroup", "anyAttribute")
                ]
                synthetic = ET.Element(XS + "simpleType")
                anon[0] += 1
                alias = ident(name) + "__content_base_%d" % anon[0]
                nested_simple = next(
                    (facet for facet in facets if lname(facet.tag) == "simpleType"),
                    None,
                )
                if nested_simple is not None:
                    parse_simple(nested_simple, alias)
                    nested_ref = {
                        "base": types[alias]["base"],
                        "simple": alias,
                        "complex": None,
                        "builtin": None,
                        "list_builtin": False,
                    }
                    if not simple_type_restricts(nested_ref, simple_ref):
                        raise InvalidSchemaError(
                            "simpleContent restriction type does not restrict its base content"
                        )
                    facets = [facet for facet in facets if facet is not nested_simple]
                elif (
                    simple_ref.get("builtin") == "anySimpleType"
                    and facets
                    and not schema_for_schemas
                ):
                    # xs:anySimpleType has no applicable constraining facets.
                    raise InvalidSchemaError("facets cannot restrict xs:anySimpleType content")
                elif simple_ref.get("simple"):
                    types[alias] = dict(types[simple_ref["simple"]])
                else:
                    builtin = simple_ref.get("builtin")
                    types[alias] = {
                        "kind": "simple",
                        "base": simple_ref["base"],
                        "facets": {},
                        "enums": [],
                        "whitespace": (
                            "ZXSD_WS_PRESERVE"
                            if builtin in WHITESPACE_PRESERVE
                            else (
                                "ZXSD_WS_REPLACE"
                                if builtin in WHITESPACE_REPLACE
                                else "ZXSD_WS_COLLAPSE"
                            )
                        ),
                        "variety": "ZXSD_SIMPLE_ATOMIC",
                        "item": None,
                        "members": [],
                    }
                restriction = ET.SubElement(synthetic, XS + "restriction", {"base": alias})
                for facet in facets:
                    restriction.append(facet)
                anon[0] += 1
                restricted_name = ident(name) + "__content_%d" % anon[0]
                parse_simple(synthetic, restricted_name)
                simple_ref = {
                    "base": types[restricted_name]["base"],
                    "simple": restricted_name,
                    "complex": None,
                    "builtin": None,
                    "list_builtin": False,
                }
                deriv_children = attrs
            model, gmin, gmax, elements, attributes, particle, any_attribute = body(
                deriv_children, [], inherited_attributes, None, dk == "restriction"
            )
            if dk == "restriction":
                restricted_attributes = {
                    (attribute["name"], attribute["ns"]): attribute for attribute in attributes
                }
                for base_attribute in inherited_attributes:
                    restricted = restricted_attributes.get(
                        (base_attribute["name"], base_attribute["ns"])
                    )
                    if base_attribute["required"] and (
                        restricted is None or not restricted["required"]
                    ):
                        raise InvalidSchemaError(
                            "simpleContent restriction weakens a required attribute"
                        )
                    if restricted is not None and not simple_type_restricts(
                        restricted["type"], base_attribute["type"]
                    ):
                        raise InvalidSchemaError(
                            "simpleContent restriction widens an attribute type"
                        )
            types[name] = {
                "kind": "complex",
                "model": model,
                "group_min": gmin,
                "group_max": gmax,
                "elements": elements,
                "attributes": attributes,
                "simple_content": simple_ref,
                "abstract": abstract in ("true", "1"),
                "particle": particle,
                "any_attribute": any_attribute,
                "base_ref": base.get("complex"),
                "derivation": (
                    "ZXSD_DERIVATION_EXTENSION"
                    if dk == "extension"
                    else "ZXSD_DERIVATION_RESTRICTION"
                ),
                "final": final,
                "block": block,
                "type_name": type_name,
                "type_ns": type_namespace,
                "mixed": False,
                "any_type": False,
            }
            return

        model, group_min, group_max, elements, attributes, particle, any_attribute = body(children)
        types[name] = {
            "kind": "complex",
            "model": model,
            "group_min": group_min,
            "group_max": group_max,
            "elements": elements,
            "attributes": attributes,
            "simple_content": None,
            "abstract": abstract in ("true", "1"),
            "particle": particle,
            "any_attribute": any_attribute,
            "base_ref": None,
            "derivation": 0,
            "final": final,
            "block": block,
            "type_name": type_name,
            "type_ns": type_namespace,
            "mixed": declared_mixed,
            "any_type": False,
        }

    def parse_named(name):
        """Compile a named type once and reject invalid recursive definitions."""
        if name in types:
            return
        if name in visiting:
            raise SchemaError("recursive definition involving %r" % name)
        visiting.add(name)
        try:
            n = type_nodes[name]
            (parse_simple if lname(n.tag) == "simpleType" else parse_complex)(n, name)
        finally:
            visiting.remove(name)

    def parse_global(name):
        """Compile a global element once and detect recursive references."""
        if name in globals_:
            return globals_[name]
        if name in global_visiting:
            node = global_nodes[name]
            return parse_element(node, "global", True)
        global_visiting.add(name)
        globals_[name] = parse_element(global_nodes[name], "global", True)
        global_visiting.remove(name)
        return globals_[name]

    def parse_xpath(expression, field=False, context_node=None):
        """Compile the restricted XSD 1.0 identity XPath subset."""
        if not expression:
            raise SchemaError("identity XPath must not be empty")
        branches = [branch.strip() for branch in expression.split("|")]
        if not all(branches):
            raise SchemaError("invalid identity XPath union")

        def parse_branch(branch):
            branch = branch.strip()
            branch = re.sub(r"child[\t\r\n ]*::[\t\r\n ]*", "child::", branch)
            branch = re.sub(r"attribute[\t\r\n ]*::[\t\r\n ]*", "attribute::", branch)
            branch = re.sub(r"\.[\t\r\n ]+//", ".//", branch)
            branch = re.sub(r"@[\t\r\n ]+", "@", branch)
            descendant = branch.startswith(".//")
            if descendant:
                branch = branch[3:].strip()
            elif branch.startswith("./"):
                branch = branch[2:]
            attribute = False
            raw = branch.split("/") if branch else []
            if field and raw and raw[-1].startswith("attribute::"):
                attribute = True
                raw[-1] = raw[-1][11:]
            elif field and raw and raw[-1].startswith("@"):
                attribute = True
                raw[-1] = raw[-1][1:]
            dot_only = bool(raw) and all(step == "." for step in raw)
            raw = [step for step in raw if step != "."]
            if not raw and not dot_only:
                raise SchemaError("invalid identity XPath")
            steps = []
            for step in raw:
                if step.startswith("child::"):
                    step = step[7:]
                if not step or step == ".." or step.startswith("@") or "::" in step:
                    raise SchemaError("unsupported identity XPath step %r" % step)
                if step == "*":
                    steps.append({"name": None, "ns": None})
                    continue
                if step.endswith(":*"):
                    prefix = step[:-2]
                    document = contexts[id(context_node)]
                    mapping = document.get("element_nsmaps", {}).get(
                        id(context_node), document["nsmap"]
                    )
                    if not valid_ncname(prefix) or prefix not in mapping:
                        raise SchemaError("invalid identity XPath namespace wildcard %r" % step)
                    steps.append({"name": None, "ns": mapping[prefix]})
                    continue
                uri, local = qname(step, "XPath name", context_node)
                if ":" not in step:
                    uri = None
                if not valid_ncname(local):
                    raise SchemaError("invalid identity XPath name %r" % step)
                steps.append({"name": local, "ns": uri})
            return {"steps": steps, "descendant": descendant, "attribute": attribute}

        parsed = []
        for branch in branches:
            candidate = parse_branch(branch)
            if candidate not in parsed:
                parsed.append(candidate)
        if len(parsed) == 1:
            return parsed[0]
        return {
            "steps": [],
            "descendant": False,
            "attribute": False,
            "alternatives": parsed,
        }

    identity_declaration_names = {}

    def parse_identities(element_node):
        """Compile unique, key, and keyref constraints on an element."""
        constraints = []
        names = {}
        for node in element_node:
            kind = lname(node.tag)
            if kind not in ("unique", "key", "keyref"):
                continue
            check(node, {"name", "id", "refer"} if kind == "keyref" else {"name", "id"})
            name = normalize_token(node.get("name", ""))
            if not valid_ncname(name) or name in names:
                raise SchemaError("identity constraint requires a unique name")
            expanded_name = schema_key(contexts[id(node)]["target"], name)
            previous = identity_declaration_names.get(expanded_name)
            if previous is not None and previous != id(node):
                raise InvalidSchemaError("duplicate identity constraint declaration")
            identity_declaration_names[expanded_name] = id(node)
            if kind == "keyref" and not node.get("refer"):
                raise SchemaError("xs:keyref requires refer")
            children = list(node)
            if not children or lname(children[0].tag) != "selector":
                raise SchemaError("identity constraint requires selector first")
            check(children[0], {"xpath", "id"})
            if list(children[0]):
                raise SchemaError("xs:selector cannot contain child components")
            selector = parse_xpath(children[0].get("xpath"), False, children[0])
            fields = []
            for field in children[1:]:
                if lname(field.tag) != "field":
                    raise SchemaError("identity constraint accepts only field after selector")
                check(field, {"xpath", "id"})
                if list(field):
                    raise SchemaError("xs:field cannot contain child components")
                fields.append(parse_xpath(field.get("xpath"), True, field))
            if not fields:
                raise SchemaError("identity constraint requires at least one field")
            constraint = {
                "name": name,
                "namespace": contexts[id(node)]["target"],
                "kind": kind,
                "scope": {"steps": [], "descendant": False, "attribute": False},
                "selector": selector,
                "fields": fields,
                "refer": node.get("refer"),
                "refer_key": (
                    schema_key(*qname(node.get("refer"), "keyref refer", node))
                    if kind == "keyref"
                    else None
                ),
                "refer_index": 0,
            }
            names[name] = len(constraints)
            constraints.append(constraint)
        return constraints

    def prefix_identity_xpath(xpath, prefix):
        alternatives = xpath.get("alternatives", [])
        if alternatives:
            return {
                "steps": [],
                "descendant": False,
                "attribute": False,
                "alternatives": [
                    prefix_identity_xpath(alternative, prefix) for alternative in alternatives
                ],
            }
        return {**xpath, "steps": list(prefix) + list(xpath["steps"])}

    def local_element_step(node):
        context = contexts[id(node)]
        if node.get("ref"):
            uri, name = qname(node.get("ref"), "element", node)
            return {"name": name, "ns": uri}
        form = node.get("form", context["efd"])
        return {
            "name": node.get("name"),
            "ns": context["target"] if form == "qualified" else None,
        }

    def nested_identities(element_node):
        """Collect identities on nested declarations with their owning scope."""
        constraints = []
        active_types = set()
        active_globals = set()

        def visit(node, path):
            if lname(node.tag) == "element" and node.get("type"):
                uri, type_name = qname(node.get("type"), "type", node)
                key = schema_key(uri, type_name)
                if key in type_nodes and key not in active_types:
                    active_types.add(key)
                    visit(type_nodes[key], path)
                    active_types.remove(key)
            for child in node:
                kind = lname(child.tag)
                if kind == "element":
                    child_path = path + [local_element_step(child)]
                    local_constraints = parse_identities(child)
                    for constraint in local_constraints:
                        constraint["scope"] = {
                            "steps": list(child_path),
                            "descendant": False,
                            "attribute": False,
                        }
                        constraints.append(constraint)
                    visit(child, child_path)
                    if child.get("ref"):
                        uri, element_name = qname(child.get("ref"), "element", child)
                        key = schema_key(uri, element_name)
                        if key in global_nodes and key not in active_globals:
                            active_globals.add(key)
                            referenced_constraints = parse_identities(global_nodes[key])
                            for constraint in referenced_constraints:
                                constraint["scope"] = {
                                    "steps": list(child_path),
                                    "descendant": False,
                                    "attribute": False,
                                }
                                constraints.append(constraint)
                            visit(global_nodes[key], child_path)
                            active_globals.remove(key)
                elif kind not in ("unique", "key", "keyref", "selector", "field"):
                    visit(child, path)

        visit(element_node, [])
        return constraints

    def validate_global_attribute_group(key, visiting=None):
        """Validate even an attribute group that no element happens to reference."""
        visiting = set() if visiting is None else visiting
        if key in visiting:
            raise InvalidSchemaError("circular attributeGroup reference")
        visiting.add(key)
        definition = attribute_group_nodes[key]
        seen = {}
        wildcard_seen = False
        for child in definition:
            kind = lname(child.tag)
            if wildcard_seen:
                raise InvalidSchemaError("xs:anyAttribute must be last in an attributeGroup")
            if kind == "attribute":
                attribute = parse_attribute(child, key.split("|", 1)[-1])
                expanded = {(attribute["name"], attribute["ns"]): attribute}
            elif kind == "attributeGroup":
                check(child, {"ref", "id"})
                if not child.get("ref") or list(child):
                    raise InvalidSchemaError("local xs:attributeGroup requires ref and no children")
                uri, group_name = qname(child.get("ref"), "attributeGroup", child)
                referenced = schema_key(uri, group_name)
                if referenced not in attribute_group_nodes:
                    raise InvalidSchemaError("invalid attributeGroup ref %r" % child.get("ref"))
                expanded = validate_global_attribute_group(referenced, visiting)
            elif kind == "anyAttribute":
                check(child, {"id", "namespace", "processContents"})
                if list(child):
                    raise InvalidSchemaError("xs:anyAttribute cannot contain child components")
                if child.get("processContents", "strict") not in (
                    "strict",
                    "lax",
                    "skip",
                ):
                    raise InvalidSchemaError("invalid processContents on xs:anyAttribute")
                normalized_wildcard_namespace(child)
                wildcard_seen = True
                expanded = {}
            else:
                raise InvalidSchemaError("xs:%s is not legal in an attributeGroup" % kind)
            for name, attribute in expanded.items():
                if name in seen:
                    raise InvalidSchemaError("duplicate attribute in attributeGroup")
                seen[name] = attribute
        visiting.remove(key)
        return seen

    # Phase 2: resolve declarations and their dependency relationships.
    for n in list(type_nodes):
        parse_named(n)
    group_aliases = set(redefined_bases.values())
    for index, (key, definition) in enumerate(group_nodes.items()):
        if key in group_aliases:
            continue
        document = contexts[id(definition)]
        namespace, group_name = key.split("|", 1)
        namespace = namespace or None
        mapping = document.get("element_nsmaps", {}).get(id(definition), document["nsmap"])
        prefix = next(
            (candidate for candidate, uri in mapping.items() if candidate and uri == namespace),
            None,
        )
        if mapping.get("") == namespace or namespace is None or document.get("chameleon"):
            reference = group_name
        elif prefix is not None:
            reference = prefix + ":" + group_name
        else:
            prefix = "zxsd_group"
            reference = prefix + ":" + group_name
            document = dict(document)
            document["nsmap"] = dict(mapping, **{prefix: namespace})
        synthetic = ET.Element(XS + "complexType")
        group_reference = ET.SubElement(synthetic, XS + "group", {"ref": reference})
        contexts[id(synthetic)] = document
        contexts[id(group_reference)] = document
        temporary_name = "__validate_group_%d" % index
        parse_complex(synthetic, temporary_name)
        types.pop(temporary_name, None)
    for key in attribute_group_nodes:
        derived = validate_global_attribute_group(key)
        node = attribute_group_nodes[key]
        base_key = redefined_bases.get(id(node))
        if base_key is not None and not redefined_self_references.get(id(node), False):
            base = validate_global_attribute_group(base_key)
            for name, attribute in derived.items():
                if name not in base:
                    raise InvalidSchemaError("attributeGroup restriction introduces an attribute")
                base_attribute = base[name]
                if base_attribute.get("prohibited") and not attribute.get("prohibited"):
                    raise InvalidSchemaError(
                        "attributeGroup restriction introduces a prohibited base attribute"
                    )
                if (
                    not attribute.get("prohibited")
                    and not base_attribute.get("prohibited")
                    and not simple_type_restricts(attribute["type"], base_attribute["type"])
                ):
                    raise InvalidSchemaError("attributeGroup restriction widens an attribute type")
                if base_attribute.get("fixed") is not None and (
                    attribute.get("fixed") is None
                    or comparable_value(attribute["type"], attribute["fixed"])
                    != comparable_value(base_attribute["type"], base_attribute["fixed"])
                ):
                    raise InvalidSchemaError("attributeGroup restriction changes a fixed attribute")
            for name, attribute in base.items():
                if attribute.get("required") and (
                    name not in derived or not derived[name].get("required")
                ):
                    raise InvalidSchemaError(
                        "attributeGroup restriction removes a required attribute"
                    )
    for n in order:
        parse_global(n)

    def type_derivation_path(candidate, base):
        if candidate == base:
            return []
        path = []
        current = candidate
        seen = set()
        while current and current not in seen:
            seen.add(current)
            definition = types.get(current)
            if not definition:
                break
            path.append(definition.get("derivation", 0))
            current = definition.get("base_ref")
            if current == base:
                return path
            if current is None and types.get(base, {}).get("any_type"):
                return path
        return None

    for element_key in order:
        element = globals_[element_key]
        if element["substitution_head"]:
            head_name, head_uri = element["substitution_head"]
            head_key = schema_key(head_uri, head_name)
            if head_key not in globals_:
                raise SchemaError("unknown substitutionGroup head %r" % head_name)
            head = globals_[head_key]
            child_ref = element["type"].get("complex") or element["type"].get("simple")
            head_ref = head["type"].get("complex") or head["type"].get("simple")
            if (
                not element["type"].get("complex")
                and head["type"].get("complex")
                and types[head["type"]["complex"]].get("any_type")
            ):
                path = ["ZXSD_DERIVATION_RESTRICTION"]
            elif element["type"].get("complex") and not head["type"].get("complex"):
                child_type = types[element["type"]["complex"]]
                content = child_type.get("simple_content")
                if content and head["type"].get("builtin") == "anySimpleType":
                    nested_path = []
                else:
                    content_ref = content.get("simple") if content else None
                    nested_path = (
                        type_derivation_path(content_ref, head_ref) if content_ref else None
                    )
                path = (
                    [child_type.get("derivation", 0)] + nested_path
                    if nested_path is not None
                    else None
                )
            elif not element["type"].get("complex") and not head["type"].get("complex"):
                path = (
                    []
                    if element["type"] == head["type"]
                    else (
                        ["ZXSD_DERIVATION_RESTRICTION"]
                        if simple_type_restricts(element["type"], head["type"])
                        else None
                    )
                )
            else:
                path = (
                    type_derivation_path(child_ref, head_ref)
                    if child_ref or head_ref
                    else ([] if element["type"]["base"] == head["type"]["base"] else None)
                )
            if path is None:
                raise SchemaError("substitutionGroup member type is not derived from head type")
            final = derivation_methods(
                global_nodes[head_key].get(
                    "final", contexts[id(global_nodes[head_key])]["final_default"]
                ),
                {"extension", "restriction"},
                global_nodes[head_key].get("final") is None,
            )
            if any(method_blocked(final, method) for method in path if method):
                raise SchemaError("head final blocks substitution member type")
    for element_key in order:
        seen = set()
        current = element_key
        while globals_[current].get("substitution_head"):
            if current in seen:
                raise InvalidSchemaError("circular substitutionGroup declarations")
            seen.add(current)
            head_name, head_uri = globals_[current]["substitution_head"]
            current = schema_key(head_uri, head_name)
    all_identity_names = {}
    for n in order:
        root_identities = parse_identities(global_nodes[n])
        local_identities = nested_identities(global_nodes[n])
        constraints = root_identities + local_identities

        globals_[n]["identities"] = constraints
        for constraint in constraints:
            if constraint["kind"] in ("key", "unique"):
                all_identity_names[schema_key(constraint["namespace"], constraint["name"])] = (
                    constraint
                )

    for n in order:
        constraints = globals_[n]["identities"]
        identity_names = {
            schema_key(constraint["namespace"], constraint["name"]): index
            for index, constraint in enumerate(constraints)
            if constraint["kind"] in ("key", "unique")
        }
        for constraint in constraints:
            if constraint["kind"] != "keyref":
                continue
            refer_key = constraint["refer_key"]
            if refer_key not in identity_names:
                if refer_key not in all_identity_names:
                    raise SchemaError("keyref refers to an unknown key or unique")
                identity_names[refer_key] = len(constraints)
                constraints.append(dict(all_identity_names[refer_key]))
            constraint["refer_index"] = identity_names[refer_key]
            referenced = constraints[constraint["refer_index"]]
            if len(constraint["fields"]) != len(referenced["fields"]):
                raise SchemaError("keyref field count differs from referenced key")
        globals_[n]["identities"] = constraints
    # Phase 3: assemble the immutable model consumed by the C emitter.
    global_attributes = []
    for attribute_key in attribute_order:
        global_attributes.append(parse_global_attribute(attribute_key))
    return {
        "target": target,
        "types": types,
        "globals": globals_,
        "order": order,
        "global_attributes": global_attributes,
        "notations": [notation_nodes[key] for key in notation_order],
    }
