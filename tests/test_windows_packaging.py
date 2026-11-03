"""Portable negative tests for the Windows package assembler and verifier."""
import importlib.util
import json
from pathlib import Path
import tempfile
import subprocess
import unittest
from unittest.mock import patch, MagicMock

ROOT = Path(__file__).resolve().parents[1]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


package = load("package_windows", "package-windows.py")
check = load("check_package", "check-windows-package.py")


class PackagingTests(unittest.TestCase):
    def test_recursive_case_insensitive_closure_and_cycles(self):
        graph = {"app": ["GTK.DLL", "kernel32.dll", "api-ms-win-core-file-l1-1-0.dll"],
                 "gtk": ["glib.dll"], "glib": ["gtk.dll"]}
        files, system = package.dependency_closure(["app"], {"gtk.dll": "gtk", "glib.dll": "glib"},
                                                  {"kernel32.dll": "system"}, graph.__getitem__)
        self.assertEqual(set(files), {"gtk", "glib"})
        self.assertEqual(system, ["api-ms-win-core-file-l1-1-0.dll", "kernel32.dll"])

    def test_missing_transitive_dependency_fails(self):
        graph = {"app": ["gtk.dll"], "gtk": ["missing.dll"]}
        with self.assertRaisesRegex(ValueError, "Unresolved DLL missing.dll"):
            package.dependency_closure(["app"], {"gtk.dll": "gtk"}, {}, graph.__getitem__)

    def test_reject_posix_runtime_even_if_available(self):
        for name in ("msys-2.0.dll", "cygwin1.dll"):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "POSIX runtime"):
                package.dependency_closure(["app"], {name: "runtime"}, {}, lambda _: [name])

    def test_system_dll_not_bundled(self):
        files, system = package.dependency_closure(["app"], {"kernel32.dll": "fake"},
                                                  {"kernel32.dll": "system"}, lambda _: ["KERNEL32.dll"])
        self.assertEqual(files, [])
        self.assertEqual(system, ["kernel32.dll"])

    def test_pe_import_parser(self):
        with patch.object(package, "run", return_value="x: file format pei-x86-64\n\tDLL Name: libgtk-4-1.dll\n"):
            self.assertEqual(package.imports("x", "objdump"), ["libgtk-4-1.dll"])
        for output in ("file format elf64-x86-64", "file format pei-x86-64", "file format pei-x86-64\n DLL Name: ../bad.dll\n"):
            with self.subTest(output=output), patch.object(package, "run", return_value=output), self.assertRaises(ValueError):
                package.imports("x", "objdump")

    def test_relative_loader_cache_with_space_path(self):
        prefix = Path("/build space/ucrt64")
        module = prefix / "lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-svg.dll"
        result = package.relative_loader_cache(f'# build directory\n"{module}"\n"svg" 6 "gdk-pixbuf" "SVG" "LGPL"\n\n', prefix, [module])
        self.assertTrue(result.startswith('"lib/gdk-pixbuf-2.0/'))
        self.assertNotIn(str(prefix), result)

    def test_missing_loader_query_result_fails(self):
        with self.assertRaisesRegex(ValueError, "every bundled module"):
            package.relative_loader_cache("", Path("/prefix"), [Path("/prefix/lib/svg.dll")])

    def test_external_loader_fails(self):
        with self.assertRaisesRegex(ValueError, "outside UCRT64"):
            package.relative_loader_cache('"/outside/svg.dll"\n', Path("/prefix"), [])

    def test_unknown_loader_fails(self):
        with self.assertRaisesRegex(ValueError, "Unexpected loader"):
            package.relative_loader_cache('"/prefix/lib/other.dll"\n', Path("/prefix"), [])

    def test_missing_assets_fail(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(ValueError, "Missing runtime assets"):
                package.copy_tree(Path(temp) / "missing", Path(temp) / "destination")

    def test_missing_or_modified_manifest_files_fail(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = {"format": 1, "runtime_owners": {"dll": "package"}, "files": {"required.dll": "abc"}}
            (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "inventory mismatch"):
                check.verify_tree(root)
            (root / "required.dll").write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError, "hash mismatch"):
                check.verify_tree(root)

    def test_isolation_removes_development_paths(self):
        with patch.dict(package.os.environ, {"SystemRoot": "C:/Windows", "PATH": "C:/msys64/ucrt64/bin",
                                             "GDK_PIXBUF_MODULE_FILE": "outside/cache", "XDG_DATA_DIRS": "outside"}, clear=True):
            env = check.isolated_environment()
            self.assertNotIn("msys", env["PATH"])
            self.assertNotIn("GDK_PIXBUF_MODULE_FILE", env)
            self.assertNotIn("XDG_DATA_DIRS", env)
            self.assertEqual(env["GSK_RENDERER"], "cairo")

    def test_failed_runtime_command_retains_diagnostics(self):
        result = subprocess.CompletedProcess(["diagnostic"], 1, "Missing schema detail")
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / "failure.log"
            with patch.object(check.subprocess, "run", return_value=result), self.assertRaises(subprocess.CalledProcessError):
                with check.validation_log(log) as output:
                    check.run_logged(["diagnostic"], Path(temp), {}, output)
            self.assertIn("Missing schema detail", log.read_text())
            self.assertIn("FAILED", log.read_text())

    def test_timeout_retains_partial_output(self):
        error = subprocess.TimeoutExpired(["diagnostic"], 30, output=b"partial diagnostic")
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / "timeout.log"
            with patch.object(check.subprocess, "run", side_effect=error), self.assertRaises(subprocess.TimeoutExpired):
                with check.validation_log(log) as output:
                    check.run_logged(["diagnostic"], Path(temp), {}, output)
            self.assertIn("partial diagnostic", log.read_text())

    def test_module_audit_failure_drains_gui_output(self):
        process = MagicMock()
        process.__enter__.return_value = process
        process.poll.return_value = None
        process.communicate.return_value = ("GUI startup diagnostics", None)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            log = root / "modules.log"
            with patch.object(check.subprocess, "Popen", return_value=process), \
                 patch.object(check.time, "sleep"), \
                 patch.object(check, "loaded_modules", side_effect=OSError("module enumeration failed")), \
                 self.assertRaises(OSError):
                with check.validation_log(log) as output:
                    check.smoke(root, root, {}, output)
            process.kill.assert_called_once()
            self.assertIn("GUI startup diagnostics", log.read_text())
            self.assertIn("module enumeration failed", log.read_text())

    def test_module_audit_requires_app_and_gtk(self):
        root, windows = Path("/package"), Path("/Windows")
        for paths in ([], [root / "bin/music.exe"], [windows / "System32/kernel32.dll"]):
            with self.subTest(paths=paths), self.assertRaisesRegex(ValueError, "did not observe"):
                check.verify_module_paths(paths, root, windows)
        valid = [root / "bin/music.exe", root / "bin/libgtk-4-1.dll", windows / "System32/kernel32.dll"]
        check.verify_module_paths(valid, root, windows)
        with self.assertRaisesRegex(ValueError, "outside package"):
            check.verify_module_paths(valid + [Path("/development/bin/other.dll")], root, windows)

    def test_archive_members_have_exactly_one_root(self):
        self.assertEqual(check.archive_root(["MUSIC/bin/music.exe", "MUSIC/manifest.json"]), "MUSIC")
        for names in (["MUSIC/bin/music.exe", "outside.exe"], ["MUSIC/a", "other/b"], []):
            with self.subTest(names=names), self.assertRaises(ValueError):
                check.archive_root(names)

    def test_archive_rejects_collisions_and_windows_ambiguous_names(self):
        for names in (["MUSIC/a", "MUSIC/a"], ["MUSIC/a", "MUSIC/A"], ["MUSIC/NUL.txt"],
                      ["MUSIC/CON"], ["MUSIC/file."], ["MUSIC/file "], ["MUSIC/../escape"],
                      ["/absolute/file"], ["MUSIC/a:b"], ["MUSIC/./file"], ["MUSIC//file"]):
            with self.subTest(names=names), self.assertRaises(ValueError):
                check.archive_root(names)


if __name__ == "__main__":
    unittest.main()
