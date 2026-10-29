# Copyright (c) 2026 zxsd contributors
# SPDX-License-Identifier: Apache-2.0

"""Emit deterministic typed and streaming C bindings."""

import re

from .codegen import abi_checks, abi_guard
from .common import (
    VERSION,
    WHITESPACE_PRESERVE,
    WHITESPACE_REPLACE,
    XML_URI,
    SchemaError,
    cstr,
    ident,
)


class BindingError(SchemaError):
    """Raised when a valid schema construct cannot be represented by typed bindings."""


# Built-in value kinds with a native C representation in bindings.
BIND_NATIVE_TYPES = {
    "ZXSD_T_BOOLEAN": ("ZXSD_BIND_BOOLEAN", "bool"),
    "ZXSD_T_BYTE": ("ZXSD_BIND_INT8", "int8_t"),
    "ZXSD_T_UNSIGNED_BYTE": ("ZXSD_BIND_UINT8", "uint8_t"),
    "ZXSD_T_SHORT": ("ZXSD_BIND_INT16", "int16_t"),
    "ZXSD_T_UNSIGNED_SHORT": ("ZXSD_BIND_UINT16", "uint16_t"),
    "ZXSD_T_INT": ("ZXSD_BIND_INT32", "int32_t"),
    "ZXSD_T_UNSIGNED_INT": ("ZXSD_BIND_UINT32", "uint32_t"),
    "ZXSD_T_LONG": ("ZXSD_BIND_INT64", "int64_t"),
    "ZXSD_T_UNSIGNED_LONG": ("ZXSD_BIND_UINT64", "uint64_t"),
}
# Value kinds whose value space is their whitespace-normalized lexical space.
BIND_ENUMERABLE_TYPES = {
    "ZXSD_T_STRING",
    "ZXSD_T_NORMALIZED_STRING",
    "ZXSD_T_TOKEN",
    "ZXSD_T_LANGUAGE",
    "ZXSD_T_NAME",
    "ZXSD_T_NCNAME",
    "ZXSD_T_NMTOKEN",
    "ZXSD_T_ANY_URI",
    "ZXSD_T_ID",
    "ZXSD_T_IDREF",
    "ZXSD_T_ENTITY",
}
# Identifiers that cannot name a structure member in C99 or C++ consumers.
C_RESERVED_WORDS = set("""
    alignas alignof and and_eq asm auto bitand bitor bool break case catch char char16_t
    char32_t class compl concept const const_cast consteval constexpr continue decltype
    default delete do double dynamic_cast else enum explicit export extern false float for
    friend goto if inline int long mutable namespace new noexcept not not_eq nullptr operator
    or or_eq private protected public register reinterpret_cast requires restrict return
    short signed sizeof static static_assert static_cast struct switch template this
    thread_local throw true try typedef typeid typename union unsigned using virtual void
    volatile wchar_t while xor xor_eq _Bool _Complex _Imaginary
    """.split())


def component_symbol(key, collection):
    """Return the C symbol fragment for a schema component key, as generate() does."""
    if "|" not in key:
        return ident(key)
    uri, name = key.split("|", 1)
    duplicates = sum(1 for candidate in collection if candidate.split("|", 1)[-1] == name)
    return ident(name) if duplicates == 1 else ident(uri + "_" + name)


def comment_text(value):
    """Make arbitrary schema text safe inside a C block comment."""
    return re.sub(r"\s+", " ", value).replace("*/", "*\\/")


def normalize_enumeration(value, whitespace):
    """Apply an XML Schema whiteSpace facet to an enumeration value."""
    if whitespace == "ZXSD_WS_PRESERVE":
        return value
    value = re.sub(r"[\t\n\r]", " ", value)
    if whitespace == "ZXSD_WS_REPLACE":
        return value
    return " ".join(part for part in value.split(" ") if part)


def resolve_stream_paths(model, paths):
    """Resolve finite absolute element paths, never runtime XPath expressions."""
    selections = []
    symbols = set()
    for path in paths:
        parts, current, braced = [], "", False
        for character in path.removeprefix("/"):
            if character == "{":
                if braced or current:
                    raise BindingError("bindings: malformed stream path %r" % path)
                braced = True
            elif character == "}":
                if not braced:
                    raise BindingError("bindings: malformed stream path %r" % path)
                braced = False
            if character == "/" and not braced:
                parts.append(current)
                current = ""
            else:
                current += character
        parts.append(current)
        if braced or any(not part for part in parts):
            raise BindingError("bindings: malformed stream path %r" % path)
        steps, root_key, element = [], None, None
        for index, part in enumerate(parts):
            uri, name = None, part
            qualified = part.startswith("{")
            if qualified:
                uri, name = part[1:].split("}", 1)
                uri = uri or None
            if not name or name in (".", "..") or any(c in name for c in ":*[]@{}"):
                raise BindingError("bindings: stream paths support only element names: %r" % path)
            candidates = (
                [(key, model["globals"][key]) for key in model["order"]]
                if index == 0
                else [
                    (None, child)
                    for child in model["types"]
                    .get(element["type"].get("complex"), {})
                    .get("elements", [])
                ]
            )
            matches = [
                (key, child)
                for key, child in candidates
                if child["name"] == name and (not qualified or child["ns"] == uri)
            ]
            if len(matches) != 1:
                reason = "ambiguous" if matches else "unknown"
                raise BindingError("bindings: %s stream path step %r in %r" % (reason, part, path))
            key, element = matches[0]
            if index == 0:
                root_key = key
            if element.get("abstract") or any(
                global_.get("substitution_head") == [element["name"], element["ns"]]
                or global_.get("substitution_head") == (element["name"], element["ns"])
                for global_ in model["globals"].values()
            ):
                raise BindingError("bindings: stream path crosses a substitution-group head")
            steps.append((element["name"], element["ns"]))
        if element.get("nillable"):
            raise BindingError("bindings: selected stream element is nillable")
        if any(item["steps"] == steps for item in selections):
            raise BindingError("bindings: duplicate stream selection %r" % path)
        base = "_".join(ident(name) for name, _uri in steps)
        symbol, suffix = base, 2
        while symbol in symbols:
            symbol, suffix = "%s_%d" % (base, suffix), suffix + 1
        symbols.add(symbol)
        selections.append({"steps": steps, "root": root_key, "element": element, "symbol": symbol})
    return selections


def generate_bindings(stem, m, selected_roots=None, stream_paths=None, stream_only=False):
    """Render typed C structures and binding descriptors for complex global elements.

    Every construct reachable from a bound root must have a faithful C
    representation; anything else raises BindingError instead of being dropped.
    """
    sid = ident(stem)
    guard = sid.upper() + "_BIND_H"
    types = dict(m["types"])
    globals_ = m["globals"]
    root_symbol = lambda key: component_symbol(key, m["order"])
    type_symbol = lambda key: component_symbol(key, types)
    heads = {tuple(e["substitution_head"]) for e in globals_.values() if e.get("substitution_head")}

    selections = resolve_stream_paths(m, stream_paths or [])
    roots = [] if stream_only else [key for key in m["order"] if globals_[key]["type"]["complex"]]
    if selected_roots:
        chosen = []
        for wanted in selected_roots:
            matches = [
                key
                for key in m["order"]
                if wanted
                in (
                    globals_[key]["name"],
                    "{%s}%s" % (globals_[key]["ns"] or "", globals_[key]["name"]),
                )
            ]
            if not matches:
                raise BindingError("bindings: unknown root element %r" % wanted)
            for key in matches:
                if not globals_[key]["type"]["complex"]:
                    raise BindingError(
                        "bindings: root element %r has a simple type; bindings require a "
                        "complex root" % globals_[key]["name"]
                    )
                if key not in chosen:
                    chosen.append(key)
        roots = [key for key in m["order"] if key in chosen]
    if not roots and not selections:
        raise BindingError("bindings: the schema declares no global element with a complex type")

    type_names = set()

    def unique_type_name(base):
        base = ident(base)
        candidate = base
        suffix = 2
        while candidate in type_names:
            candidate = "%s_%d" % (base, suffix)
            suffix += 1
        type_names.add(candidate)
        return candidate

    enums = {}
    structs = {}

    def is_qname_like(ref):
        if ref is None:
            return False
        if ref.get("base") in ("ZXSD_T_QNAME", "ZXSD_T_NOTATION"):
            return True
        simple = ref.get("simple")
        if simple:
            definition = types[simple]
            if definition.get("base") in ("ZXSD_T_QNAME", "ZXSD_T_NOTATION"):
                return True
            return is_qname_like(definition.get("item")) or any(
                is_qname_like(member) for member in definition.get("members", [])
            )
        return False

    def simple_value(ref, owner, item_name, context):
        """Return (kind, C type, whitespace, enum) for a simple type reference."""
        if is_qname_like(ref):
            raise BindingError(
                "bindings: %s has a QName or NOTATION value, which depends on namespace "
                "declarations that bindings do not represent" % context
            )
        simple = ref.get("simple")
        if simple:
            definition = types[simple]
            variety = definition.get("variety", "ZXSD_SIMPLE_ATOMIC")
            if variety == "ZXSD_SIMPLE_LIST":
                return "ZXSD_BIND_STRING", "const char *", "ZXSD_WS_COLLAPSE", None
            if variety == "ZXSD_SIMPLE_UNION":
                return "ZXSD_BIND_STRING", "const char *", "ZXSD_WS_PRESERVE", None
            base = definition["base"]
            whitespace = definition.get("whitespace", "ZXSD_WS_PRESERVE")
            if definition.get("enums") and base in BIND_ENUMERABLE_TYPES:
                if simple not in enums:
                    name = unique_type_name(
                        type_symbol(simple)
                        if definition.get("type_name")
                        else "%s_%s" % (owner, item_name)
                    )
                    values = []
                    for value in definition["enums"]:
                        value = normalize_enumeration(value, whitespace)
                        if value not in values:
                            values.append(value)
                    enums[simple] = {
                        "name": name,
                        "values": values,
                        "xsd": definition.get("type_name"),
                    }
                return (
                    "ZXSD_BIND_ENUM",
                    "%s_%s_t" % (sid, enums[simple]["name"]),
                    whitespace,
                    enums[simple],
                )
            if base in BIND_NATIVE_TYPES:
                kind, ctype = BIND_NATIVE_TYPES[base]
                return kind, ctype, whitespace, None
            return "ZXSD_BIND_STRING", "const char *", whitespace, None
        if ref.get("list_builtin"):
            return "ZXSD_BIND_STRING", "const char *", "ZXSD_WS_COLLAPSE", None
        base = ref.get("base", "ZXSD_T_STRING")
        builtin = ref.get("builtin") or "string"
        if builtin in WHITESPACE_PRESERVE:
            whitespace = "ZXSD_WS_PRESERVE"
        elif builtin in WHITESPACE_REPLACE:
            whitespace = "ZXSD_WS_REPLACE"
        else:
            whitespace = "ZXSD_WS_COLLAPSE"
        if base in BIND_NATIVE_TYPES:
            kind, ctype = BIND_NATIVE_TYPES[base]
            return kind, ctype, whitespace, None
        return "ZXSD_BIND_STRING", "const char *", whitespace, None

    def flatten(particle, optional, out, context):
        if particle["kind"] == "element":
            if particle["max"] != 0:
                out.append(
                    (
                        particle["element"],
                        0 if optional else particle["min"],
                        particle["max"],
                    )
                )
            return
        if particle["kind"] == "any":
            raise BindingError("bindings: %s contains an element wildcard (xs:any)" % context)
        if particle["max"] == 0:
            return
        if particle["max"] is None or particle["max"] > 1:
            if particle["kind"] != "sequence" or particle["max"] is not None:
                raise BindingError(
                    "bindings: %s contains a repeating xs:%s group; only optional "
                    "members of an unbounded sequence can be represented"
                    % (context, particle["kind"])
                )
            if any(child["min"] != 0 for child in particle["children"]):
                raise BindingError(
                    "bindings: %s contains required members in a repeating xs:sequence group"
                    % context
                )
            repeated = []
            for child in particle["children"]:
                flatten(child, True, repeated, context)
            for element, _minimum, maximum in repeated:
                combined = (
                    None
                    if particle["max"] is None or maximum is None
                    else particle["max"] * maximum
                )
                out.append((element, 0, combined))
            return
        children = particle["children"]
        child_optional = (
            optional or particle["min"] == 0 or (particle["kind"] == "choice" and len(children) > 1)
        )
        for child in children:
            flatten(child, child_optional, out, context)

    def content_particle(definition):
        if definition.get("particle") is not None or not definition["elements"]:
            return definition.get("particle")
        kind = {
            "ZXSD_SEQUENCE": "sequence",
            "ZXSD_CHOICE": "choice",
            "ZXSD_ALL": "all",
        }[definition["model"]]
        return {
            "kind": kind,
            "min": definition["group_min"],
            "max": definition["group_max"],
            "children": [
                {
                    "kind": "element",
                    "min": e["min"],
                    "max": e["max"],
                    "element": e,
                    "children": [],
                }
                for e in definition["elements"]
            ],
        }

    def struct_for(key, suggested):
        if key in structs:
            return structs[key]
        definition = types[key]
        display = (
            "type %r" % definition["type_name"]
            if definition.get("type_name")
            else "the anonymous type of %r" % suggested
        )
        if definition.get("any_type"):
            raise BindingError("bindings: %s is xs:anyType, which has no fixed structure" % display)
        if definition.get("mixed"):
            raise BindingError("bindings: %s has mixed content" % display)
        if definition.get("any_attribute"):
            raise BindingError("bindings: %s contains an attribute wildcard" % display)
        if definition.get("abstract"):
            raise BindingError(
                "bindings: %s is abstract and is only usable through xsi:type" % display
            )
        name = unique_type_name(type_symbol(key) if definition.get("type_name") else suggested)
        info = {"key": key, "name": name, "fields": [], "definition": definition}
        structs[key] = info
        fields = []
        for attribute in definition["attributes"]:
            if attribute.get("prohibited"):
                continue
            context = "attribute %r of %s" % (attribute["name"], display)
            kind, ctype, whitespace, enum = simple_value(
                attribute["type"], name, attribute["name"], context
            )
            fields.append(
                {
                    "role": "ZXSD_BIND_ATTRIBUTE",
                    "name": attribute["name"],
                    "ns": attribute["ns"],
                    "kind": kind,
                    "ctype": ctype,
                    "whitespace": whitespace,
                    "enum": enum,
                    "min": 1 if attribute["required"] else 0,
                    "max": 1,
                    "default": attribute.get("default"),
                    "fixed": attribute.get("fixed"),
                    "struct": None,
                }
            )
        particle = content_particle(definition)
        if particle is not None:
            flattened = []
            flatten(particle, False, flattened, display)
            seen = set()
            for element, minimum, maximum in flattened:
                expanded = (element["ns"], element["name"])
                context = "element %r of %s" % (element["name"], display)
                if expanded in seen:
                    raise BindingError(
                        "bindings: %s occurs at several places of the content model" % context
                    )
                seen.add(expanded)
                if element.get("nillable"):
                    raise BindingError("bindings: %s is nillable" % context)
                if element.get("abstract") or expanded[::-1] in heads:
                    raise BindingError("bindings: %s is the head of a substitution group" % context)
                field = {
                    "role": "ZXSD_BIND_ELEMENT",
                    "name": element["name"],
                    "ns": element["ns"],
                    "min": minimum,
                    "max": maximum,
                    "default": element.get("default"),
                    "fixed": element.get("fixed"),
                    "enum": None,
                    "struct": None,
                }
                reference = element["type"]
                if reference["complex"]:
                    child = struct_for(reference["complex"], "%s_%s" % (name, element["name"]))
                    field.update(
                        kind="ZXSD_BIND_COMPLEX",
                        ctype="%s_%s_t" % (sid, child["name"]),
                        whitespace="ZXSD_WS_PRESERVE",
                        struct=child,
                    )
                else:
                    kind, ctype, whitespace, enum = simple_value(
                        reference, name, element["name"], context
                    )
                    field.update(kind=kind, ctype=ctype, whitespace=whitespace, enum=enum)
                fields.append(field)
        content = definition.get("simple_content")
        if content:
            context = "the character content of %s" % display
            kind, ctype, whitespace, enum = simple_value(content, name, "value", context)
            fields.append(
                {
                    "role": "ZXSD_BIND_CONTENT",
                    "name": "value",
                    "ns": None,
                    "kind": kind,
                    "ctype": ctype,
                    "whitespace": whitespace,
                    "enum": enum,
                    "min": 1,
                    "max": 1,
                    "default": None,
                    "fixed": None,
                    "struct": None,
                }
            )
        info["fields"] = fields
        return info

    root_structs = []
    for key in roots:
        root = globals_[key]
        if root.get("nillable"):
            raise BindingError("bindings: root element %r is nillable" % root["name"])
        if root.get("abstract") or (root["name"], root["ns"]) in heads:
            raise BindingError(
                "bindings: root element %r is abstract or heads a substitution group" % root["name"]
            )
        root_structs.append(struct_for(root["type"]["complex"], root_symbol(key)))

    for selection in selections:
        element = selection["element"]
        key = element["type"].get("complex")
        if key is None:
            key = "#stream-value-" + selection["symbol"]
            types[key] = {
                "kind": "complex",
                "elements": [],
                "attributes": [],
                "simple_content": element["type"],
            }
        selection["struct"] = struct_for(key, selection["symbol"])

    # Reader typedefs share C's ordinary identifier namespace with bound types.
    for selection in selections:
        base = selection["symbol"]
        suffix = 2
        while selection["symbol"] + "_reader" in type_names:
            selection["symbol"] = "%s_%d" % (base, suffix)
            suffix += 1
        type_names.add(selection["symbol"] + "_reader")

    # A required single child is embedded by value unless its type can reach the
    # enclosing type, in which case the recursion is broken with a pointer.
    edges = {
        key: [field["struct"]["key"] for field in info["fields"] if field["struct"]]
        for key, info in structs.items()
    }

    def reaches(start, target):
        stack, seen = [start], set()
        while stack:
            current = stack.pop()
            if current == target:
                return True
            if current not in seen:
                seen.add(current)
                stack.extend(edges[current])
        return False

    for key, info in structs.items():
        for field in info["fields"]:
            if field["max"] is None or field["max"] > 1:
                field["storage"] = "ZXSD_BIND_ARRAY"
            elif field["struct"] is not None and (
                field["min"] == 0 or reaches(field["struct"]["key"], key)
            ):
                field["storage"] = "ZXSD_BIND_POINTER"
            else:
                field["storage"] = "ZXSD_BIND_INLINE"

    # Assign collision-free member names, including presence and count helpers.
    for info in structs.values():
        used = set()

        def claim(base, role):
            base = ident(base)
            if base.startswith("_"):
                base = "x" + base
            if base in C_RESERVED_WORDS:
                base += "_"
            alternatives = [
                base,
                base + ("_attr" if role == "ZXSD_BIND_ATTRIBUTE" else "_elem"),
            ]
            suffix = 2
            while True:
                for candidate in alternatives:
                    helpers = {candidate, "has_" + candidate, candidate + "_count"}
                    if not helpers & used:
                        used.update(helpers)
                        return candidate
                alternatives = ["%s_%d" % (base, suffix)]
                suffix += 1

        ordered = sorted(
            info["fields"],
            key=lambda field: 0 if field["role"] == "ZXSD_BIND_CONTENT" else 1,
        )
        for field in ordered:
            field["member"] = claim(field["name"], field["role"])

    # Structures embedded by value must be defined before their containers.
    definition_order = []
    visited = set()

    def define(info):
        if info["key"] in visited:
            return
        visited.add(info["key"])
        for field in info["fields"]:
            if field["struct"] is not None and field["storage"] == "ZXSD_BIND_INLINE":
                define(field["struct"])
        definition_order.append(info)

    for info in structs.values():
        define(info)

    def occurrence_text(field):
        maximum = "unbounded" if field["max"] is None else str(field["max"])
        return "%d to %s occurrences" % (field["min"], maximum)

    def member_comment(field):
        item = {
            "ZXSD_BIND_ATTRIBUTE": "Attribute",
            "ZXSD_BIND_ELEMENT": "Element",
            "ZXSD_BIND_CONTENT": "Character content",
        }[field["role"]]
        if field["role"] == "ZXSD_BIND_CONTENT":
            return item + "."
        text = "%s @c %s" % (item, comment_text(field["name"]))
        if field["storage"] == "ZXSD_BIND_ARRAY":
            return text + ", " + occurrence_text(field) + "."
        if field["min"] == 0:
            absent = (
                "@c NULL when absent"
                if field["kind"] in ("ZXSD_BIND_STRING", "ZXSD_BIND_COMPLEX")
                else "valid when @c has_%s is true" % field["member"]
            )
            return "%s, optional; %s." % (text, absent)
        return text + "."

    license_header = (
        "/*\n * Copyright (c) 2026 zxsd contributors\n *\n"
        " * SPDX-License-Identifier: Apache-2.0\n */\n\n"
    )
    file_doc = license_header + (
        "/**\n * @file\n * @brief Generated zxsd typed bindings for the " + stem + " schema.\n"
        f" *\n * @note Generated by zxsdc {VERSION}. Do not edit manually.\n */\n\n"
    )
    h = [
        file_doc + f"#ifndef {guard}\n#define {guard}\n",
        "#include <zxsd_bind.h>\n",
        abi_checks(bindings=True),
        "#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n",
        '#ifdef __cplusplus\nextern "C" {\n#endif\n',
    ]
    enum_constants = set()
    for enum in enums.values():
        prefix = ("%s_%s" % (sid, enum["name"])).upper()
        source = (
            "the %s simple type" % comment_text(enum["xsd"])
            if enum["xsd"]
            else "an anonymous simple type"
        )
        lines = ["/** Enumeration values of %s. */" % source, "typedef enum {"]
        for index, value in enumerate(enum["values"]):
            base = "%s_%s" % (prefix, ident(value).upper() if value else "EMPTY")
            constant = base
            suffix = 2
            while constant in enum_constants:
                constant = "%s_%d" % (base, suffix)
                suffix += 1
            enum_constants.add(constant)
            lines.append('\t%s = %d, /**< "%s" */' % (constant, index, comment_text(value)))
        lines.append("} %s_%s_t;\n" % (sid, enum["name"]))
        h.append("\n".join(lines))
    for info in structs.values():
        h.append("typedef struct %s_%s %s_%s_t;" % (sid, info["name"], sid, info["name"]))
    h.append("")
    for info in definition_order:
        definition = info["definition"]
        source = (
            "complex type @c %s" % comment_text(definition["type_name"])
            if definition.get("type_name")
            else "an anonymous complex type"
        )
        lines = [
            "/** Typed binding of %s. */" % source,
            "struct %s_%s {" % (sid, info["name"]),
        ]
        for field in info["fields"]:
            member, ctype = field["member"], field["ctype"]
            comment = member_comment(field)
            if field["storage"] == "ZXSD_BIND_ARRAY":
                pointer = "const char *const *" if ctype == "const char *" else "const %s *" % ctype
                lines.append("\t/** %s */\n\t%s%s;" % (comment, pointer, member))
                lines.append(
                    "\t/** Number of items in @c %s. */\n\tsize_t %s_count;" % (member, member)
                )
            elif field["storage"] == "ZXSD_BIND_POINTER":
                lines.append("\t/** %s */\n\tconst %s *%s;" % (comment, ctype, member))
            else:
                if field["min"] == 0 and field["kind"] not in (
                    "ZXSD_BIND_STRING",
                    "ZXSD_BIND_COMPLEX",
                ):
                    lines.append(
                        "\t/** Whether @c %s is present. */\n\tbool has_%s;" % (member, member)
                    )
                separator = "" if ctype.endswith("*") else " "
                lines.append("\t/** %s */\n\t%s%s%s;" % (comment, ctype, separator, member))
        if not info["fields"]:
            lines.append(
                "\t/** Placeholder: C forbids empty structures. */\n\tunsigned char reserved;"
            )
        lines.append("};\n")
        h.append("\n".join(lines))
    for key, info in zip(roots, root_structs):
        root = globals_[key]
        prefix = "%s_%s" % (sid, root_symbol(key))
        ctype = "%s_%s_t" % (sid, info["name"])
        name = comment_text(root["name"])
        h.append(
            f"/**\n * @brief Validate a document and fill a {ctype} from its {name} root.\n"
            " *\n * @param[in] xml NUL-terminated XML document.\n"
            " * @param[out] object Structure to fill.\n"
            " * @param[in,out] arena Storage for strings, arrays and optional structures.\n"
            " *\n"
            " * @return A structured result; see zxsd_bind_read().\n */\n"
            f"zxsd_result_t {prefix}_read(const char *xml, {ctype} *object, zxsd_bind_arena_t *arena);\n\n"
            f"/**\n * @brief Write a {ctype} as an XML document with a {name} root.\n"
            " *\n * @param[in] object Structure to serialize.\n"
            " * @param[in] sink Output callback.\n"
            " * @param[in] context Context passed to @p sink.\n"
            " * @param[in,out] validator Optional initialized streaming validator, or @c NULL.\n"
            " *\n"
            " * @return A structured result; see zxsd_bind_write().\n */\n"
            f"zxsd_result_t {prefix}_write(const {ctype} *object, zxsd_bind_sink_fn sink, void *context,\n"
            "\t\t\tzxsd_stream_t *validator);\n\n"
            f"/**\n * @brief Write a {ctype} with a {name} root into a buffer and validate it.\n"
            " *\n * @param[in] object Structure to serialize.\n"
            " * @param[out] buffer Destination buffer.\n"
            " * @param[in] capacity Size of @p buffer, including the terminating NUL.\n"
            " * @param[out] length Optional output receiving the document length.\n"
            " *\n"
            " * @return A structured result; see zxsd_bind_write_buffer().\n */\n"
            f"zxsd_result_t {prefix}_write_buffer(const {ctype} *object, char *buffer, size_t capacity,\n"
            "\t\t\t\tsize_t *length);\n\n"
            f"/**\n * @brief Get the binding descriptor of the {name} root.\n *\n"
            " * @return Generated immutable binding descriptor.\n */\n"
            f"const zxsd_bind_root_t *{prefix}_binding(void);\n"
        )
    for selection in selections:
        prefix = "%s_%s" % (sid, selection["symbol"])
        ctype = "%s_%s_t" % (sid, selection["struct"]["name"])
        h.append(
            f"/** @brief Deliver one locally validated subtree; pointers expire on return.\n"
            " * @param[in] user_data Application context.\n"
            " * @param[in] object Selected value; document validity is decided at finish.\n"
            " * @return Delivery action; see zxsd_bind_action_t.\n */\n"
            f"typedef zxsd_bind_action_t (*{prefix}_item_fn)(void *user_data,\n"
            f"\t\tconst {ctype} *object);\n\n"
            "/** Caller-owned typed streaming reader; initialize before use. */\n"
            f"typedef struct {{\n\tzxsd_bind_stream_t stream; /**< Generic reader. */\n"
            f"\t{prefix}_item_fn callback; /**< Typed delivery callback. */\n"
            f"\tvoid *user_data; /**< Application context. */\n}} {prefix}_reader_t;\n\n"
            "/** @brief Initialize full-document validation and selected typed delivery.\n"
            " * @param[out] reader Reader to initialize.\n"
            " * @param[out] object Reusable output structure.\n"
            " * @param[in,out] arena Reusable subtree storage.\n"
            " * @param[in] callback Typed delivery callback.\n"
            " * @param[in] user_data Application context.\n"
            " * @param[in] allocator Identity scratch allocator, or NULL.\n"
            " * @return Structured initialization result.\n */\n"
            f"zxsd_result_t {prefix}_reader_init({prefix}_reader_t *reader,\n"
            f"\t\t{ctype} *object, zxsd_bind_arena_t *arena,\n"
            f"\t\t{prefix}_item_fn callback, void *user_data,\n"
            "\t\tconst zxsd_allocator_t *allocator);\n\n"
            "/** @brief Feed raw XML fragments.\n"
            " * @param[in,out] reader Initialized reader.\n"
            " * @param[in] data Raw XML bytes.\n * @param[in] length Byte count.\n"
            " * @return First sticky error or success.\n */\n"
            f"zxsd_result_t {prefix}_reader_feed({prefix}_reader_t *reader,\n"
            "\t\tconst void *data, size_t length);\n\n"
            "/** @brief Finish full-document validation, including identity constraints.\n"
            " * @param[in,out] reader Initialized reader.\n"
            " * @return Final document result.\n */\n"
            f"zxsd_result_t {prefix}_reader_finish({prefix}_reader_t *reader);\n\n"
            "/** @brief Release scratch storage and restore the arena.\n"
            " * @param[in,out] reader Initialized reader, or NULL.\n */\n"
            f"void {prefix}_reader_dispose({prefix}_reader_t *reader);\n"
        )
    h.append("#ifdef __cplusplus\n}\n#endif\n#endif")

    def c_string(value):
        return "NULL" if value is None else cstr(value)

    out = [
        file_doc.rstrip(),
        "",
        f'#include "{stem}.bind.h"',
        f'#include "{stem}.zxsd.h"',
        "",
        "#include <limits.h>",
        "#include <stddef.h>",
        "",
        abi_checks(bindings=True),
        abi_guard(sid + "_binding", bindings=True),
    ]
    for enum in enums.values():
        out.append(
            "static const char *const enum_values_%s[] = {%s};"
            % (enum["name"], ", ".join(cstr(value) for value in enum["values"]))
        )
    for info in structs.values():
        out.append("static const zxsd_bind_type_t binding_type_%s;" % info["name"])
    out.append("")
    for info in structs.values():
        ctype = "%s_%s_t" % (sid, info["name"])
        entries = []
        for field in info["fields"]:
            member = field["member"]
            if field["storage"] == "ZXSD_BIND_ARRAY":
                value_size = "sizeof(%s)" % field["ctype"]
                presence = "ZXSD_BIND_NO_OFFSET"
                count = "offsetof(%s, %s_count)" % (ctype, member)
            else:
                value_size = "sizeof(%s)" % field["ctype"]
                count = "ZXSD_BIND_NO_OFFSET"
                presence = (
                    "offsetof(%s, has_%s)" % (ctype, member)
                    if field["storage"] == "ZXSD_BIND_INLINE"
                    and field["min"] == 0
                    and field["kind"] not in ("ZXSD_BIND_STRING", "ZXSD_BIND_COMPLEX")
                    else "ZXSD_BIND_NO_OFFSET"
                )
            nested = "&binding_type_%s" % field["struct"]["name"] if field["struct"] else "NULL"
            enum = field["enum"]
            enumeration = (
                "enum_values_%s, %du" % (enum["name"], len(enum["values"])) if enum else "NULL, 0u"
            )
            maximum = "ULONG_MAX" if field["max"] is None else "%dUL" % field["max"]
            entries.append(
                "\t{%s, %s, %s, %s, %s, %dUL, %s,\n\t offsetof(%s, %s), %s, %s, %s,\n\t %s, %s, %s, %s, %s}"
                % (
                    cstr(field["name"]),
                    c_string(field["ns"]),
                    field["role"],
                    field["kind"],
                    field["storage"],
                    field["min"],
                    maximum,
                    ctype,
                    member,
                    presence,
                    count,
                    value_size,
                    nested,
                    enumeration,
                    field["whitespace"],
                    c_string(field["default"]),
                    c_string(field["fixed"]),
                )
            )
        if entries:
            out.append(
                "static const zxsd_bind_field_t binding_fields_%s[] = {\n%s,\n};"
                % (info["name"], ",\n".join(entries))
            )
            fields_pointer, field_count = "binding_fields_%s" % info["name"], len(entries)
        else:
            fields_pointer, field_count = "NULL", 0
        definition = info["definition"]
        out.append(
            "static const zxsd_bind_type_t binding_type_%s = {%s, %s, %s, sizeof(%s), %s, %du};\n"
            % (
                info["name"],
                cstr(ctype),
                c_string(definition.get("type_name")),
                c_string(definition.get("type_ns")),
                ctype,
                fields_pointer,
                field_count,
            )
        )
    for key, info in zip(roots, root_structs):
        root = globals_[key]
        symbol = root_symbol(key)
        prefix = "%s_%s" % (sid, symbol)
        ctype = "%s_%s_t" % (sid, info["name"])
        namespaces = []

        def collect(item, seen):
            if item["key"] in seen:
                return
            seen.add(item["key"])
            for field in item["fields"]:
                if field["ns"] and field["ns"] != XML_URI and field["ns"] not in namespaces:
                    namespaces.append(field["ns"])
                if field["struct"] is not None:
                    collect(field["struct"], seen)

        if root["ns"]:
            namespaces.append(root["ns"])
        collect(info, set())
        if namespaces:
            out.append(
                "static const zxsd_bind_namespace_t binding_namespaces_%s[] = {%s};"
                % (
                    symbol,
                    ", ".join(
                        "{%s, %s}" % (cstr("ns%d" % (index + 1)), cstr(uri))
                        for index, uri in enumerate(namespaces)
                    ),
                )
            )
            namespace_pointer = "binding_namespaces_%s" % symbol
        else:
            namespace_pointer = "NULL"
        out.append(
            "static const zxsd_bind_root_t binding_root_%s = {%s, %s, &binding_type_%s, %s_schema,\n\t%s, %du, %s, %s};\n"
            % (
                symbol,
                cstr(root["name"]),
                c_string(root["ns"]),
                info["name"],
                prefix,
                namespace_pointer,
                len(namespaces),
                c_string(root.get("default")),
                c_string(root.get("fixed")),
            )
        )
        out.append(
            f"zxsd_result_t {prefix}_read(const char *xml, {ctype} *object, zxsd_bind_arena_t *arena)\n"
            f"{{\n\t{sid}_binding_require_abi();\n\treturn zxsd_bind_read(&binding_root_{symbol}, xml, object, arena);\n}}\n\n"
            f"zxsd_result_t {prefix}_write(const {ctype} *object, zxsd_bind_sink_fn sink, void *context,\n"
            "\t\t\tzxsd_stream_t *validator)\n"
            f"{{\n\t{sid}_binding_require_abi();\n\treturn zxsd_bind_write(&binding_root_{symbol}, object, sink, context, validator);\n}}\n\n"
            f"zxsd_result_t {prefix}_write_buffer(const {ctype} *object, char *buffer, size_t capacity,\n"
            "\t\t\t\tsize_t *length)\n"
            f"{{\n\t{sid}_binding_require_abi();\n\treturn zxsd_bind_write_buffer(&binding_root_{symbol}, object, buffer, capacity, length);\n}}\n\n"
            f"const zxsd_bind_root_t *{prefix}_binding(void)\n{{\n\t{sid}_binding_require_abi();\n\treturn &binding_root_{symbol};\n}}\n"
        )
    for selection in selections:
        prefix = "%s_%s" % (sid, selection["symbol"])
        ctype = "%s_%s_t" % (sid, selection["struct"]["name"])
        path_symbol = "stream_path_" + selection["symbol"]
        out.append(
            "static const zxsd_bind_path_step_t %s[] = {%s};\n"
            % (
                path_symbol,
                ", ".join("{%s, %s}" % (cstr(n), c_string(ns)) for n, ns in selection["steps"]),
            )
        )
        out.append(
            "static const zxsd_bind_selection_t selection_%s = {%s_%s_schema, %s, %du,\n"
            "\t&binding_type_%s, %s, %s};\n"
            % (
                selection["symbol"],
                sid,
                root_symbol(selection["root"]),
                path_symbol,
                len(selection["steps"]),
                selection["struct"]["name"],
                c_string(selection["element"].get("default")),
                c_string(selection["element"].get("fixed")),
            )
        )
        out.append(
            "/** Adapt the generated callback without converting a function pointer. */\n"
            f"static zxsd_bind_action_t {prefix}_deliver(void *context, const void *object)\n"
            f"{{\n\t{prefix}_reader_t *reader = context;\n"
            f"\treturn reader->callback(reader->user_data, (const {ctype} *)object);\n}}\n\n"
            f"zxsd_result_t {prefix}_reader_init({prefix}_reader_t *reader,\n"
            f"\t\t{ctype} *object, zxsd_bind_arena_t *arena,\n"
            f"\t\t{prefix}_item_fn callback, void *user_data,\n"
            "\t\tconst zxsd_allocator_t *allocator)\n{\n"
            f"\t{sid}_binding_require_abi();\n"
            "\tif (reader != NULL) {\n\t\treader->callback = callback;\n"
            "\t\treader->user_data = user_data;\n\t}\n"
            f"\treturn zxsd_bind_stream_init(reader != NULL ? &reader->stream : NULL,\n"
            f"\t\t&selection_{selection['symbol']}, object, arena,\n"
            f"\t\tcallback != NULL ? {prefix}_deliver : NULL, reader, allocator);\n}}\n\n"
            f"zxsd_result_t {prefix}_reader_feed({prefix}_reader_t *reader,\n"
            "\t\tconst void *data, size_t length)\n{\n"
            f"\t{sid}_binding_require_abi();\n"
            "\treturn zxsd_bind_stream_feed(reader != NULL ? &reader->stream : NULL, data, length);\n}\n\n"
            f"zxsd_result_t {prefix}_reader_finish({prefix}_reader_t *reader)\n{{\n"
            f"\t{sid}_binding_require_abi();\n"
            "\treturn zxsd_bind_stream_finish(reader != NULL ? &reader->stream : NULL);\n}\n\n"
            f"void {prefix}_reader_dispose({prefix}_reader_t *reader)\n{{\n"
            f"\t{sid}_binding_require_abi();\n"
            "\tzxsd_bind_stream_dispose(reader != NULL ? &reader->stream : NULL);\n}\n"
        )
    return "\n".join(h) + "\n", "\n".join(out).rstrip("\n") + "\n"
