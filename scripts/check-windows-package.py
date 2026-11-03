#!/usr/bin/env python3
"""Verify and smoke-test the extracted ZIP outside the development environment."""
import argparse
import ctypes
from contextlib import contextmanager
from ctypes import wintypes
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tempfile
import time
import zipfile


def archive_root(names):
    roots, seen = set(), set()
    reserved = re.compile(r"^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)", re.I)
    for name in names:
        path = PurePosixPath(name)
        parts = name.rstrip("/").split("/")
        if (path.is_absolute() or ":" in name or "\\" in name or
                any(not part or part in (".", "..") or part.endswith((".", " ")) or
                    reserved.match(part) for part in parts)):
            raise ValueError(f"Unsafe archive member: {name}")
        if len(parts) < 2 and not name.endswith("/"):
            raise ValueError(f"Unmanifested top-level file: {name}")
        key = name.rstrip("/").casefold()
        if key in seen:
            raise ValueError(f"Duplicate/case-colliding archive member: {name}")
        seen.add(key)
        roots.add(parts[0])
    if len(roots) != 1:
        raise ValueError("Expected all archive members under one package root")
    return roots.pop()


def verify_tree(root):
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    if manifest["format"] != 1 or not manifest["runtime_owners"]:
        raise ValueError("Missing runtime inventory")
    actual = {path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()}
    if actual != set(manifest["files"]) | {"manifest.json"}:
        raise ValueError("Package file inventory mismatch")
    for name, digest in manifest["files"].items():
        if hashlib.sha256((root / name).read_bytes()).hexdigest() != digest:
            raise ValueError(f"Package hash mismatch: {name}")
    for name in ("bin/music.exe", "bin/music-cli.exe", "bin/music-runtime-check.exe", "MUSIC.cmd",
                 "LICENSE", "MINIAUDIO-LICENSE.txt", "THIRD-PARTY.txt", "runtime-packages.txt",
                 "share/glib-2.0/schemas/gschemas.compiled", "share/icons/Adwaita/index.theme",
                 "share/icons/hicolor/index.theme"):
        if not (root / name).is_file():
            raise ValueError(f"Required package file missing: {name}")
    if not list((root / "share/licenses").rglob("*")):
        raise ValueError("No dependency licenses")
    caches = list((root / "lib/gdk-pixbuf-2.0").glob("*/loaders.cache"))
    if len(caches) != 1:
        raise ValueError("Expected one pixbuf loader cache")
    for line in caches[0].read_text(encoding="utf-8").splitlines():
        if line.startswith('"') and line.lower().endswith('.dll"'):
            name = json.loads(line)
            relative = PurePosixPath(name)
            if relative.is_absolute() or ".." in relative.parts or ":" in name or "\\" in name:
                raise ValueError("Non-relocatable pixbuf loader cache")
            if not (root / name).is_file():
                raise ValueError(f"Missing pixbuf loader: {name}")
    return manifest


def loaded_modules(pid):
    """Read this test child's DLL paths, not an unrelated process."""
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    psapi.EnumProcessModulesEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.HMODULE), wintypes.DWORD,
                                        ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
    psapi.GetModuleFileNameExW.argtypes = [wintypes.HANDLE, wintypes.HMODULE, wintypes.LPWSTR, wintypes.DWORD]
    handle = kernel.OpenProcess(0x0400 | 0x0010, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        modules = (wintypes.HMODULE * 2048)()
        needed = wintypes.DWORD()
        if not psapi.EnumProcessModulesEx(handle, modules, ctypes.sizeof(modules), ctypes.byref(needed), 3):
            raise ctypes.WinError(ctypes.get_last_error())
        if not needed.value or needed.value % ctypes.sizeof(wintypes.HMODULE):
            raise ValueError("Invalid loaded-module count")
        if needed.value > ctypes.sizeof(modules):
            raise ValueError("Loaded-module buffer too small")
        paths = []
        for module in modules[:needed.value // ctypes.sizeof(wintypes.HMODULE)]:
            path = ctypes.create_unicode_buffer(32768)
            length = psapi.GetModuleFileNameExW(handle, module, path, len(path))
            if not length or length >= len(path):
                raise ctypes.WinError(ctypes.get_last_error())
            paths.append(Path(path.value).resolve())
        return paths
    finally:
        kernel.CloseHandle(handle)


def verify_module_paths(paths, root, windows):
    required = {root / "bin/music.exe", root / "bin/libgtk-4-1.dll"}
    if not required.issubset(set(paths)):
        raise ValueError("Module audit did not observe the packaged executable and GTK DLL")
    for path in paths:
        if not path.is_relative_to(root) and not path.is_relative_to(windows):
            raise ValueError(f"Loaded DLL outside package/Windows: {path}")


def isolated_environment():
    # Inherit only ordinary Windows account/temporary-directory state. In
    # particular, no MSYS PATH, GTK/GIO/GDK paths, XDG overrides or preload hooks.
    keep = {"systemroot", "windir", "comspec", "temp", "tmp", "userprofile", "appdata", "localappdata",
            "homedrive", "homepath", "username", "userdomain", "programdata"}
    env = {key: value for key, value in os.environ.items() if key.lower() in keep}
    windows = Path(os.environ["SystemRoot"])
    env["PATH"] = os.pathsep.join((str(windows / "System32"), str(windows)))
    env.update(GTK_A11Y="none", GSK_RENDERER="cairo")
    return env


@contextmanager
def validation_log(log):
    log.parent.mkdir(parents=True, exist_ok=True)
    output = []
    try:
        yield output
    except Exception as error:
        output.append(f"FAILED: {error}")
        raise
    finally:
        log.write_text("\n".join(output), encoding="utf-8")


def decoded(value):
    return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else (value or "")


def run_logged(command, cwd, env, output):
    output.append("Stage: " + str(command))
    try:
        result = subprocess.run(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                                errors="replace", timeout=30, check=False)
    except subprocess.TimeoutExpired as error:
        output.append(decoded(error.stdout))
        raise
    output.append(result.stdout)
    result.check_returncode()


def smoke(root, cwd, env, output):
    output.append("Stage: packaged GUI/null-audio smoke and loaded-module audit")
    with subprocess.Popen([str(root / "bin/music.exe"), "--smoke"], cwd=cwd, env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          encoding="utf-8", errors="replace") as process:
        captured = False
        try:
            time.sleep(1.5)
            paths = loaded_modules(process.pid)
            windows = Path(os.environ["SystemRoot"]).resolve()
            verify_module_paths(paths, root, windows)
            output.append("Loaded modules:\n" + "\n".join(str(path) for path in paths))
            stdout, _ = process.communicate(timeout=60)
            output.append(stdout)
            captured = True
            if process.returncode or "GTK/audio smoke: PASS" not in stdout:
                raise ValueError(f"Packaged GUI smoke failed ({process.returncode})")
            if any(word in stdout for word in ("CRITICAL", "ERROR", "FAIL")):
                raise ValueError("Packaged GUI logged an error")
        finally:
            if process.poll() is None:
                process.kill()
            if not captured:
                stdout, _ = process.communicate()
                output.append(decoded(stdout))


def check_archive(archive, log):
    with validation_log(log) as output:
        output.append(f"Archive: {archive}")
        if os.name != "nt":
            raise ValueError("Native package execution requires Windows")
        with tempfile.TemporaryDirectory(prefix="MUSIC relocated Ω space ") as temporary:
            base = Path(temporary)
            output.append("Stage: safe extraction and payload validation")
            with zipfile.ZipFile(archive) as source:
                root_name = archive_root(source.namelist())
                source.extractall(base)
            root = base / root_name
            manifest = verify_tree(root)
            output.extend((f"Source commit: {manifest['source_commit']}", f"Source tree: {manifest['source_tree']}"))
            cwd = base / "unrelated working directory"
            cwd.mkdir()
            env = isolated_environment()
            svg = next((root / "share/icons/Adwaita").rglob("document-open-symbolic.svg"), None)
            if svg is None:
                raise ValueError("No symbolic SVG fixture in Adwaita")
            env["MUSIC_PACKAGE_SVG"] = str(svg)
            for executable, arguments in (("music.exe", ["--version"]), ("music-cli.exe", ["--version"]),
                                          ("music-runtime-check.exe", [])):
                run_logged([str(root / "bin" / executable), *arguments], cwd, env, output)
            smoke(root, cwd, env, output)
            output.append("Extracted Windows package: PASS (relocated Unicode/space path, isolated PATH, native GTK, null audio)")
            print(output[-1])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--log", type=Path, default=Path("artifacts/windows-package-smoke.log"))
    args = parser.parse_args()
    try:
        check_archive(args.archive.resolve(), args.log.resolve())
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        sys.exit(f"Package verification failed: {error}")
