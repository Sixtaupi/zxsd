# Copyright (c) 2026 mxsd contributors
# SPDX-License-Identifier: Apache-2.0

"""Derive Zephyr Kconfig recommendations from a compiled schema model."""

from .common import XML_URI, XSD_URI, XSI_URI, SchemaError, ident
from .patterns import PatternResources


def zephyr_particle_nodes(particle):
    """Walk descriptor particles, including terms with zero occurrences."""
    if particle is not None:
        yield particle
        for child in particle.get("children", []):
            yield from zephyr_particle_nodes(child)


def zephyr_particle_bounds(particle):
    """Return descriptor depth and minimum/maximum direct-child token counts."""
    if particle is None:
        return 0, 0, 0
    children = [zephyr_particle_bounds(child) for child in particle.get("children", [])]
    depth = 1 + max((child[0] for child in children), default=0)
    if particle["kind"] in ("element", "any"):
        minimum = maximum = 1
    elif particle["kind"] == "choice":
        minimum = min((child[1] for child in children), default=0)
        maximum = (
            None
            if any(child[2] is None for child in children)
            else max((child[2] for child in children), default=0)
        )
    else:
        minimum = sum(child[1] for child in children)
        maximum = (
            None if any(child[2] is None for child in children) else sum(c[2] for c in children)
        )
    minimum *= particle["min"]
    if particle["max"] == 0 or maximum == 0:
        maximum = 0
    elif maximum is None or particle["max"] is None:
        maximum = None
    else:
        maximum *= particle["max"]
    return depth, minimum, maximum


def zephyr_particle_is_flat(particle):
    """Mirror stream_incremental_model() assuming enough direct-term counters."""
    if particle is None or particle["kind"] in ("element", "any"):
        return True
    children = particle["children"]
    if any(child["kind"] not in ("element", "any") for child in children):
        return False
    if particle["max"] is not None and particle["max"] <= 1:
        return True
    if particle["kind"] == "sequence" and particle["min"] <= 1:
        return True
    return particle["kind"] == "choice" and all(
        child["min"] == child["max"] == 1 for child in children
    )


def zephyr_particle_is_incremental(particle):
    """Account for both flat counters and the shared continuation interpreter."""
    if zephyr_particle_is_flat(particle):
        return True
    return particle["kind"] in ("sequence", "choice") and all(
        child["kind"] in ("element", "any", "sequence", "choice")
        for child in zephyr_particle_nodes(particle)
    )


def zephyr_depth_envelope(model):
    """Bound element depth, accounting for recursion through dynamic derived types.

    Derivation edges have weight zero (same element); element-type edges have
    weight one. A cycle, wildcard, or anyType leaves the input depth a policy.
    All emitted complex types are included, even unused named declarations.
    """
    types = {key: value for key, value in model["types"].items() if value["kind"] == "complex"}
    edges = {key: {} for key in types}
    depth = dict.fromkeys(types, 1)
    wildcard = False

    def active_terms(particle):
        if particle is None or particle["max"] == 0:
            return
        if particle["kind"] in ("element", "any"):
            yield particle
        else:
            for child in particle["children"]:
                yield from active_terms(child)

    for key, definition in types.items():
        wildcard |= bool(definition.get("any_type"))
        base = definition.get("base_ref")
        if base in types:
            edges[base].setdefault(key, 0)
        for term in active_terms(definition.get("particle")):
            if term["kind"] == "any":
                wildcard = True
                continue
            child_type = term["element"]["type"].get("complex")
            if child_type in types:
                edges[key][child_type] = 1
            else:
                depth[key] = max(depth[key], 2)

    reverse = {key: [] for key in types}
    pending = {key: len(children) for key, children in edges.items()}
    for parent, children in edges.items():
        for child, weight in children.items():
            reverse[child].append((parent, weight))
    ready = [key for key in types if pending[key] == 0]
    visited = 0
    while ready:
        child = ready.pop()
        visited += 1
        for parent, weight in reverse[child]:
            depth[parent] = max(depth[parent], depth[child] + weight)
            pending[parent] -= 1
            if pending[parent] == 0:
                ready.append(parent)
    return max(depth.values(), default=1), visited != len(types), wildcard


def generate_zephyr_config(stem, model, bindings=False):
    """Render a Kconfig fragment and opt-in compile-time structural-limit checks.

    Values depending on document spelling, arbitrary instance declarations,
    recursion budgets, or unbounded repetitions remain commented suggestions.
    The runtime never reads Kconfig or loads schemas at run time.
    """
    complex_types = [t for t in model["types"].values() if t["kind"] == "complex"]
    particles = [node for t in complex_types for node in zephyr_particle_nodes(t.get("particle"))]
    particle_depth = max(
        (zephyr_particle_bounds(t.get("particle"))[0] for t in complex_types), default=1
    )
    counter_count = max(
        [1]
        + [len(t["elements"]) for t in complex_types]
        + [len(p.get("children", [])) for p in particles]
        + [
            sum(p["kind"] in ("element", "any") for p in zephyr_particle_nodes(t.get("particle")))
            for t in complex_types
        ]
    )
    attributes = max((len(t["attributes"]) for t in complex_types), default=0)
    named = sum(bool(t.get("type_name")) for t in model["types"].values())
    registry_count = (
        len(model["order"]) + len(model["global_attributes"]) + len(model["notations"]) + named
    )
    root_complexities = [registry_count]
    for key in model["order"]:
        root = model["globals"][key]
        definition = model["types"].get(root["type"].get("complex"))
        count = registry_count + len(root.get("identities", []))
        if definition is not None:
            count += len(definition["elements"]) + len(definition["attributes"])
            count += sum(1 for _ in zephyr_particle_nodes(definition.get("particle")))
        root_complexities.append(count)

    required = {
        "MAX_PARTICLES": counter_count,
        "MAX_ATTRIBUTES": max(1, attributes),
        "MAX_SCHEMA_COMPLEXITY": max(1, max(root_complexities)),
        "MAX_PATTERN_LENGTH": 1,
        "MAX_REGEX_NODES": 16,
        "MAX_REGEX_DEPTH": 1,
    }
    assigned = dict(required)
    notes = []
    policy = {
        "MAX_INPUT": (
            1048576,
            "XML spelling, comments and whitespace are not bounded by XSD",
        ),
        "MAX_NAMESPACES": (
            16,
            "instances may declare additional or aliased namespace bindings",
        ),
        "MAX_TEXT_LENGTH": (
            1024,
            "lexical whitespace and unconstrained values need a byte budget",
        ),
        "XML_TOKEN_CAPACITY": (
            4096,
            "raw tags, prefixes, entities, comments and CDATA need a budget",
        ),
    }
    depth, recursive, wildcard = zephyr_depth_envelope(model)
    required["MAX_DEPTH"] = max(1, particle_depth)
    if recursive or wildcard:
        reason = ", ".join(
            text
            for enabled, text in (
                (recursive, "recursive type graph"),
                (wildcard, "wildcard/anyType"),
            )
            if enabled
        )
        policy["MAX_DEPTH"] = (
            max(32, particle_depth),
            reason + "; choose an application depth",
        )
    else:
        required["MAX_DEPTH"] = assigned["MAX_DEPTH"] = max(1, depth, particle_depth)
        notes.append("Depth covers all emitted types, dynamic derivations and descriptor nesting.")

    history_minimum = history_maximum = 1
    history_unbounded = False
    for definition in complex_types:
        particle = definition.get("particle")
        if zephyr_particle_is_incremental(particle):
            continue
        _, minimum, maximum = zephyr_particle_bounds(particle)
        history_minimum = max(history_minimum, minimum)
        if maximum is None:
            history_unbounded = True
        else:
            history_maximum = max(history_maximum, maximum)
    required["STREAM_CHILD_CAPACITY"] = history_minimum
    if history_unbounded or history_maximum > 65536:
        reason = "unbounded" if history_unbounded else "larger than Zephyr's 65536-token limit"
        policy["STREAM_CHILD_CAPACITY"] = (
            max(64, history_minimum),
            reason + " fallback history; choose an accepted-document child budget",
        )
    else:
        assigned["STREAM_CHILD_CAPACITY"] = history_maximum
    notes.append("Incremental flat models do not consume child-history capacity.")
    notes.append("Attribute minima cover declarations; allow headroom for xsi:* and wildcards.")
    nested = any(not zephyr_particle_is_flat(t.get("particle")) for t in complex_types)
    if nested:
        required["STREAM_MODEL_CAPACITY"] = 1
        policy["STREAM_MODEL_CAPACITY"] = (
            max(128, 2 * particle_depth + 4),
            "shared continuation pool: budget active nesting and alternative repetition partitions",
        )
        notes.append("Nested sequences/choices need no child history with the model pool enabled.")
    else:
        assigned["STREAM_MODEL_CAPACITY"] = 0
        notes.append("All emitted models use flat counters; the shared model pool can be disabled.")

    patterns = set()
    text_floor = 1
    length_hint = 0
    for definition in model["types"].values():
        if definition["kind"] != "simple":
            continue
        patterns.update(definition.get("patterns", []))
        facets = definition["facets"]
        if "pattern" in facets:
            patterns.add(facets["pattern"])
        text_floor = max(text_floor, facets.get("length", 0), facets.get("minLength", 0))
        length_hint = max(length_hint, facets.get("length", 0), facets.get("maxLength", 0))
        literals = definition.get("enums", []) + [
            str(facets[key])
            for key in ("minInclusive", "minExclusive", "maxInclusive", "maxExclusive")
            if key in facets
        ]
        text_floor = max([text_floor] + [len(value.encode("utf-8")) for value in literals])
    declarations = list(model["globals"].values()) + model["global_attributes"]
    declarations += [element for t in complex_types for element in t["elements"]]
    declarations += [attribute for t in complex_types for attribute in t["attributes"]]
    names = [XSD_URI, XSI_URI, XML_URI]
    for declaration in declarations:
        names.extend([declaration.get("name") or "", declaration.get("ns") or ""])
        text_floor = max(text_floor, len((declaration.get("ns") or "").encode("utf-8")))
    for definition in model["types"].values():
        names.extend([definition.get("type_name") or "", definition.get("type_ns") or ""])
    required["STREAM_NAME_CAPACITY"] = assigned["STREAM_NAME_CAPACITY"] = (
        max(len(name.encode("utf-8")) for name in names) + 1
    )
    notes.append(
        "Name storage includes NUL and standard XML/XSD/xsi URIs; budget extra-long prefixes."
    )
    for declaration in declarations:
        for key in ("default", "fixed"):
            value = declaration.get(key)
            if value is not None:
                text_floor = max(text_floor, len(value.encode("utf-8")))
    required["MAX_TEXT_LENGTH"] = text_floor
    policy["MAX_TEXT_LENGTH"] = (
        max(1024, text_floor),
        policy["MAX_TEXT_LENGTH"][1] + "; schema-only lower bound %d bytes" % text_floor,
    )
    if length_hint:
        notes.append(
            "Largest length/maxLength facet: %d character/item/decoded-byte units, not raw XML bytes."
            % length_hint
        )
    if patterns:
        resources = []
        for pattern in sorted(patterns):
            resource = PatternResources(pattern)
            resource.parse()
            resources.append(resource)
        required["MAX_PATTERN_LENGTH"] = assigned["MAX_PATTERN_LENGTH"] = max(
            1, max(len(pattern.encode("utf-8")) for pattern in patterns)
        )
        required["MAX_REGEX_NODES"] = assigned["MAX_REGEX_NODES"] = max(
            16, max(resource.nodes for resource in resources)
        )
        required["MAX_REGEX_DEPTH"] = assigned["MAX_REGEX_DEPTH"] = max(
            1, max(resource.maximum_depth for resource in resources)
        )
        notes.append(
            "Regex AST sizing accounts for collapsed empty alternatives and UTF-8 patterns."
        )

    identities = [model["globals"][key].get("identities", []) for key in model["order"]]
    if any(identities):
        required["MAX_IDENTITY_CONSTRAINTS"] = max(map(len, identities))
        constraints = [identity for group in identities for identity in group]
        required["MAX_IDENTITY_FIELDS"] = max(len(identity["fields"]) for identity in constraints)
        paths = []
        for identity in constraints:
            paths.extend([identity["scope"], identity["selector"], *identity["fields"]])
        for path in list(paths):
            paths.extend(path.get("alternatives", []))
        required["MAX_XPATH_DEPTH"] = max(1, max(len(path["steps"]) for path in paths))
        required["MAX_XPATH_ALTERNATIVES"] = max(
            1, max(len(path.get("alternatives", [])) for path in paths)
        )
        policy["MAX_IDENTITY_VALUES"] = (
            64,
            "tuple counts depend on the accepted input document",
        )
        notes.append("Identity constraints require caller-owned scratch allocation with NO_HEAP.")

    # Keep these ranges aligned with zephyr/Kconfig. Never silently clamp a
    # derived requirement to a smaller supported value.
    maxima = {
        "MAX_PARTICLES": 256,
        "MAX_ATTRIBUTES": 256,
        "MAX_SCHEMA_COMPLEXITY": 1048576,
        "MAX_DEPTH": 256,
        "STREAM_CHILD_CAPACITY": 65536,
        "STREAM_MODEL_CAPACITY": 4096,
        "MAX_TEXT_LENGTH": 1048576,
        "MAX_PATTERN_LENGTH": 4096,
        "MAX_REGEX_NODES": 4096,
        "MAX_REGEX_DEPTH": 256,
        "STREAM_NAME_CAPACITY": 4096,
    }
    for name, maximum in maxima.items():
        value = max(required.get(name, 1), assigned.get(name, 1))
        if value > maximum:
            raise SchemaError(
                "Zephyr CONFIG_MXSD_%s needs %d, exceeding its Kconfig maximum %d"
                % (name, value, maximum)
            )

    lines = [
        "# Generated by mxsdc for %s. Do not edit; copy and review for your application." % stem,
        "# Merge before Zephyr's Kconfig phase, e.g. with -DEXTRA_CONF_FILE=<this file>.",
        "# Structural minima are not a guarantee of accepting every XSD-valid XML spelling.",
        "CONFIG_MXSD=y",
    ]
    if bindings:
        lines.append("CONFIG_MXSD_BINDINGS=y")
    lines += ["", "# Schema-derived descriptor requirements and finite envelopes."]
    lines += ["CONFIG_MXSD_%s=%d" % item for item in sorted(assigned.items())]
    lines += [
        "",
        "# Application policies: uncomment and adjust; these are not XSD-derived bounds.",
    ]
    for name, (suggestion, reason) in sorted(policy.items()):
        lines += ["# " + reason, "# CONFIG_MXSD_%s=%d" % (name, suggestion)]
    lines += ["", *["# " + note for note in notes]]
    for name in sorted(set(required) - set(maxima)):
        lines.append(
            "# Set C compile definition MXSD_%s >= %d (no Kconfig option)." % (name, required[name])
        )

    guard = ident(stem).upper() + "_MXSD_LIMITS_H"
    header = [
        "/* Generated by mxsdc. Structural limits only; document budgets remain application policy. */",
        "#ifndef " + guard,
        "#define " + guard,
        "#include <mxsd.h>",
    ]
    for name, value in sorted(required.items()):
        header += [
            "#if MXSD_%s < %du" % (name, value),
            '#error "%s requires MXSD_%s >= %d"' % (stem, name, value),
            "#endif",
        ]
    header += ["#endif", ""]
    return "\n".join(lines) + "\n", "\n".join(header)
