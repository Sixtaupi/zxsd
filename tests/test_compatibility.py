#!/usr/bin/env python3
"""Exercise generated ABI checks with real compilation and link failures."""

import argparse
import json
import pathlib
import re
import subprocess
import sys
import tempfile


def run(command, success=True, diagnostic=None):
    """Require the expected exit status, optionally checking a failure's cause."""
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if (result.returncode == 0) != success or (diagnostic and diagnostic not in result.stdout):
        raise AssertionError("%r\n%s" % (command, result.stdout))
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True, type=pathlib.Path)
    parser.add_argument("--build", required=True, type=pathlib.Path)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--compiler-id", required=True)
    parser.add_argument("--cmake", required=True)
    args = parser.parse_args()
    compiler = [sys.executable, str(args.project / "mxsdc")]
    info = json.loads(run(compiler + ["--abi-info"]))
    version_header = (args.project / "include/mxsd_version.h").read_text(encoding="utf-8")
    cmake = (args.project / "CMakeLists.txt").read_text(encoding="utf-8")
    version = re.search(r"project\(mxsd VERSION ([0-9.]+)", cmake).group(1)
    assert info["compiler_version"] == version
    assert run(compiler + ["--version"]).strip() == "mxsdc " + version
    assert re.search(r'^#define MXSD_VERSION\s+"%s"$' % re.escape(version), version_header, re.M)
    for kind in ("runtime", "descriptor", "binding"):
        assert re.search(
            r"^#define MXSD_%s_ABI_VERSION\s+%d$" % (kind.upper(), info[kind]),
            version_header,
            re.M,
        )

    # A new resource macro must not silently escape the configuration stamp.
    public = (args.project / "include/mxsd.h").read_text(encoding="utf-8")
    limits = re.findall(r"^#ifndef (MXSD_(?:MAX_|STREAM_|XML_)\w+)", public, re.M)
    initializer = public.split("#define MXSD_RUNTIME_CONFIG_INIT", 1)[1].split("/**", 1)[0]
    assert len(limits) == 22 and all(name in initializer for name in limits)

    with tempfile.TemporaryDirectory(prefix="mxsd-compatibility-", dir=args.build) as directory:
        work = pathlib.Path(directory)
        headers = work / "include"
        headers.mkdir()
        originals = {}
        for path in (args.project / "include").glob("*.h"):
            originals[path.name] = path.read_text(encoding="utf-8")
            (headers / path.name).write_text(originals[path.name], encoding="utf-8")

        def compile_file(source, output, success=True, diagnostic=None):
            includes = [str(headers), str(work)]
            if args.compiler_id == "MSVC":
                flags = ["/nologo", "/c"] + ["/I" + path for path in includes]
                flags += ["/Fo" + str(output)]
            else:
                flags = ["-std=c99", "-Werror=implicit-function-declaration", "-c"]
                flags += ["-I" + path for path in includes] + ["-o", str(output)]
            return run([args.cc] + flags + [str(source)], success, diagnostic)

        objects = []
        for schema, options in (
            ("device", ["--bindings"]),
            ("stream_bindings", ["--stream-bind", "doc/row"]),
        ):
            run(
                compiler
                + [str(args.project / "tests" / (schema + ".xsd")), "-o", str(work)]
                + options
            )
            for suffix in ("mxsd", "bind"):
                source = work / (schema + "." + suffix + ".c")
                obj = work / (schema + "." + suffix + ".obj")
                compile_file(source, obj)
                objects.append(obj)
                required = (
                    ("runtime", "descriptor", "binding")
                    if suffix == "bind"
                    else ("runtime", "descriptor")
                )
                for kind in required:
                    macro = "MXSD_%s_ABI_VERSION" % kind.upper()
                    # Header-only clients must fail, not merely generated .c files.
                    probe = work / "probe.c"
                    probe.write_text('#include "%s.%s.h"\n' % (schema, suffix), encoding="utf-8")
                    for replacement in (
                        "#define %s 999" % macro,
                        "/* missing epoch */",
                    ):
                        bad = re.sub(r"#define %s\s+\d+" % macro, replacement, version_header)
                        (headers / "mxsd_version.h").write_text(bad, encoding="utf-8")
                        compile_file(probe, work / "bad.obj", False, "mxsdc requires " + macro)
                    (headers / "mxsd_version.h").write_text(version_header, encoding="utf-8")
                    # Include the valid generated header first, then alter the
                    # epoch: its include guard cannot hide the .c's own check.
                    probe.write_text(
                        '#include "%s.%s.h"\n#undef %s\n#define %s 999\n#include "%s"\n'
                        % (schema, suffix, macro, macro, source.name),
                        encoding="utf-8",
                    )
                    compile_file(probe, work / "bad.obj", False, "mxsdc requires " + macro)

        # Releases are diagnostic metadata; unchanged epochs do not pin the
        # compiler to an exact patch release.
        (headers / "mxsd_version.h").write_text(
            version_header.replace('"%s"' % version, '"0.20.999"'), encoding="utf-8"
        )
        compile_file(work / "device.mxsd.c", work / "other-release.obj")
        (headers / "mxsd_version.h").write_text(version_header, encoding="utf-8")

        # Stub only the ordinary runtime calls: link checks must fail because
        # the epoch symbol is missing, not because of some unrelated API.
        stub = """
#include <mxsd_bind.h>
#include "device.mxsd.h"
#include "device.bind.h"
#include "stream_bindings.bind.h"
static mxsd_result_t ok(void) { mxsd_result_t r = {MXSD_OK, 0, NULL, NULL}; return r; }
mxsd_result_t mxsd_validate(const mxsd_schema_t *s, const char *x) { return ok(); }
mxsd_result_t mxsd_stream_init(mxsd_stream_t *s, const mxsd_schema_t *d) { return ok(); }
mxsd_result_t mxsd_bind_read(const mxsd_bind_root_t *r, const char *x, void *o,
                           mxsd_bind_arena_t *a) { return ok(); }
mxsd_result_t mxsd_bind_write(const mxsd_bind_root_t *r, const void *o, mxsd_bind_sink_fn s,
                            void *c, mxsd_stream_t *v) { return ok(); }
mxsd_result_t mxsd_bind_write_buffer(const mxsd_bind_root_t *r, const void *o, char *b,
                                   size_t c, size_t *l) { return ok(); }
mxsd_result_t mxsd_bind_stream_init(mxsd_bind_stream_t *r, const mxsd_bind_selection_t *s,
    void *o, mxsd_bind_arena_t *a, mxsd_bind_item_fn c, void *u,
    const mxsd_allocator_t *alloc) { return ok(); }
mxsd_result_t mxsd_bind_stream_feed(mxsd_bind_stream_t *r, const void *d, size_t n) { return ok(); }
mxsd_result_t mxsd_bind_stream_finish(mxsd_bind_stream_t *r) { return ok(); }
void mxsd_bind_stream_dispose(mxsd_bind_stream_t *r) {}
int main(void) {
    return device_schema() == NULL || device_device_binding() == NULL ||
        stream_bindings_doc_row_reader_init(NULL, NULL, NULL, NULL, NULL, NULL).code != MXSD_OK;
}
"""
        for missing in (None, "runtime", "descriptor", "binding"):
            definitions = "".join(
                "void mxsd_%s_abi_%d(void) {}\n"
                % (kind, info[kind] if kind != missing else info[kind] + 1)
                for kind in ("runtime", "descriptor", "binding")
            )
            source = work / "stub.c"
            source.write_text(stub + definitions, encoding="utf-8")
            stub_object = work / "stub.obj"
            compile_file(source, stub_object)
            executable = work / "probe.exe"
            link_flags = (
                ["/nologo", "/Fe" + str(executable)]
                if args.compiler_id == "MSVC"
                else ["-o", str(executable)]
            )
            diagnostic = None if missing is None else "mxsd_%s_abi_%d" % (missing, info[missing])
            run(
                [args.cc] + link_flags + [str(p) for p in objects + [stub_object]],
                missing is None,
                diagnostic,
            )
            if missing is None:
                run([str(executable)])

        # Actual package version selection must enforce the pre-1.0 minor line.
        script = work / "package-version.cmake"
        major, minor, patch = map(int, version.split("."))
        cases = [(version, True), ("%d.%d.0" % (major, minor + 1), False)]
        if major == 0 and minor > 0:
            cases.append(("0.%d.0" % (minor - 1), False))
        elif major > 0:
            cases[1] = ("%d.%d.0" % (major + 1, minor), False)
        for requested, compatible in cases:
            parts = requested.split(".")
            script.write_text(
                "set(PACKAGE_FIND_VERSION %s)\n" % requested
                + "".join(
                    "set(PACKAGE_FIND_VERSION_%s %s)\n" % (name, value)
                    for name, value in zip(("MAJOR", "MINOR", "PATCH"), parts)
                )
                + "set(PACKAGE_FIND_VERSION_COUNT 3)\n"
                + 'include("%s")\n' % (args.build / "mxsdConfigVersion.cmake").as_posix()
                + 'if(NOT "${PACKAGE_VERSION_COMPATIBLE}" STREQUAL "%s")\n'
                % ("TRUE" if compatible else "FALSE")
                + '  message(FATAL_ERROR "Unexpected package compatibility")\nendif()\n',
                encoding="utf-8",
            )
            run([args.cmake, "-P", str(script)])
    print("Generated headers, sources, precompiled objects, metadata and package policy: passed")


if __name__ == "__main__":
    main()
