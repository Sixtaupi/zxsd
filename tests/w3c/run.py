#!/usr/bin/env python3
"""Inventory and execute the official W3C XML Schema 1.0 test suite."""

import argparse
import json
import pathlib
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from urllib.parse import unquote, urlparse

TS = "{http://www.w3.org/XML/2004/xml-schema-test-suite/}"
XLINK = "{http://www.w3.org/1999/xlink}href"
XSI = "{http://www.w3.org/2001/XMLSchema-instance}"
META_PATTERNS = ("*Meta/*.testSet", "msMeta/*_w3c.xml")


def local_ref(metadata, value):
    if not value:
        return None
    parsed = urlparse(value)
    if parsed.scheme or parsed.netloc:
        return None
    return (metadata.parent / unquote(parsed.path)).resolve()


def instance_schemas(document):
    """Return local schema hints carried by an XML instance document."""
    if document is None:
        return []
    try:
        root = ET.parse(document).getroot()
    except (OSError, ET.ParseError):
        return []
    locations = root.get(XSI + "schemaLocation", "").split()
    hints = locations[1::2]
    no_namespace = root.get(XSI + "noNamespaceSchemaLocation")
    if no_namespace:
        hints.append(no_namespace)
    paths = []
    for hint in hints:
        parsed = urlparse(hint)
        if not parsed.scheme and not parsed.netloc:
            paths.append(str((document.parent / unquote(parsed.path)).resolve()))
    return paths


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
                effective_schemas = schema_docs or (
                    instance_schemas(instance_path) if kind == "instance" else []
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
                        "status": None if current is None else current.get("status"),
                        "schema_documents": effective_schemas,
                        "document": None if instance_path is None else str(instance_path),
                        "metadata": str(metadata),
                    }
                )
    return cases


def execute(args, cases):
    args.work_dir.mkdir(parents=True, exist_ok=True)
    template = (pathlib.Path(__file__).parent / "runner.c.in").read_text(encoding="utf-8")
    compiled = {}
    results = []
    selected = 0
    for case in cases:
        result = dict(case)
        if args.status != "all" and case["status"] != args.status:
            result.update(classification="skipped", reason="status filter")
            results.append(result)
            continue
        if args.limit is not None and selected >= args.limit:
            result.update(classification="skipped", reason="limit")
            results.append(result)
            continue
        selected += 1
        if not case["schema_documents"]:
            result.update(
                classification="unsupported", reason="standalone instance has no schema document"
            )
            results.append(result)
            continue
        schema = pathlib.Path(case["schema_documents"][0])
        key = str(schema)
        if key not in compiled:
            unit = args.work_dir / ("case-" + str(len(compiled)))
            unit.mkdir(parents=True, exist_ok=True)
            command = [
                sys.executable,
                str(args.mxsdc),
                str(schema),
                "-o",
                str(unit),
                "--max-schema-complexity",
                str(args.max_schema_complexity),
            ]
            process = subprocess.run(
                command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
            )
            stem = re.sub(r"[^A-Za-z0-9_]", "_", schema.stem)
            if stem[:1].isdigit():
                stem = "_" + stem
            compiled[key] = {
                "accepted": process.returncode == 0,
                "log": process.stdout,
                "unit": unit,
                "stem": stem,
                "executable": None,
            }
        state = compiled[key]
        if case["kind"] == "schema":
            actual_valid = state["accepted"]
            passed = actual_valid == (case["expected"] == "valid")
            # A rejection of a W3C-valid schema describes an unsupported mxsd
            # feature, regardless of the diagnostic wording. Accepting a
            # W3C-invalid schema is a genuine conformance failure.
            classification = (
                "passed"
                if passed
                else (
                    "unsupported" if not actual_valid and case["expected"] == "valid" else "failed"
                )
            )
            result.update(
                classification=classification,
                actual="valid" if actual_valid else "invalid",
                diagnostic=state["log"].strip(),
            )
            results.append(result)
            continue
        if not state["accepted"]:
            result.update(
                classification="unsupported",
                reason="schema was not compiled",
                diagnostic=state["log"].strip(),
            )
            results.append(result)
            continue
        if state["executable"] is None:
            source = state["unit"] / "runner.c"
            source.write_text(
                template.replace("@HEADER@", state["stem"] + ".mxsd.h").replace(
                    "@SYMBOL@", state["stem"]
                ),
                encoding="utf-8",
            )
            executable = state["unit"] / ("runner.exe" if sys.platform == "win32" else "runner")
            runtime_sources = [
                args.project / "src" / name
                for name in (
                    "mxsd.c",
                    "mxsd_dom.c",
                    "mxsd_identity.c",
                    "mxsd_stream.c",
                    "mxsd_value.c",
                    "mxsd_xml_builtin.c",
                )
            ]
            command = (
                [
                    args.cc,
                    "-std=c99",
                    "-I" + str(args.project / "include"),
                    "-I" + str(args.project / "src"),
                    "-I" + str(state["unit"]),
                    str(source),
                    str(state["unit"] / (state["stem"] + ".mxsd.c")),
                ]
                + [str(path) for path in runtime_sources]
                + ["-o", str(executable)]
            )
            process = subprocess.run(
                command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
            )
            state["executable"] = executable if process.returncode == 0 else False
            state["compile_log"] = process.stdout
        if state["executable"] is False:
            result.update(
                classification="failed",
                reason="generated C did not compile",
                diagnostic=state["compile_log"].strip(),
            )
        elif not case["document"] or not pathlib.Path(case["document"]).is_file():
            result.update(classification="skipped", reason="missing local instance document")
        else:
            process = subprocess.run(
                [str(state["executable"]), case["document"]],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            if process.returncode not in (0, 1):
                result.update(
                    classification="failed",
                    reason="validator runner failed",
                    diagnostic=process.stdout.strip(),
                )
            else:
                actual_valid = process.returncode == 0
                result.update(
                    classification=(
                        "passed" if actual_valid == (case["expected"] == "valid") else "failed"
                    ),
                    actual="valid" if actual_valid else "invalid",
                    diagnostic=process.stdout.strip(),
                )
        results.append(result)
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
    parser.add_argument("--max-schema-complexity", type=int, default=65536)
    parser.add_argument(
        "--expected-failures", type=pathlib.Path, help="JSON array of exact W3C case identifiers"
    )
    parser.add_argument("--inventory-only", action="store_true")
    args = parser.parse_args()
    args.project = args.project.resolve()
    args.mxsdc = (args.mxsdc or args.project / "mxsdc").resolve()
    cases = inventory(args.suite.resolve())
    results = cases if args.inventory_only else execute(args, cases)
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
    print(json.dumps({"inventory_count": len(cases), "counts": counts}, sort_keys=True))
    return 1 if not args.inventory_only and counts["failed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
