#!/usr/bin/env python3
"""Inventory and execute the official W3C XML Schema 1.0 test suite."""

import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from urllib.parse import unquote, urlparse

import report as w3c_report

TS = "{http://www.w3.org/XML/2004/xml-schema-test-suite/}"
XLINK = "{http://www.w3.org/1999/xlink}href"
XSI = "{http://www.w3.org/2001/XMLSchema-instance}"
XS_SCHEMA = "{http://www.w3.org/2001/XMLSchema}schema"
META_PATTERNS = ("*Meta/*.testSet", "msMeta/*_w3c.xml")
RUNTIME_SOURCES = (
    "mxsd.c",
    "mxsd_dom.c",
    "mxsd_identity.c",
    "mxsd_regex.c",
    "mxsd_stream.c",
    "mxsd_unicode.c",
    "mxsd_value.c",
    "mxsd_xml_builtin.c",
)
RUNTIME_DEFINES = (
    "-DMXSD_MAX_ATTRIBUTES=4096",
    "-DMXSD_MAX_TEXT_LENGTH=262144",
    "-DMXSD_MAX_REGEX_NODES=4096",
)
RUNNER_BATCH = 256
# Reviewed accepted cases whose published expectation encodes one processor's
# implementation limit rather than an XSD 1.0 rule. Each entry gives the result
# the Recommendation requires; a case is reclassified as skipped only when
# mxsd produces exactly that result, so a regression still reports a failure.
_Z036_REASON = (
    "disputed suite expectation: the documentation of this case says that the "
    "Microsoft engine validates a particle as xs:any when maxOccurs exceeds 4096. "
    "XSD 1.0 defines no such limit; the instance interleaves a* runs and b "
    "elements within choice maxOccurs=100000, which particlesZ036_b.xsd accepts"
)
DISPUTED_CASES = {
    "Microsoft/MS-Particles2006-07-15/particlesZ036_b1/particlesZ036_b1.i": (
        "valid",
        _Z036_REASON,
    ),
    "Microsoft/MS-Particles2006-07-15/particlesZ036_b2/particlesZ036_b2.i": (
        "valid",
        _Z036_REASON,
    ),
}


def local_ref(metadata, value):
    if not value:
        return None
    parsed = urlparse(value)
    if parsed.scheme or parsed.netloc:
        return None
    return (metadata.parent / unquote(parsed.path)).resolve()


def instance_schemas(document):
    """Return local schema hints carried by an XML instance document.

    The second value reports whether a hint names an existing local file that
    is not a well-formed schema document. A processor that honors hints must
    then report an error, which mxsd's static schema model cannot reproduce.
    """
    if document is None:
        return [], False
    try:
        root = ET.parse(document).getroot()
    except (OSError, ET.ParseError):
        return [], False
    hints = []
    for element in root.iter():
        locations = element.get(XSI + "schemaLocation", "").split()
        hints.extend(locations[1::2])
        no_namespace = element.get(XSI + "noNamespaceSchemaLocation")
        if no_namespace:
            hints.append(no_namespace)
    paths = []
    unusable = False
    for hint in hints:
        parsed = urlparse(hint)
        if not parsed.scheme and not parsed.netloc:
            resolved = (document.parent / unquote(parsed.path)).resolve()
            is_schema = resolved.is_file() and resolved.suffix.lower() == ".xsd"
            if resolved.is_file() and not is_schema:
                try:
                    is_schema = ET.parse(resolved).getroot().tag == XS_SCHEMA
                except (OSError, ET.ParseError):
                    is_schema = False
                    unusable = True
            if is_schema:
                path = str(resolved)
                if path not in paths:
                    paths.append(path)
    return paths, unusable


def same_schema_document(left, right):
    """Return whether two suite paths carry byte-identical schema documents."""
    try:
        return pathlib.Path(left).read_bytes() == pathlib.Path(right).read_bytes()
    except OSError:
        return False


def schema_target_namespace(path):
    """Return a local schema document's declared target namespace."""
    try:
        return ET.parse(path).getroot().get("targetNamespace")
    except (OSError, ET.ParseError):
        return None


def inline_documents(document):
    """Extract a single inline schema and its sibling payload for suite cases."""
    if document is None:
        return None
    try:
        root = ET.parse(document).getroot()
    except (OSError, ET.ParseError):
        return None
    schemas = [element for element in root.iter() if element.tag == XS_SCHEMA]
    if len(schemas) != 1:
        return None
    payloads = [child for child in root if child.tag != XS_SCHEMA]
    payload = payloads[0] if schemas[0] in list(root) and len(payloads) == 1 else None
    ET.register_namespace("xsd", "http://www.w3.org/2001/XMLSchema")
    return {
        "schema": ET.tostring(schemas[0], encoding="unicode"),
        "instance": None if payload is None else ET.tostring(payload, encoding="unicode"),
    }


def standalone_schema(document):
    """Create an anyType root declaration for schema-less xsi:type test cases."""
    if document is None:
        return None
    try:
        root = ET.parse(document).getroot()
    except (OSError, ET.ParseError):
        return None
    if not isinstance(root.tag, str):
        return None
    if root.tag.startswith("{"):
        namespace, name = root.tag[1:].split("}", 1)
    else:
        namespace, name = None, root.tag
    schema = ET.Element(XS_SCHEMA)
    if namespace:
        schema.set("targetNamespace", namespace)
        schema.set("elementFormDefault", "qualified")
    ET.SubElement(
        schema, "{http://www.w3.org/2001/XMLSchema}element", {"name": name, "type": "xsd:anyType"}
    )
    ET.register_namespace("xsd", "http://www.w3.org/2001/XMLSchema")
    return ET.tostring(schema, encoding="unicode")


def inventory(suite):
    files = sorted({p for pattern in META_PATTERNS for p in suite.glob(pattern)})
    cases = []
    for metadata in files:
        root = ET.parse(metadata).getroot()
        contributor = root.get("contributor", "unknown")
        for group in root.findall(TS + "testGroup"):
            schema = group.find(TS + "schemaTest")
            schema_docs = (
                []
                if schema is None
                else [
                    local_ref(metadata, n.get(XLINK, ""))
                    for n in schema.findall(TS + "schemaDocument")
                ]
            )
            schema_docs = [str(p) for p in schema_docs if p is not None]
            schema_expected_node = None if schema is None else schema.find(TS + "expected")
            schema_expected = (
                None if schema_expected_node is None else schema_expected_node.get("validity")
            )
            tests = ([] if schema is None else [("schema", schema)]) + [
                ("instance", n) for n in group.findall(TS + "instanceTest")
            ]
            for kind, test in tests:
                expected = test.find(TS + "expected")
                current = test.find(TS + "current")
                document = test.find(TS + "instanceDocument")
                instance_path = (
                    None if document is None else local_ref(metadata, document.get(XLINK, ""))
                )
                effective_schemas = list(schema_docs)
                requires_instance_schema_selection = False
                if kind == "instance":
                    covered_namespaces = {
                        schema_target_namespace(existing) for existing in effective_schemas
                    }
                    hinted_schemas, unusable_hint = instance_schemas(instance_path)
                    requires_instance_schema_selection = unusable_hint
                    for hinted_schema in hinted_schemas:
                        if not any(
                            same_schema_document(hinted_schema, existing)
                            for existing in effective_schemas
                        ):
                            requires_instance_schema_selection = True
                        if (
                            schema_target_namespace(hinted_schema) not in covered_namespaces
                            and hinted_schema not in effective_schemas
                            and not any(
                                same_schema_document(hinted_schema, existing)
                                for existing in effective_schemas
                            )
                        ):
                            effective_schemas.append(hinted_schema)
                            covered_namespaces.add(schema_target_namespace(hinted_schema))
                inline = (
                    inline_documents(instance_path)
                    if kind == "instance" and not effective_schemas
                    else None
                )
                cases.append(
                    {
                        "id": "/".join(
                            (
                                contributor,
                                root.get("name", metadata.stem),
                                group.get("name", ""),
                                test.get("name", kind),
                            )
                        ),
                        "kind": kind,
                        "expected": None if expected is None else expected.get("validity"),
                        "schema_expected": schema_expected,
                        "status": None if current is None else current.get("status"),
                        "schema_documents": effective_schemas,
                        "requires_instance_schema_selection": requires_instance_schema_selection,
                        "inline_documents": inline,
                        "standalone_schema": (
                            standalone_schema(instance_path)
                            if kind == "instance" and not effective_schemas and inline is None
                            else None
                        ),
                        "document": None if instance_path is None else str(instance_path),
                        "metadata": str(metadata),
                    }
                )
    return cases


def run_command(command, timeout, label):
    """Run a command, folding a timeout into exit status 124 with a diagnostic."""
    try:
        return subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", "replace")
        return subprocess.CompletedProcess(command, 124, stdout=output + label + " timed out\n")


def compile_flags(args):
    return ["-std=c99", *RUNTIME_DEFINES, "-I" + str(args.project / "include")]


def build_runtime(args, pool):
    """Compile the runtime once; each generated validator only links against it."""
    directory = args.work_dir / "runtime"
    directory.mkdir(parents=True, exist_ok=True)
    jobs = []
    for name in RUNTIME_SOURCES:
        obj = directory / (pathlib.Path(name).stem + ".o")
        command = [
            args.cc,
            *compile_flags(args),
            "-I" + str(args.project / "src"),
            "-c",
            str(args.project / "src" / name),
            "-o",
            str(obj),
        ]
        jobs.append((obj, pool.submit(run_command, command, args.timeout * 6, "C compilation")))
    objects = []
    for obj, job in jobs:
        process = job.result()
        if process.returncode != 0:
            raise SystemExit("runtime compilation failed:\n" + process.stdout)
        objects.append(str(obj))
    return objects


def classify_schema(result, accepted, log):
    actual_valid = accepted
    passed = actual_valid == (result["expected"] == "valid")
    # A rejection of a W3C-valid schema describes an unsupported mxsd
    # feature, regardless of the diagnostic wording. Accepting a
    # W3C-invalid schema is a genuine conformance failure.
    crashed = log.lstrip().startswith("Traceback (most recent call last):")
    classification = "failed" if crashed else ("passed" if passed else "failed")
    if not passed and not crashed and not actual_valid and result["expected"] == "valid":
        classification = "unsupported"
    result.update(
        classification=classification,
        actual="valid" if actual_valid else "invalid",
        diagnostic=log.strip(),
    )
    if crashed:
        result["reason"] = "mxsdc crashed"


def generate_unit(args, unit, schemas, members, prefix):
    """Generate one schema set, classify what needs no runner, and compile the rest.

    The generated validator is compiled through a wrapper that prefixes its
    exported symbols, so that many validators can share one runner executable.
    """
    unit.mkdir(parents=True, exist_ok=True)
    command = [
        sys.executable,
        str(args.mxsdc),
        *(str(value) for value in schemas),
        "-o",
        str(unit),
        "--max-schema-complexity",
        str(args.max_schema_complexity),
        "--allow-unresolved-schema-locations",
    ]
    process = run_command(command, args.timeout, "mxsdc")
    accepted = process.returncode == 0
    log = process.stdout
    pending = []
    for position, (result, document) in enumerate(members):
        if accepted and "warning: schema location" in log:
            result["has_unresolved_schema_location"] = True
        if result["kind"] == "schema":
            classify_schema(result, accepted, log)
        elif not accepted:
            crashed = log.lstrip().startswith("Traceback (most recent call last):")
            explicitly_invalid = log.startswith("error: invalid schema:") or log.startswith(
                "error: malformed XSD:"
            )
            if crashed:
                result.update(
                    classification="failed",
                    actual="error",
                    reason="mxsdc crashed",
                    diagnostic=log.strip(),
                )
            elif result["expected"] == "invalid" and (
                result.get("schema_expected") == "invalid"
                or result.get("inline_documents") is not None
                or explicitly_invalid
            ):
                result.update(
                    classification="passed",
                    actual="invalid",
                    reason="the test group's invalid schema was correctly rejected",
                    diagnostic=log.strip(),
                )
            else:
                result.update(
                    classification="unsupported",
                    reason="schema was not compiled",
                    diagnostic=log.strip(),
                )
        else:
            pending.append(position)
    state = {"members": [result for result, _ in members], "pending": pending, "object": None}
    if not pending:
        return state
    stem = re.sub(r"[^A-Za-z0-9_]", "_", schemas[0].stem)
    if stem[:1].isdigit():
        stem = "_" + stem
    header = (unit / (stem + ".mxsd.h")).read_text(encoding="utf-8")
    symbols = sorted(set(re.findall(r"\b(" + re.escape(stem) + r"_\w+)\s*\(", header)))
    wrapper = unit / "validator.c"
    wrapper.write_text(
        "".join("#define %s %s%s\n" % (symbol, prefix, symbol) for symbol in symbols)
        + '#include "%s.mxsd.c"\n' % stem,
        encoding="utf-8",
    )
    obj = unit / "validator.o"
    command = [args.cc, *compile_flags(args), "-I" + str(unit), "-c", str(wrapper), "-o", str(obj)]
    process = run_command(command, args.timeout, "C compilation")
    if process.returncode == 0:
        state["object"] = str(obj)
        state["symbol"] = prefix + stem + "_validate_document"
    else:
        for position in pending:
            state["members"][position].update(
                classification="failed",
                reason="generated C did not compile",
                diagnostic=process.stdout.strip(),
            )
        state["pending"] = []
    return state


def link_runner(args, template, directory, entries, runtime_objects):
    """Link generated validators into one runner that dispatches on an index."""
    directory.mkdir(parents=True, exist_ok=True)
    source = directory / "runner.c"
    source.write_text(
        template.replace(
            "/* @DECLARATIONS@ */\n",
            "".join("mxsd_result_t %s(const char *xml);\n" % symbol for _, symbol in entries),
        ).replace("\t/* @VALIDATORS@ */\n", "".join("\t%s,\n" % symbol for _, symbol in entries)),
        encoding="utf-8",
    )
    executable = directory / ("runner.exe" if sys.platform == "win32" else "runner")
    command = [
        args.cc,
        *compile_flags(args),
        str(source),
        *(obj for obj, _ in entries),
        *runtime_objects,
        "-o",
        str(executable),
    ]
    process = run_command(command, args.timeout * 6, "C linking")
    return (str(executable) if process.returncode == 0 else None), process.stdout


def run_documents(args, executable, index, members, pending):
    """Validate every pending instance document of one schema set."""
    for position in pending:
        result = members[position]
        document = result["document_path"]
        if not document or not pathlib.Path(document).is_file():
            result.update(classification="skipped", reason="missing local instance document")
            continue
        try:
            process = subprocess.run(
                [executable, str(index), document],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=args.timeout,
            )
        except subprocess.TimeoutExpired as error:
            output = error.stdout or ""
            if isinstance(output, bytes):
                output = output.decode("utf-8", "replace")
            result.update(
                classification="failed",
                reason="validator timed out",
                diagnostic=output.strip(),
            )
            continue
        if process.returncode not in (0, 1):
            result.update(
                classification="failed",
                reason="validator runner failed",
                diagnostic=process.stdout.strip(),
            )
        else:
            actual_valid = process.returncode == 0
            matches = actual_valid == (result["expected"] == "valid")
            dynamic_schema = result.get("requires_instance_schema_selection") or result.get(
                "has_unresolved_schema_location"
            )
            result.update(
                classification=(
                    "passed" if matches else "unsupported" if dynamic_schema else "failed"
                ),
                actual="valid" if actual_valid else "invalid",
                diagnostic=process.stdout.strip(),
            )
            if not matches and dynamic_schema:
                result["reason"] = "instance-driven schema discovery is unsupported"
    return members


def progress(label, done, total):
    if done % 1000 == 0 or done == total:
        print("w3c: %s %d/%d" % (label, done, total), file=sys.stderr, flush=True)


def execute(args, cases):
    args.work_dir.mkdir(parents=True, exist_ok=True)
    template = (pathlib.Path(__file__).parent / "runner.c.in").read_text(encoding="utf-8")
    # Cases sharing a schema set are grouped into one unit, numbered in order of
    # first appearance so that work directories stay deterministic.
    units = {}
    results = []
    selected = 0
    for case in cases:
        result = dict(case)
        if args.status != "all" and case["status"] != args.status:
            result.update(classification="skipped", reason="status filter")
            results.append(result)
            continue
        if selected < args.offset:
            selected += 1
            result.update(classification="skipped", reason="offset")
            results.append(result)
            continue
        if args.limit is not None and selected - args.offset >= args.limit:
            result.update(classification="skipped", reason="limit")
            results.append(result)
            continue
        selected += 1
        schema_documents = list(case["schema_documents"])
        document = case["document"]
        if not schema_documents and case.get("inline_documents"):
            inline_dir = args.work_dir / ("inline-" + str(selected - 1))
            inline_dir.mkdir(parents=True, exist_ok=True)
            inline_schema = inline_dir / "inline.xsd"
            inline_instance = inline_dir / "instance.xml"
            inline_schema.write_text(case["inline_documents"]["schema"], encoding="utf-8")
            if case["inline_documents"]["instance"] is not None:
                inline_instance.write_text(case["inline_documents"]["instance"], encoding="utf-8")
            schema_documents.append(str(inline_schema))
            if case["inline_documents"]["instance"] is not None:
                document = str(inline_instance)
        elif not schema_documents and case.get("standalone_schema"):
            standalone_dir = args.work_dir / ("standalone-" + str(selected - 1))
            standalone_dir.mkdir(parents=True, exist_ok=True)
            synthetic_schema = standalone_dir / "standalone.xsd"
            synthetic_schema.write_text(case["standalone_schema"], encoding="utf-8")
            schema_documents.append(str(synthetic_schema))
        if not schema_documents:
            result.update(
                classification="unsupported", reason="standalone instance has no schema document"
            )
            results.append(result)
            continue
        key = tuple(schema_documents)
        if key not in units:
            units[key] = []
        units[key].append((len(results), result, document))
        results.append(result)
    if not units:
        return results
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        runtime_objects = build_runtime(args, pool)
    # Worker processes stay small, so spawning compilers and validators from
    # them is not serialized behind forks of this large inventory process.
    with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs) as pool:
        keys = list(units)
        jobs = {
            pool.submit(
                generate_unit,
                args,
                args.work_dir / ("case-" + str(index)),
                tuple(pathlib.Path(value) for value in key),
                [(result, document) for _, result, document in units[key]],
                "mxsdw%d_" % index,
            ): index
            for index, key in enumerate(keys)
        }
        states = [None] * len(keys)
        for done, job in enumerate(concurrent.futures.as_completed(jobs), 1):
            states[jobs[job]] = job.result()
            progress("generated", done, len(jobs))
        # Each new executable pays a first-launch cost (notably macOS code
        # scanning), so validators are linked in batches rather than one per
        # schema set.
        runnable = [index for index, state in enumerate(states) if state["pending"]]
        batches = [
            runnable[first : first + RUNNER_BATCH]
            for first in range(0, len(runnable), RUNNER_BATCH)
        ]
        jobs = {}
        for number, batch in enumerate(batches):
            entries = [(states[index]["object"], states[index]["symbol"]) for index in batch]
            directory = args.work_dir / ("runner-" + str(number))
            jobs[pool.submit(link_runner, args, template, directory, entries, runtime_objects)] = [
                (index, position) for position, index in enumerate(batch)
            ]
        runners = {}
        for job in concurrent.futures.as_completed(jobs):
            executable, log = job.result()
            for index, position in jobs[job]:
                runners[index] = (executable, position, log)
        # A failed batch link is retried per schema set to attribute the error.
        for index, (executable, _, _) in list(runners.items()):
            if executable is None:
                state = states[index]
                executable, log = link_runner(
                    args,
                    template,
                    args.work_dir / ("case-" + str(index)),
                    [(state["object"], state["symbol"])],
                    runtime_objects,
                )
                runners[index] = (executable, 0, log)
        jobs = {}
        for index in runnable:
            state = states[index]
            executable, position, log = runners[index]
            if executable is None:
                for pending in state["pending"]:
                    state["members"][pending].update(
                        classification="failed",
                        reason="generated C did not link",
                        diagnostic=log.strip(),
                    )
                continue
            for pending in state["pending"]:
                state["members"][pending]["document_path"] = units[keys[index]][pending][2]
            job = pool.submit(
                run_documents, args, executable, position, state["members"], state["pending"]
            )
            jobs[job] = index
        for done, job in enumerate(concurrent.futures.as_completed(jobs), 1):
            states[jobs[job]]["members"] = job.result()
            progress("validated", done, len(jobs))
    for index, key in enumerate(keys):
        for (position, _, _), result in zip(units[key], states[index]["members"]):
            result.pop("document_path", None)
            results[position] = result
    return results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--suite", required=True, type=pathlib.Path)
    parser.add_argument(
        "--project", default=pathlib.Path(__file__).resolve().parents[2], type=pathlib.Path
    )
    parser.add_argument("--mxsdc", type=pathlib.Path)
    parser.add_argument("--work-dir", type=pathlib.Path, default=pathlib.Path("build/w3c"))
    parser.add_argument(
        "--output", type=pathlib.Path, default=pathlib.Path("w3c-xsd10-report.json")
    )
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--status", default="accepted", choices=("accepted", "all"))
    parser.add_argument("--limit", type=int)
    parser.add_argument("--offset", type=int, default=0)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument(
        "--jobs", type=int, default=os.cpu_count() or 1, help="parallel schema sets to process"
    )
    parser.add_argument("--max-schema-complexity", type=int, default=65536)
    parser.add_argument(
        "--expected-failures", type=pathlib.Path, help="JSON array of exact W3C case identifiers"
    )
    parser.add_argument("--inventory-only", action="store_true")
    parser.add_argument("--html", type=pathlib.Path, help="also write a readable HTML report")
    args = parser.parse_args()
    args.project = args.project.resolve()
    args.mxsdc = (args.mxsdc or args.project / "mxsdc").resolve()
    cases = inventory(args.suite.resolve())
    results = cases if args.inventory_only else execute(args, cases)
    for result in results:
        disputed = DISPUTED_CASES.get(result["id"])
        if (
            disputed is not None
            and result.get("classification") == "failed"
            and result.get("actual") == disputed[0]
        ):
            result.update(classification="skipped", reason=disputed[1])
    expected_failures = set()
    if args.expected_failures:
        expected_failures = set(json.loads(args.expected_failures.read_text(encoding="utf-8")))
        for result in results:
            if result.get("classification") == "failed" and result["id"] in expected_failures:
                result["classification"] = "expected-failure"
    categories = ("passed", "failed", "unsupported", "expected-failure", "skipped")
    counts = {
        name: sum(1 for result in results if result.get("classification") == name)
        for name in categories
    }
    report = {
        "suite": "W3C XML Schema 1.0 Test Suite xsts-2007-06-20",
        "inventory_count": len(cases),
        "counts": counts,
        "cases": results,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.html:
        args.html.parent.mkdir(parents=True, exist_ok=True)
        args.html.write_text(w3c_report.render(report), encoding="utf-8")
    print(json.dumps({"inventory_count": len(cases), "counts": counts}, sort_keys=True))
    return 1 if not args.inventory_only and counts["failed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
