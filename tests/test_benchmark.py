#!/usr/bin/env python3
"""Offline regression tests for benchmark fairness, value checks, and report semantics."""

import copy
import importlib.util
import json
import pathlib
import tempfile
import unittest

PROTOCOL_PATH = pathlib.Path(__file__).parent / "benchmark/stream_protocol.py"
SPEC = importlib.util.spec_from_file_location("benchmark_protocol", PROTOCOL_PATH)
protocol = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(protocol)


class BenchmarkProtocolTest(unittest.TestCase):
    def test_canonical_values(self):
        xml = b"""<samples xmlns='urn:mxsd:benchmark'><sample id='007'><label>sensor_A</label>
        <enabled>1</enabled><reading> -12 </reading></sample></samples>"""
        equivalent = xml.replace(b"'007'", b"'7'").replace(b">1<", b">true<")
        self.assertEqual(protocol.reference_values(xml), protocol.reference_values(equivalent))
        self.assertEqual(protocol.reference_values(xml)["count"], 1)
        changed = xml.replace(b"sensor_A", b"sensor_B")
        self.assertNotEqual(protocol.reference_values(xml), protocol.reference_values(changed))

    def test_all_fields_affect_checksum(self):
        xml = b"""<samples xmlns='urn:mxsd:benchmark'><sample id='7'><label>a</label>
        <enabled>true</enabled><reading>-12</reading></sample></samples>"""
        for before, after in (
            (b"'7'", b"'8'"),
            (b">a<", b">b<"),
            (b">true<", b">false<"),
            (b">-12<", b">-13<"),
        ):
            with self.subTest(field=before):
                self.assertNotEqual(
                    protocol.reference_values(xml),
                    protocol.reference_values(xml.replace(before, after)),
                )

    def test_measurement_validation(self):
        meta = {"runs": 1, "input_bytes": 100}
        sample = {
            "run": 0,
            "count": 2,
            "checksum": 123,
            "cycles": 1000,
            "first_cycles": 100,
            "first_supplied_bytes": 64,
        }
        expected = {"count": 2, "checksum": 123}
        protocol.validate_run(meta, [sample], {"selftest": 0}, expected)
        for key, value in (
            ("count", 0),
            ("checksum", 0),
            ("run", 1),
            ("first_cycles", 0),
            ("first_cycles", 1001),
            ("first_supplied_bytes", 101),
        ):
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                protocol.validate_run(meta, [{**sample, key: value}], {"selftest": 0}, expected)
        with self.assertRaises(ValueError):
            protocol.validate_run(meta, [], {"selftest": 0}, expected)
        with self.assertRaises(ValueError):
            protocol.validate_run(meta, [sample], {"selftest": 1}, expected)

    def test_codegen_flags(self):
        flags = protocol.codegen_flags(
            [
                "cc",
                "-Os",
                "-DNDEBUG",
                "-mcpu=cortex-a53",
                "-fno-rtti",
                "-fno-exceptions",
                "-fmacro-prefix-map=a=b",
                "-fno-strict-aliasing",
            ]
        )
        self.assertEqual(flags, ["-Os", "-mcpu=cortex-a53", "-fno-strict-aliasing"])

    def test_pair_audit_rejects_unfair_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            library = root / "xsde/libxsde.a"
            library.parent.mkdir()
            library.write_bytes(b"test runtime archive")
            (root / "build").mkdir()
            (root / "build/config.build").write_text("test configuration", encoding="utf-8")
            (root / "CMakeCache.txt").write_text(
                "XSDE_LIBRARY:FILEPATH=" + str(library) + "\n", encoding="utf-8"
            )
            metadata = {
                "directory": str(root),
                "fixture_sha256": "input",
                "codegen_flags": ["-Os"],
                "configuration": {"CONFIG_MAIN_STACK_SIZE": "16384"},
                "compilers": {"C": "/toolchain/cc", "CXX": "/toolchain/cxx"},
                "cxx_flags": ["-std=c++98", "-fcheck-new"],
                "emulator_command": ["qemu-system-aarch64", "-icount", "shift=4"],
            }
            manifest = {
                "flags": ["-Os", "-DNDEBUG"],
                "library": str(library),
                "library_sha256": protocol.sha256(library),
                "build_configuration": "test configuration",
                "cc": "/toolchain/cc",
                "cxx": "/toolchain/cxx",
                "cxx_flags": ["-std=c++98", "-fcheck-new"],
            }
            protocol.audit_pair(metadata, metadata, manifest)
            changes = [
                {"emulator_command": ["qemu-system-aarch64", "-icount", "shift=5"]},
                {"fixture_sha256": "other input"},
                {"codegen_flags": ["-O2"]},
                {"configuration": {"CONFIG_MAIN_STACK_SIZE": "131072"}},
                {"compilers": {"C": "/other/cc", "CXX": "/toolchain/cxx"}},
            ]
            for change in changes:
                with self.subTest(change=change), self.assertRaises(ValueError):
                    protocol.audit_pair(metadata, {**metadata, **change}, manifest)
            for change in (
                {"flags": ["-O2", "-DNDEBUG"]},
                {"flags": ["-Os"]},
                {"library_sha256": "modified"},
                {"build_configuration": "modified"},
                {"cxx_flags": ["-std=c++98"]},
            ):
                with self.subTest(change=change), self.assertRaises(ValueError):
                    protocol.audit_pair(metadata, metadata, {**manifest, **change})

    def test_actual_build_commands_are_audited(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "zephyr").mkdir()
            (root / "zephyr/zephyr.elf").write_bytes(b"fixture ELF identity")
            (root / "benchmark.xml").write_bytes(b"fixture identity")
            (root / "build_info.yml").write_text("test build", encoding="utf-8")
            (root / "build.ninja").write_text(
                "COMMAND = cd /build && /qemu-system-aarch64 -icount shift=4 -kernel /zephyr.elf\n",
                encoding="utf-8",
            )
            (root / "CMakeCache.txt").write_text(
                "CMAKE_C_COMPILER:FILEPATH=/cc\nCMAKE_CXX_COMPILER:FILEPATH=/cxx\n",
                encoding="utf-8",
            )
            config = root / "zephyr/.config"
            config.write_text("CONFIG_INIT_STACKS=y\n", encoding="utf-8")
            commands = [
                {
                    "file": "/project/streaming/observer.c",
                    "arguments": ["cc", "-Os", "-DNDEBUG", "-ffunction-sections"],
                }
            ]
            database = root / "compile_commands.json"
            database.write_text(json.dumps(commands), encoding="utf-8")
            result = protocol.build_metadata(root)
            self.assertEqual(result["codegen_flags"], ["-Os", "-ffunction-sections"])
            for bad_flags in (
                ["cc", "-O2", "-DNDEBUG"],
                ["cc", "-Os"],
                ["cc", "-Os", "-DNDEBUG", "-flto"],
            ):
                bad = copy.deepcopy(commands)
                bad[0]["arguments"] = bad_flags
                database.write_text(json.dumps(bad), encoding="utf-8")
                with self.subTest(flags=bad_flags), self.assertRaises(ValueError):
                    protocol.build_metadata(root)
            database.write_text(json.dumps(commands), encoding="utf-8")
            config.write_text("CONFIG_NETWORKING=y\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                protocol.build_metadata(root)


if __name__ == "__main__":
    unittest.main()
