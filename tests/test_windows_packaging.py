"""Portable negative tests for the Windows package assembler and verifier."""
import importlib.util
import json
from pathlib import Path, PureWindowsPath
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
        prefix = Path("/build space/ucrt64").resolve()
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
        prefix = Path("/prefix").resolve()
        with self.assertRaisesRegex(ValueError, "Unexpected loader"):
            package.relative_loader_cache(f'"{prefix / "lib/other.dll"}"\n', prefix, [])

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

    def test_module_membership_failure_retains_observed_paths(self):
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / "smoke.log"
            process = MagicMock()
            process.__enter__.return_value = process
            process.poll.return_value = None
            process.communicate.return_value = ("GUI diagnostics", None)
            with patch.object(check.subprocess, "Popen", return_value=process), \
                 patch.object(check.time, "sleep"), \
                 patch.object(check, "snapshot_modules", return_value=[Path("/unexpected/music.exe")]), \
                 patch.dict(check.os.environ, {"SystemRoot": temp}), \
                 self.assertRaisesRegex(ValueError, "did not observe"):
                with check.validation_log(log) as output:
                    check.smoke(Path(temp) / "package", Path(temp), {}, output)
            self.assertIn("unexpected", log.read_text())
            self.assertIn("Module audit package root:", log.read_text())
            self.assertIn("libgtk-4-1.dll", log.read_text())
            self.assertIn("GUI diagnostics", log.read_text())

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

    def test_system_driver_import_requires_an_existing_system_file(self):
        with tempfile.TemporaryDirectory() as temp:
            system_dir = Path(temp)
            (system_dir / "WINSPOOL.DRV").write_bytes(b"synthetic PE test placeholder")
            self.assertNotIn("winspool.drv", package.dll_index(system_dir))
            system = package.dll_index(system_dir, (".dll", ".drv"))
            files, external = package.dependency_closure(["gtk"], {}, system, lambda _: ["WINSPOOL.DRV"])
            self.assertEqual(files, [])
            self.assertEqual(external, ["winspool.drv"])
            with self.assertRaisesRegex(ValueError, "Unresolved DLL missing.drv"):
                package.dependency_closure(["gtk"], {}, system, lambda _: ["missing.drv"])

    def test_already_relative_loader_cache_matches_absolute_form(self):
        prefix = Path("/build space/ucrt64").resolve()
        relative = "lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-svg.dll"
        modules = [prefix / relative]
        absolute = package.relative_loader_cache(f'"{modules[0]}"\n', prefix, modules)
        self.assertEqual(package.relative_loader_cache(f'"{relative}"\n', prefix, modules), absolute)

    def test_relative_and_absolute_loader_escapes_fail(self):
        prefix = Path("/prefix").resolve()
        modules = [prefix / "lib/svg.dll"]
        for path in ("../prefix/lib/svg.dll", "lib/../../outside/svg.dll", "/prefix/../outside/svg.dll",
                     "C:lib/svg.dll", "/outside/svg.dll", "lib/other.dll"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                package.relative_loader_cache(f'"{path}"\n', prefix, modules)

    def test_windows_loader_path_semantics_on_any_host(self):
        prefix = PureWindowsPath("C:/msys64/ucrt64")
        modules = [prefix / "lib/svg.dll"]
        with patch.object(package, "Path", PureWindowsPath):
            absolute = package.relative_loader_cache('"C:/msys64/ucrt64/lib/svg.dll"\n', prefix, modules)
            self.assertEqual(package.relative_loader_cache('"lib/svg.dll"\n', prefix, modules), absolute)
            for path in ("C:lib/svg.dll", "/lib/svg.dll", "../lib/svg.dll", "//server/share/lib/svg.dll",
                         "C:/msys64/ucrt64/lib/../svg.dll"):
                with self.subTest(path=path), self.assertRaises(ValueError):
                    package.relative_loader_cache(f'"{path}"\n', prefix, modules)

    @staticmethod
    def snapshot_error(code, operation="CreateToolhelp32Snapshot"):
        error = OSError("snapshot test error")
        error.winerror, error.operation = code, operation
        return error

    def test_snapshot_retries_only_documented_capture_race(self):
        process = MagicMock()
        process.poll.return_value = None
        output = []
        paths = [Path("/package/bin/music.exe"), Path("/package/bin/libgtk-4-1.dll")]
        with patch.object(check, "loaded_modules", side_effect=[self.snapshot_error(24), paths]) as read, \
             patch.object(check.time, "sleep"), patch.object(check.time, "monotonic", return_value=0):
            self.assertEqual(check.snapshot_modules(process, output), paths)
            self.assertEqual(read.call_count, 2)
        self.assertIn("retrying complete snapshot", output[0])

    def test_snapshot_other_errors_and_exhausted_window_fail(self):
        process = MagicMock()
        process.poll.return_value = None
        for code, operation in ((6, "CreateToolhelp32Snapshot"), (5, "CreateToolhelp32Snapshot"), (24, "Module32NextW")):
            with self.subTest(code=code, operation=operation), \
                 patch.object(check, "loaded_modules", side_effect=self.snapshot_error(code, operation)) as read, \
                 self.assertRaises(OSError):
                check.snapshot_modules(process, [])
            self.assertEqual(read.call_count, 1)
        with patch.object(check, "loaded_modules", side_effect=self.snapshot_error(24)) as read, \
             patch.object(check.time, "monotonic", side_effect=[0, 2]), self.assertRaises(OSError):
            check.snapshot_modules(process, [])
        self.assertEqual(read.call_count, 1)

    def test_snapshot_cannot_pass_after_child_exits(self):
        process = MagicMock()
        process.poll.return_value = 0
        with patch.object(check, "loaded_modules") as read, self.assertRaisesRegex(ValueError, "exited"):
            check.snapshot_modules(process, [])
        read.assert_not_called()


if __name__ == "__main__":
    unittest.main()
