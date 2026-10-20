#!/usr/bin/env python3
"""Render a W3C conformance JSON report as a standalone HTML page."""

import argparse
import collections
import html
import json
import os
import pathlib

CATEGORIES = ("passed", "failed", "unsupported", "expected-failure", "skipped")
DETAILED = ("failed", "expected-failure", "unsupported")

STYLE = """
:root {
  --bg: #ffffff; --fg: #1f2328; --muted: #656d76; --border: #d0d7de; --panel: #f6f8fa;
  --passed: #1a7f37; --failed: #cf222e; --unsupported: #9a6700;
  --expected-failure: #8250df; --skipped: #6e7781;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #0d1117; --fg: #e6edf3; --muted: #8d96a0; --border: #30363d; --panel: #161b22;
    --passed: #3fb950; --failed: #f85149; --unsupported: #d29922;
    --expected-failure: #a371f7; --skipped: #8d96a0;
  }
}
* { box-sizing: border-box; }
body {
  margin: 0 auto; max-width: 1200px; padding: 24px 16px; background: var(--bg);
  color: var(--fg); font: 14px/1.5 -apple-system, "Segoe UI", Helvetica, Arial, sans-serif;
}
h1 { font-size: 22px; margin: 0 0 4px; }
h2 { font-size: 17px; margin: 32px 0 8px; }
.muted { color: var(--muted); }
.tiles { display: flex; flex-wrap: wrap; gap: 12px; margin: 16px 0; }
.tile {
  flex: 1 1 140px; border: 1px solid var(--border); border-radius: 8px; padding: 10px 14px;
  background: var(--panel);
}
.tile b { display: block; font-size: 24px; font-variant-numeric: tabular-nums; }
.bar { display: flex; height: 10px; border-radius: 5px; overflow: hidden; background: var(--border); }
.bar span { display: block; height: 100%; }
.passed { color: var(--passed); } .failed { color: var(--failed); }
.unsupported { color: var(--unsupported); }
.expected-failure { color: var(--expected-failure); } .skipped { color: var(--skipped); }
.bg-passed { background: var(--passed); } .bg-failed { background: var(--failed); }
.bg-unsupported { background: var(--unsupported); }
.bg-expected-failure { background: var(--expected-failure); }
.bg-skipped { background: var(--skipped); }
.scroll { overflow-x: auto; }
table { border-collapse: collapse; width: 100%; font-variant-numeric: tabular-nums; }
th, td { border-bottom: 1px solid var(--border); padding: 6px 8px; text-align: right; }
th:first-child, td:first-child { text-align: left; }
th { background: var(--panel); position: sticky; top: 0; }
input[type=search] {
  width: 100%; padding: 8px 10px; border: 1px solid var(--border); border-radius: 6px;
  background: var(--bg); color: var(--fg); font: inherit;
}
details.case { border: 1px solid var(--border); border-radius: 6px; margin: 6px 0; }
details.case > summary { cursor: pointer; padding: 6px 10px; overflow-wrap: anywhere; }
details.case > div { padding: 0 10px 10px; }
pre {
  background: var(--panel); border: 1px solid var(--border); border-radius: 6px; padding: 8px;
  overflow-x: auto; white-space: pre-wrap; overflow-wrap: anywhere; margin: 6px 0 0;
}
.tag {
  display: inline-block; font-size: 12px; border: 1px solid currentColor; border-radius: 10px;
  padding: 0 7px; margin-right: 6px;
}
dl { display: grid; grid-template-columns: max-content 1fr; gap: 2px 12px; margin: 6px 0 0; }
dt { color: var(--muted); } dd { margin: 0; overflow-wrap: anywhere; }
"""

SCRIPT = """
const input = document.getElementById("filter");
input.addEventListener("input", () => {
  const terms = input.value.toLowerCase().split(/\\s+/).filter(Boolean);
  for (const node of document.querySelectorAll("details.case")) {
    const text = node.dataset.search;
    node.hidden = !terms.every((term) => text.includes(term));
  }
});
"""


def suite_root(cases):
    """Return the directory holding the metadata folders, used to shorten paths."""
    folders = {str(pathlib.Path(case["metadata"]).parent) for case in cases if case.get("metadata")}
    if not folders:
        return None
    root = os.path.commonpath(sorted(folders))
    return root if len(folders) > 1 else str(pathlib.Path(root).parent)


def shorten(path, root):
    if not path:
        return ""
    if root and path.startswith(root + os.sep):
        return path[len(root) + 1 :]
    return path


def bar(counts, total):
    parts = []
    for name in CATEGORIES:
        if counts.get(name):
            width = 100.0 * counts[name] / total
            parts.append(
                '<span class="bg-%s" style="width:%.3f%%" title="%s: %d"></span>'
                % (name, width, name, counts[name])
            )
    return '<div class="bar">%s</div>' % "".join(parts)


def render_case(case, root):
    name = case.get("classification", "")
    fields = [
        ("Kind", case.get("kind")),
        ("Expected", case.get("expected")),
        ("Actual", case.get("actual")),
        ("Reason", case.get("reason")),
        ("Instance", shorten(case.get("document"), root)),
        ("Schemas", ", ".join(shorten(path, root) for path in case.get("schema_documents", []))),
    ]
    rows = "".join(
        "<dt>%s</dt><dd>%s</dd>" % (label, html.escape(str(value)))
        for label, value in fields
        if value
    )
    diagnostic = case.get("diagnostic") or ""
    search = " ".join(
        [
            case["id"],
            name,
            case.get("reason") or "",
            diagnostic.splitlines()[0] if diagnostic else "",
        ]
    ).lower()
    return (
        '<details class="case" data-search="%s"><summary><span class="tag %s">%s</span>%s</summary>'
        "<div><dl>%s</dl>%s</div></details>"
        % (
            html.escape(search),
            name,
            html.escape(name),
            html.escape(case["id"]),
            rows,
            "<pre>%s</pre>" % html.escape(diagnostic) if diagnostic else "",
        )
    )


def render(report):
    """Return the HTML page for a report produced by run.py."""
    cases = report["cases"]
    counts = report["counts"]
    total = report["inventory_count"] or 1
    root = suite_root(cases)
    executed = counts["passed"] + counts["failed"] + counts["unsupported"]
    executed += counts["expected-failure"]
    out = [
        "<!doctype html><html lang=en><head><meta charset=utf-8>",
        '<meta name="viewport" content="width=device-width, initial-scale=1">',
        "<title>W3C XSD Conformance</title><style>%s</style></head><body>" % STYLE,
        "<h1>W3C XSD 1.0 conformance</h1>",
        '<div class="muted">%s &middot; %d cases inventoried &middot; %d executed</div>'
        % (html.escape(report.get("suite", "")), report["inventory_count"], executed),
        '<div class="tiles">',
    ]
    for name in CATEGORIES:
        out.append(
            '<div class="tile"><span class="%s">%s</span><b>%d</b>'
            '<span class="muted">%.2f%%</span></div>'
            % (name, name, counts.get(name, 0), 100.0 * counts.get(name, 0) / total)
        )
    out.append("</div>")
    out.append(bar(counts, total))

    groups = collections.OrderedDict()
    for case in cases:
        key = "/".join(case["id"].split("/")[:2])
        groups.setdefault(key, collections.Counter())[case.get("classification")] += 1
    out.append("<h2>By test set</h2><div class=scroll><table><thead><tr><th>Test set</th>")
    out.extend("<th>%s</th>" % name for name in CATEGORIES)
    out.append("<th>Pass rate</th><th style='width:20%'></th></tr></thead><tbody>")
    for key in sorted(groups):
        group = groups[key]
        ran = sum(group[name] for name in CATEGORIES if name != "skipped")
        rate = "%.1f%%" % (100.0 * group["passed"] / ran) if ran else "&ndash;"
        out.append("<tr><td>%s</td>" % html.escape(key))
        out.extend(
            '<td class="%s">%s</td>' % (name if group[name] else "muted", group[name] or "")
            for name in CATEGORIES
        )
        out.append("<td>%s</td><td>%s</td></tr>" % (rate, bar(group, sum(group.values()))))
    out.append("</tbody></table></div>")

    skipped = collections.Counter(
        case.get("reason") or "unknown" for case in cases if case.get("classification") == "skipped"
    )
    if skipped:
        out.append("<h2>Skipped</h2><div class=scroll><table><thead><tr><th>Reason</th>")
        out.append("<th>Cases</th></tr></thead><tbody>")
        for reason, count in skipped.most_common():
            out.append("<tr><td>%s</td><td>%d</td></tr>" % (html.escape(reason), count))
        out.append("</tbody></table></div>")

    out.append("<h2>Cases needing attention</h2>")
    out.append('<input id="filter" type="search" placeholder="Filter by id, status, reason...">')
    for name in DETAILED:
        selected = [case for case in cases if case.get("classification") == name]
        if not selected:
            continue
        out.append('<h2 class="%s">%s (%d)</h2>' % (name, name, len(selected)))
        out.extend(render_case(case, root) for case in selected)
    out.append("<script>%s</script></body></html>\n" % SCRIPT)
    return "".join(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=pathlib.Path, help="JSON report written by run.py")
    parser.add_argument("-o", "--output", type=pathlib.Path, help="HTML file to write")
    args = parser.parse_args()
    output = args.output or args.report.with_suffix(".html")
    report = json.loads(args.report.read_text(encoding="utf-8"))
    output.write_text(render(report), encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
