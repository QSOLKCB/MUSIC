#!/usr/bin/env python3
"""Assemble the native UCRT64 preview. Build-time Python only, no runtime Python."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile


API_SET = re.compile(r"^(?:api|ext)-ms-win-[a-z0-9-]+\.dll$", re.I)


def run(*args):
    return subprocess.check_output([str(arg) for arg in args], text=True, encoding="utf-8", errors="strict")


def imports(binary, objdump):
    output = run(objdump, "-p", binary)
    if "file format pei-x86-64" not in output:
        raise ValueError(f"Not a native x64 PE image: {binary}")
    names = re.findall(r"^\s*DLL Name:\s*(\S+)\s*$", output, re.M)
    if not names:
        raise ValueError(f"No PE imports found: {binary}")
    for name in names:
        if Path(name).name != name or "/" in name or "\\" in name or ":" in name:
            raise ValueError(f"Invalid DLL import: {name}")
    return names


def dll_index(directory, suffixes=(".dll",)):
    result = {}
    for path in directory.glob("*"):
        if path.is_file() and path.suffix.lower() in suffixes:
            key = path.name.lower()
            if key in result:
                raise ValueError(f"Duplicate case-insensitive DLL: {path}")
            result[key] = path
    return result


def dependency_closure(roots, available, system, get_imports):
    """Never silently ignore an unresolved import or copy a Windows system DLL."""
    pending = list(roots)
    visited, bundled, external = set(), {}, set()
    while pending:
        binary = pending.pop()
        if binary in visited:
            continue
        visited.add(binary)
        for name in get_imports(binary):
            key = name.lower()
            if key.startswith(("msys-", "cygwin")):
                raise ValueError(f"POSIX runtime is not allowed: {name}")
            if API_SET.fullmatch(key) or key in system:
                external.add(key)
            elif key in available:
                bundled[key] = available[key]
                pending.append(available[key])
            else:
                raise ValueError(f"Unresolved DLL {name}, imported by {binary}")
    return list(bundled.values()), sorted(external)


def relative_loader_cache(text, prefix, modules):
    """GdkPixbuf's relocatable build resolves relative paths from its DLL prefix."""
    expected = {path.relative_to(prefix).as_posix().lower(): path for path in modules}
    found = set()
    lines = []
    for line in text.splitlines():
        if line.startswith('"') and line.lower().endswith('.dll"'):
            path = Path(line[1:-1].replace("\\\\", "/"))
            try:
                relative = path.relative_to(prefix).as_posix()
            except ValueError as error:
                raise ValueError(f"Loader outside UCRT64 prefix: {path}") from error
            if relative.lower() not in expected:
                raise ValueError(f"Unexpected loader: {relative}")
            found.add(relative.lower())
            line = json.dumps(relative)
        # Generated comments can contain the build prefix; they are not needed.
        if not line.startswith("#"):
            lines.append(line)
    if found != set(expected):
        raise ValueError("Loader query did not describe every bundled module")
    return "\n".join(lines) + "\n"


def copy_tree(source, target, required=True):
    if not source.is_dir():
        if required:
            raise ValueError(f"Missing runtime assets: {source}")
        return
    shutil.copytree(source, target, dirs_exist_ok=True)


def build_package(build, prefix, destination, source):
    if os.name != "nt" or prefix.name.lower() != "ucrt64":
        raise ValueError("Run with the native MSYS2 UCRT64 Python on Windows")
    if destination.exists():
        raise ValueError(f"Refusing to overwrite package output: {destination}")
    dirty = run("git", "-C", source, "status", "--porcelain").strip()
    if dirty:
        raise ValueError("Package only a clean checkout so HEAD identifies the actual source:\n" + dirty)
    source_commit = run("git", "-C", source, "rev-parse", "HEAD").strip()
    source_tree = run("git", "-C", source, "show", "-s", "--format=%T", "HEAD").strip()
    objdump = prefix / "bin/objdump.exe"
    system = dll_index(Path(os.environ["SystemRoot"]) / "System32", (".dll", ".drv"))
    bin_dir = destination / "bin"
    bin_dir.mkdir(parents=True)
    for name in ("music.exe", "music-cli.exe", "music-runtime-check.exe"):
        shutil.copy2(build / name, bin_dir / name)
    modules = sorted((prefix / "lib/gdk-pixbuf-2.0").glob("*/loaders/*.dll"))
    if not modules or not any("svg" in path.name.lower() for path in modules):
        raise ValueError("The SVG pixbuf loader is required for bundled icons")
    for path in modules:
        target = destination / path.relative_to(prefix)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    binaries = [bin_dir / "music.exe", bin_dir / "music-cli.exe", bin_dir / "music-runtime-check.exe", *modules]
    dlls, system_imports = dependency_closure(binaries, dll_index(prefix / "bin"), system,
                                            lambda path: imports(path, objdump))
    for path in dlls:
        shutil.copy2(path, bin_dir / path.name)
    cache = relative_loader_cache(run(prefix / "bin/gdk-pixbuf-query-loaders.exe", *modules), prefix, modules)
    versions = {path.parent.parent for path in modules}
    if len(versions) != 1:
        raise ValueError("Multiple pixbuf loader ABI directories are not supported")
    cache_path = destination / next(iter(versions)).relative_to(prefix) / "loaders.cache"
    cache_path.write_text(cache, encoding="utf-8")
    for directory in ("share/glib-2.0/schemas", "share/icons/Adwaita", "share/icons/hicolor", "share/licenses"):
        copy_tree(prefix / directory, destination / directory)
    for directory in ("share/icons/AdwaitaLegacy", "etc/fonts", "share/fontconfig"):
        copy_tree(prefix / directory, destination / directory, required=False)
    schemas = destination / "share/glib-2.0/schemas"
    run(prefix / "bin/glib-compile-schemas.exe", "--strict", schemas)
    if not (schemas / "gschemas.compiled").is_file():
        raise ValueError("GLib schema compilation produced no cache")
    msys = prefix.parent
    for name in ("LICENSE", "README.md", "docs/MINIAUDIO-LICENSE.txt", "docs/WINDOWS.md", "docs/DEVICE-ACCEPTANCE.md"):
        shutil.copy2(source / name, destination / Path(name).name)
    shutil.copy2(source / "packaging/windows/MUSIC.cmd", destination / "MUSIC.cmd")
    shutil.copy2(source / "packaging/windows/THIRD-PARTY.txt", destination / "THIRD-PARTY.txt")
    settings = destination / "etc/gtk-4.0"
    settings.mkdir(parents=True)
    (settings / "settings.ini").write_text("[Settings]\ngtk-theme-name=Adwaita\ngtk-font-name=Segoe UI 10\n", encoding="utf-8")
    pacman = msys / "usr/bin/pacman.exe"
    (destination / "build-packages.txt").write_text(run(pacman, "-Q"), encoding="utf-8")
    ownership = {}
    for line in run(pacman, "-Ql").splitlines():
        package, _, path = line.partition(" ")
        if path.startswith("/ucrt64/"):
            ownership[path[len("/ucrt64/"):].lower()] = package
    runtime_files = [*dlls, *modules]
    owners = {}
    for path in runtime_files:
        relative = path.relative_to(prefix).as_posix()
        if relative.lower() not in ownership:
            raise ValueError(f"Runtime file not owned by an installed package: {path}")
        owners[relative] = ownership[relative.lower()]
    (destination / "runtime-packages.txt").write_text(run(pacman, "-Qi", *sorted(set(owners.values()))), encoding="utf-8")
    manifest = {
        "format": 1, "source_commit": source_commit, "source_tree": source_tree,
        "source_dirty": False,
        "architecture": "Windows x64 / MSYS2 UCRT64", "runtime_owners": owners,
        "windows_system_imports": system_imports,
        "files": {path.relative_to(destination).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in sorted(destination.rglob("*")) if path.is_file()},
    }
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    archive = destination.with_suffix(".zip")
    if archive.exists():
        raise ValueError(f"Refusing to overwrite archive: {archive}")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
        for path in sorted(destination.rglob("*")):
            if path.is_file():
                output.write(path, path.relative_to(destination.parent))
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".zip.sha256").write_text(f"{digest}  {archive.name}\n", encoding="utf-8")
    print(f"Packaged {archive} ({len(dlls)} runtime DLLs; no MSYS runtime)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build-windows"))
    parser.add_argument("--prefix", type=Path, default=Path(sys.prefix))
    parser.add_argument("--output", type=Path, default=Path("dist/MUSIC-windows-x64"))
    args = parser.parse_args()
    try:
        build_package(args.build.resolve(), args.prefix.resolve(), args.output.resolve(), Path(__file__).resolve().parents[1])
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(f"Packaging failed: {error}")
