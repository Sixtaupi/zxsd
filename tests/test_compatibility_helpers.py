#!/usr/bin/env python3
"""Offline tests for compiler environment setup and strict ABI-probe diagnostics."""

import pathlib
import subprocess
import tempfile
import unittest
from unittest import mock

import test_compatibility as compatibility


class CompatibilityHelpersTest(unittest.TestCase):
    def installation(self, root, host="x64", target="x64"):
        """Create a fake toolset tree; these tests never execute a Windows program."""
        vc = root / "Microsoft Visual Studio/18/Enterprise/VC"
        compiler = vc / ("Tools/MSVC/14.51.36231/bin/Host%s/%s/cl.exe" % (host, target))
        compiler.parent.mkdir(parents=True)
        compiler.touch()
        script = vc / "Auxiliary/Build/vcvarsall.bat"
        script.parent.mkdir(parents=True)
        script.touch()
        return compiler, script

    def test_matching_installation_and_architecture(self):
        for host, target, expected in (
            ("x64", "x64", "x64"),
            ("x86", "x86", "x86"),
            ("x86", "x64", "x86_x64"),
            ("x64", "x86", "x64_x86"),
            ("x64", "arm64", "x64_arm64"),
            ("arm64", "arm64", "arm64"),
        ):
            with self.subTest(host=host, target=target), tempfile.TemporaryDirectory() as directory:
                compiler, script = self.installation(pathlib.Path(directory), host, target)
                actual = compatibility.msvc_setup(compiler)
                self.assertEqual(actual, (script.resolve(), expected, "14.51.36231"))

    def test_missing_script_is_explicit_error(self):
        with tempfile.TemporaryDirectory() as directory:
            compiler, script = self.installation(pathlib.Path(directory))
            script.unlink()
            with self.assertRaisesRegex(RuntimeError, "script not found"):
                compatibility.msvc_setup(compiler)

    def test_unrecognized_toolchain_is_not_guessed(self):
        with self.assertRaisesRegex(RuntimeError, "Cannot identify"):
            compatibility.msvc_setup(pathlib.Path("unknown/cl.exe"))

    def test_environment_capture_handles_spaces_unicode_and_case(self):
        with tempfile.TemporaryDirectory() as directory:
            compiler, script = self.installation(pathlib.Path(directory))
            inherited = {
                "ComSpec": r"C:\Windows\System32\cmd.exe",
                "Path": "old path",
                "INCLUDE": "stale includes",
                "KEEP": "original",
            }
            output = (
                "INCLUDE=C:\\SDK\\été\\include\r\nLIB=C:\\SDK\\lib\r\n"
                "Path=C:\\VS\\bin;C:\\Windows\r\nKEEP=value=with=equals\r\n"
                "=D:=D:\\work\r\n"
            )
            result = subprocess.CompletedProcess([], 0, output.encode("utf-16le"), b"")
            before = inherited.copy()
            with mock.patch.object(compatibility.subprocess, "run", return_value=result) as run:
                environment = compatibility.msvc_environment(compiler, inherited)
            self.assertEqual(inherited, before)
            self.assertEqual(environment["INCLUDE"], "C:\\SDK\\été\\include")
            self.assertEqual(environment["LIB"], "C:\\SDK\\lib")
            self.assertEqual(environment["PATH"], "C:\\VS\\bin;C:\\Windows")
            self.assertEqual(environment["KEEP"], "value=with=equals")
            self.assertNotIn("Path", environment)
            self.assertNotIn("", environment)
            command = run.call_args.args[0]
            self.assertIn(
                '/d /u /s /c ""%s" x64 -vcvars_ver=14.51.36231' % script.resolve(), command
            )
            self.assertTrue(command.endswith('>nul && set"'))
            self.assertEqual(run.call_args.kwargs["env"], before)
            self.assertNotIn("shell", run.call_args.kwargs)

    def test_setup_failure_is_not_silently_ignored(self):
        with tempfile.TemporaryDirectory() as directory:
            compiler, _script = self.installation(pathlib.Path(directory))
            result = subprocess.CompletedProcess([], 1, b"", b"setup failed")
            with mock.patch.object(compatibility.subprocess, "run", return_value=result):
                with self.assertRaisesRegex(RuntimeError, "Could not initialize"):
                    compatibility.msvc_environment(compiler, {})

    def test_incomplete_environment_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            compiler, _script = self.installation(pathlib.Path(directory))
            result = subprocess.CompletedProcess([], 0, "PATH=tools\r\n".encode("utf-16le"), b"")
            with mock.patch.object(compatibility.subprocess, "run", return_value=result):
                with self.assertRaisesRegex(RuntimeError, "missing INCLUDE, LIB"):
                    compatibility.msvc_environment(compiler, {})

    def test_probe_environment_and_working_directory_are_forwarded(self):
        result = subprocess.CompletedProcess([], 0, "compiled successfully")
        environment = {"INCLUDE": "SDK headers", "LIB": "SDK libraries"}
        with mock.patch.object(compatibility.subprocess, "run", return_value=result) as run:
            compatibility.run(["cl.exe"], env=environment, cwd="temporary build")
        self.assertEqual(run.call_args.kwargs["env"], environment)
        self.assertEqual(run.call_args.kwargs["cwd"], "temporary build")

    def test_unrelated_compile_failure_is_not_an_abi_pass(self):
        result = subprocess.CompletedProcess([], 1, "fatal error C1083: stddef.h not found")
        with mock.patch.object(compatibility.subprocess, "run", return_value=result):
            with self.assertRaises(AssertionError):
                compatibility.run(["cl.exe"], False, "zxsdc requires ZXSD_RUNTIME_ABI_VERSION")
        result = subprocess.CompletedProcess([], 1, "zxsdc requires ZXSD_RUNTIME_ABI_VERSION=1")
        with mock.patch.object(compatibility.subprocess, "run", return_value=result):
            compatibility.run(["cl.exe"], False, "zxsdc requires ZXSD_RUNTIME_ABI_VERSION")


if __name__ == "__main__":
    unittest.main()
