#!/usr/bin/env python3
"""Pure protocol/audit helpers shared by the fair streaming comparison and its tests."""

import hashlib
import json
import re
import shlex
import xml.etree.ElementTree as ET
from pathlib import Path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


CPP_FLAGS = {
    "-fno-exceptions",
    "-fno-rtti",
    "-fno-threadsafe-statics",
    "-fno-use-cxa-atexit",
    "-fcheck-new",
}


def cxx_flags(command: list[str]) -> list[str]:
    """Capture language-specific code generation too, including allocation null checks."""
    return [token for token in command if token in CPP_FLAGS or token.startswith("-std=c++")]


def codegen_flags(command: list[str]) -> list[str]:
    """Keep code generation/ABI options, excluding diagnostics, path maps, and C++ ABI extras."""
    return [
        token
        for token in command
        if token.startswith(("-O", "-f", "-m", "--param=", "--sysroot=", "-specs="))
        and not token.startswith(("-fdiagnostics-", "-fmacro-prefix-map="))
        and token not in CPP_FLAGS
    ]


def reference_values(xml: bytes) -> dict[str, int]:
    """Independently decode every field, then hash a canonical little-endian value stream."""
    ns = "{urn:mxsd:benchmark}"
    root = ET.fromstring(xml)
    if root.tag != ns + "samples":
        raise ValueError("wrong fixture root")
    checksum, count = 2166136261, 0
    for sample in root:
        if sample.tag != ns + "sample":
            raise ValueError("unexpected fixture element")
        label = sample.findtext(ns + "label").encode("utf-8")
        enabled = sample.findtext(ns + "enabled").strip()
        if enabled not in ("true", "false", "1", "0"):
            raise ValueError("invalid boolean in reference fixture")
        canonical = int(sample.attrib["id"]).to_bytes(4, "little")
        canonical += len(label).to_bytes(4, "little") + label
        canonical += bytes([enabled in ("true", "1")])
        canonical += int(sample.findtext(ns + "reading")).to_bytes(4, "little", signed=True)
        for byte in canonical:
            checksum = ((checksum ^ byte) * 16777619) & 0xFFFFFFFF
        count += 1
    return {"count": count, "checksum": checksum}


def validate_run(meta: dict, samples: list[dict], done: dict, expected: dict) -> None:
    """Reject incomplete runs, discarded/changed values, invalid completion, or bad timing."""
    if done.get("selftest") != 0 or len(samples) != meta["runs"]:
        raise ValueError("self-test failed or measurements missing")
    for index, sample in enumerate(samples):
        if sample["run"] != index or any(sample[key] != expected[key] for key in expected):
            raise ValueError("typed values/count differ from independent reference")
        if not 0 < sample["first_cycles"] <= sample["cycles"]:
            raise ValueError("invalid first-result timing")
        if not 0 < sample["first_supplied_bytes"] <= meta["input_bytes"]:
            raise ValueError("invalid first-result input position")


def build_metadata(build: Path) -> dict:
    """Capture actual compiler commands, generated sources, config, and fixture identity."""
    commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    entries = [
        entry
        for entry in commands
        if "/streaming/" in entry["file"]
        or "/src/mxsd" in entry["file"]
        or "benchmark.mxsd.c" in entry["file"]
        or "benchmark.bind.c" in entry["file"]
        or "benchmark-pskel.cxx" in entry["file"]
    ]
    if not entries:
        raise ValueError("no streaming benchmark sources in compilation database")
    profiles = []
    cpp_profiles = []
    for entry in entries:
        tokens = entry.get("arguments") or shlex.split(entry["command"])
        if "-DNDEBUG" not in tokens:
            raise ValueError("NDEBUG missing from " + entry["file"])
        flags = codegen_flags(tokens)
        if [flag for flag in flags if flag.startswith("-O")] != ["-Os"]:
            raise ValueError("comparison requires exactly -Os: " + entry["file"])
        if any("lto" in flag for flag in flags):
            raise ValueError("this comparison profile disables LTO for both implementations")
        profiles.append(flags)
        if entry["file"].endswith((".cpp", ".cxx")):
            cpp_profiles.append(cxx_flags(tokens))
    if any(set(flags) != set(profiles[0]) for flags in profiles):
        raise ValueError("code-generation flags differ within " + str(build))
    if cpp_profiles and any(set(flags) != set(cpp_profiles[0]) for flags in cpp_profiles):
        raise ValueError("C++ language flags differ within " + str(build))
    config_text = (build / "zephyr/.config").read_text(encoding="utf-8")
    config = dict(
        line.split("=", 1) for line in config_text.splitlines() if line.startswith("CONFIG_")
    )
    for key in ("CONFIG_NETWORKING", "CONFIG_NET_SOCKETS", "CONFIG_MBEDTLS"):
        if config.get(key) == "y":
            raise ValueError("network/TLS must not be linked into the XML-only benchmark")
    generated_sources = sorted(
        {
            Path(entry["file"])
            for entry in entries
            if Path(entry["file"]).name.startswith("benchmark.")
            or Path(entry["file"]).name.startswith("benchmark-pskel")
        }
    )
    generated = sorted(
        {
            path
            for source in generated_sources
            for path in source.parent.iterdir()
            if path.name.startswith("benchmark")
            and path.suffix in (".c", ".h", ".cxx", ".hxx", ".ixx")
        }
    )
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    compilers = {
        kind: re.search(r"^CMAKE_%s_COMPILER:[^=]+=(.+)$" % kind, cache, re.M).group(1)
        for kind in ("C", "CXX")
    }
    emulator = None
    for line in (build / "build.ninja").read_text(encoding="utf-8").splitlines():
        if "COMMAND =" not in line or " -icount " not in line or " -gdb " in line:
            continue
        tokens = shlex.split(line.split("COMMAND =", 1)[1])
        start = next(
            (i for i, token in enumerate(tokens) if Path(token).name.startswith("qemu-system-")),
            None,
        )
        if start is not None and "-kernel" in tokens:
            emulator = tokens[start:]
            emulator[emulator.index("-kernel") + 1] = "<ELF>"
            break
    if emulator is None:
        raise ValueError("cannot audit QEMU invocation: use the Ninja QEMU build")
    return {
        "directory": str(build),
        "fixture_sha256": sha256(build / "benchmark.xml"),
        "elf_sha256": sha256(build / "zephyr/zephyr.elf"),
        "codegen_flags": profiles[0],
        "cxx_flags": cpp_profiles[0] if cpp_profiles else None,
        "compile_commands": entries,
        "compilers": compilers,
        "emulator_command": emulator,
        "configuration": config,
        "configuration_text": config_text,
        "build_info": (build / "build_info.yml").read_text(encoding="utf-8"),
        "generated_source_bytes": sum(path.stat().st_size for path in generated),
        "generated_source_sha256": {path.name: sha256(path) for path in generated},
    }


def audit_pair(mxsd: dict, xsde: dict, manifest: dict) -> None:
    """Refuse a headline ratio unless fixture, codegen, runtime archive, and OS budgets match."""
    if mxsd["fixture_sha256"] != xsde["fixture_sha256"]:
        raise ValueError("different XML fixtures")
    if mxsd["emulator_command"] != xsde["emulator_command"]:
        raise ValueError("different QEMU CPU/machine/virtual-clock settings")
    if set(mxsd["codegen_flags"]) != set(xsde["codegen_flags"]):
        raise ValueError("different target optimization/ABI flags")
    if set(codegen_flags(manifest["flags"])) != set(mxsd["codegen_flags"]):
        raise ValueError("external XSD/e runtime has different optimization/ABI flags")
    if "-DNDEBUG" not in manifest["flags"]:
        raise ValueError("XSD/e runtime was not compiled with NDEBUG")
    if set(xsde["cxx_flags"] or []) != set(manifest["cxx_flags"]):
        raise ValueError("different C++ code-generation flags in the XSD/e runtime")
    for kind, name in (("C", "cc"), ("CXX", "cxx")):
        expected = Path(mxsd["compilers"][kind]).resolve()
        if (
            Path(xsde["compilers"][kind]).resolve() != expected
            or Path(manifest[name]).resolve() != expected
        ):
            raise ValueError("different target compiler: " + kind)
    xsde_cache = (Path(xsde["directory"]) / "CMakeCache.txt").read_text(encoding="utf-8")
    linked = re.search(r"^XSDE_LIBRARY:[^=]+=(.+)$", xsde_cache, re.M)
    if linked is None or Path(linked.group(1)).resolve() != Path(manifest["library"]).resolve():
        raise ValueError("runtime manifest does not describe the linked XSD/e archive")
    keys = (
        "CONFIG_BOARD",
        "CONFIG_BOARD_TARGET",
        "CONFIG_ARCH",
        "CONFIG_CPP",
        "CONFIG_SIZE_OPTIMIZATIONS",
        "CONFIG_STD_CPP98",
        "CONFIG_PICOLIBC",
        "CONFIG_XIP",
        "CONFIG_MAIN_STACK_SIZE",
        "CONFIG_ISR_STACK_SIZE",
        "CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE",
        "CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE",
        "CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC",
        "CONFIG_SYS_CLOCK_TICKS_PER_SEC",
        "CONFIG_INIT_STACKS",
        "CONFIG_THREAD_STACK_INFO",
        "CONFIG_SYS_HEAP_RUNTIME_STATS",
        "CONFIG_ASSERT",
        "CONFIG_SMP",
    )
    for key in keys:
        if mxsd["configuration"].get(key) != xsde["configuration"].get(key):
            raise ValueError("different OS/measurement configuration: " + key)
    library = Path(manifest["library"])
    if sha256(library) != manifest["library_sha256"]:
        raise ValueError("XSD/e archive changed after the runtime manifest was recorded")
    config = library.parent.parent / "build/config.build"
    if config.read_text(encoding="utf-8") != manifest["build_configuration"]:
        raise ValueError("XSD/e build configuration changed after recording")


def elf_memory(path: Path) -> dict:
    """Measure linked sections/symbols, never substitute executable file size for Flash."""
    from elftools.elf.elffile import ELFFile

    with path.open("rb") as stream:
        elf = ELFFile(stream)
        allocated = [section for section in elf.iter_sections() if section["sh_flags"] & 2]
        # TLS templates are instantiated in per-thread storage; a NOBITS TLS
        # section can overlap another section's VMA and is not a separate RAM reserve.
        runtime_sections = [section for section in allocated if not section["sh_flags"] & 0x400]
        symbols = elf.get_section_by_name(".symtab")
        values = {symbol.name: int(symbol["st_value"]) for symbol in symbols.iter_symbols()}
        return {
            "rom_load_payload_bytes": sum(
                section["sh_size"] for section in allocated if section["sh_type"] != "SHT_NOBITS"
            ),
            "writable_static_ram_bytes": sum(
                section["sh_size"] for section in runtime_sections if section["sh_flags"] & 1
            ),
            "all_allocated_section_bytes": sum(section["sh_size"] for section in runtime_sections),
            "tls_template_bytes": sum(
                section["sh_size"] for section in allocated if section["sh_flags"] & 0x400
            ),
            "linker_image_ram_bytes": values.get("_image_ram_size"),
            "linker_rom_region_bytes": values.get("__rom_region_size"),
            "sections": [
                {
                    "name": section.name,
                    "bytes": section["sh_size"],
                    "address": section["sh_addr"],
                    "writable": bool(section["sh_flags"] & 1),
                    "tls_template": bool(section["sh_flags"] & 0x400),
                    "type": section["sh_type"],
                }
                for section in allocated
            ],
        }
