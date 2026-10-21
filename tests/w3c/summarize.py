#!/usr/bin/env python3
"""Summarize actionable gaps from an mxsd W3C XSD report."""

import argparse
import collections
import json
import pathlib
import re

ATTENTION = ("failed", "unsupported", "expected-failure")


def first_line(value):
    """Return the first non-empty diagnostic line."""
    return next((line.strip() for line in (value or "").splitlines() if line.strip()), "unknown")


def diagnostic_label(case):
    """Return a useful label even when a validation mismatch has no error text."""
    lines = [line.strip() for line in (case.get("diagnostic") or "").splitlines() if line.strip()]
    diagnostic = lines[0] if lines else "unknown"
    if diagnostic == "Traceback (most recent call last):" and lines:
        diagnostic = lines[-1]
    if diagnostic != "unknown":
        return re.sub(r" at offset [0-9]+", " at offset N", diagnostic)
    expected = case.get("expected")
    actual = case.get("actual")
    if expected and actual:
        return "expected %s, got %s" % (expected, actual)
    return case.get("reason") or "unknown"


def analyze(report, limit):
    """Build stable aggregate counters for the report's actionable cases."""
    cases = report.get("cases", [])
    by_status = {}
    for status in ATTENTION:
        selected = [case for case in cases if case.get("classification") == status]
        reasons = collections.Counter(case.get("reason") or "unspecified" for case in selected)
        diagnostics = collections.Counter(diagnostic_label(case) for case in selected)
        by_status[status] = {
            "count": len(selected),
            "top_reasons": reasons.most_common(limit),
            "top_diagnostics": diagnostics.most_common(limit),
        }

    groups = collections.defaultdict(collections.Counter)
    for case in cases:
        group = "/".join(case.get("id", "unknown").split("/")[:2])
        groups[group][case.get("classification", "unclassified")] += 1
    weakest = []
    for group, counts in groups.items():
        executed = sum(counts[name] for name in ATTENTION) + counts["passed"]
        if executed:
            weakest.append(
                {
                    "test_set": group,
                    "executed": executed,
                    "passed": counts["passed"],
                    "attention": sum(counts[name] for name in ATTENTION),
                    "pass_rate": counts["passed"] / executed,
                }
            )
    weakest.sort(key=lambda row: (row["pass_rate"], -row["executed"], row["test_set"]))
    return {
        "suite": report.get("suite"),
        "inventory_count": report.get("inventory_count", len(cases)),
        "counts": report.get("counts", {}),
        "by_status": by_status,
        "weakest_test_sets": weakest[:limit],
    }


def markdown(summary):
    """Render a concise GitHub summary without claiming unsupported cases pass."""
    counts = summary["counts"]
    lines = [
        "## W3C XSD 1.0 gap analysis",
        "",
        "- Inventory: %d" % summary["inventory_count"],
        "- Counts: `%s`" % json.dumps(counts, sort_keys=True),
    ]
    for status in ATTENTION:
        section = summary["by_status"][status]
        if not section["count"]:
            continue
        lines.extend(("", "### %s (%d)" % (status, section["count"]), "", "Top diagnostics:"))
        lines.extend("- %d × `%s`" % (count, value) for value, count in section["top_diagnostics"])
    if summary["weakest_test_sets"]:
        lines.extend(
            (
                "",
                "### Weakest test sets",
                "",
                "| Test set | Passed | Needs attention | Rate |",
                "|---|---:|---:|---:|",
            )
        )
        for row in summary["weakest_test_sets"]:
            lines.append(
                "| %s | %d | %d | %.1f%% |"
                % (row["test_set"], row["passed"], row["attention"], 100 * row["pass_rate"])
            )
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=pathlib.Path)
    parser.add_argument("--json", required=True, type=pathlib.Path)
    parser.add_argument("--markdown", required=True, type=pathlib.Path)
    parser.add_argument("--limit", type=int, default=10)
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding="utf-8"))
    summary = analyze(report, args.limit)
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.markdown.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    args.markdown.write_text(markdown(summary), encoding="utf-8")


if __name__ == "__main__":
    main()
