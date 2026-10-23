#!/usr/bin/env python3
"""Explicitly build an XSD/e runtime with the streaming comparison's optimization profile."""

import argparse
import hashlib
import json
import shlex
import subprocess
from pathlib import Path

from stream_protocol import codegen_flags, cxx_flags


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, required=True, help="XSD/e libxsde source tree")
    parser.add_argument("--build-dir", type=Path, required=True, help="new out-of-source directory")
    parser.add_argument("--cc", required=True)
    parser.add_argument("--cxx", required=True)
    parser.add_argument("--build2", default="b")
    parser.add_argument(
        "--zephyr-build",
        type=Path,
        required=True,
        help="built mxsd_stream application supplying the actual codegen flags",
    )
    args = parser.parse_args()
    source = args.source_dir.resolve()
    build = args.build_dir.resolve()
    if source == build or not (source / "build/root.build").is_file():
        parser.error("use a separate build directory and a libxsde source tree")
    if build.exists() and any(build.iterdir()):
        parser.error("use a new empty directory; do not overwrite another runtime configuration")
    entries = json.loads((args.zephyr_build / "compile_commands.json").read_text(encoding="utf-8"))
    entry = next(entry for entry in entries if entry["file"].endswith("/src/mxsd_stream.c"))
    tokens = entry.get("arguments") or shlex.split(entry["command"])
    flags = codegen_flags(tokens) + ["-DNDEBUG"]
    cpp_entry = next(entry for entry in entries if entry["file"].endswith((".cpp", ".cxx")))
    cpp_tokens = cpp_entry.get("arguments") or shlex.split(cpp_entry["command"])
    cpp_options = cxx_flags(cpp_tokens)
    if [flag for flag in flags if flag.startswith("-O")] != ["-Os"]:
        parser.error("the size comparison profile requires exactly -Os")
    command = [
        args.build2,
        "configure:",
        str(source) + "/@" + str(build) + "/",
        "config.c=" + args.cc,
        "config.cxx=" + args.cxx,
        "config.cc.coptions=" + " ".join(flags),
        "config.cxx.coptions=" + " ".join(cpp_options),
        "config.bin.lib=static",
        "config.libxsde.stl=false",
        "config.libxsde.iostream=false",
        "config.libxsde.exceptions=false",
        "config.libxsde.parser_validation=true",
        "config.libxsde.serializer_validation=true",
        "config.libxsde.regexp=true",
    ]
    subprocess.run(command, check=True)
    subprocess.run([args.build2, str(build / "xsde/lib{xsde}")], check=True)
    library = build / "xsde/libxsde.a"
    manifest = {
        "protocol": 1,
        "source": str(source),
        "library": str(library),
        "library_sha256": hashlib.sha256(library.read_bytes()).hexdigest(),
        "cc": args.cc,
        "cxx": args.cxx,
        "cc_version": subprocess.check_output([args.cc, "--version"], text=True),
        "cxx_version": subprocess.check_output([args.cxx, "--version"], text=True),
        "flags": flags,
        "cxx_flags": cpp_options,
        "configure_command": command,
        "build_configuration": (build / "build/config.build").read_text(encoding="utf-8"),
    }
    (build / "benchmark-runtime.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(build / "benchmark-runtime.json")


if __name__ == "__main__":
    main()
