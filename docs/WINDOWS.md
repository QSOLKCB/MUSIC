# Native Windows desktop preview

The supported build path for this milestone is **Windows x64, MSYS2 UCRT64,
GTK 4.10+ and GCC**. The result is a portable, unsigned ZIP, not an installer.
The existing MSVC core/CLI job remains separate. Neither build path establishes
physical microphone/speaker acceptance; use [the hardware checklist](DEVICE-ACCEPTANCE.md).

## Build and package

Install [MSYS2](https://www.msys2.org/) from its official site, open its **UCRT64**
shell, and update it using the [MSYS2 update instructions](https://www.msys2.org/docs/updating/).
Install the build dependencies:

```sh
pacman -S --needed git \
  mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-gtk4 \
  mingw-w64-ucrt-x86_64-adwaita-icon-theme mingw-w64-ucrt-x86_64-librsvg
./build-windows.sh
```

Run from a clean checkout. The script builds in `build-windows`, runs all core
checks and the deterministic render digest, creates `dist/MUSIC-windows-x64.zip`
and its SHA-256 sidecar, then tests that **extracted ZIP**. The packager refuses
to overwrite an existing package directory or archive. Move the old output
elsewhere before rebuilding; it never deletes a previous package for you.

The GTK dependency route follows the [official Windows GTK guidance](https://www.gtk.org/docs/installations/windows/).
MSYS2 packages are rolling; this is not a bit-reproducible dependency lock. CI
pins the setup action by commit and records exact installed package versions,
source commit/tree, dirty-checkout state, runtime ownership, and packaged file
hashes inside the ZIP. miniaudio remains pinned and checksum-verified by CMake.

## Run the ZIP

Extract the **entire** archive into a user-owned directory, then open
`MUSIC.cmd` or `bin/music.exe`. Keep `bin`, `lib`, `share`, and `etc` together.
No MSYS2 installation, Python, development PATH, driver installation or network
connection is required by the packaged application. Paths with spaces and
Unicode characters are covered by the package check. Windows security policy
may flag unsigned software; do not bypass security warnings to run this preview.

`bin/music-cli.exe --inspect path-to-project.music` remains available. Its
legacy C filename handling is not a guarantee for every Unicode filename;
use the GTK file picker for Unicode project paths. No file association or
registry changes are installed. To remove MUSIC, delete only its extracted
application directory after preserving projects saved there.

## Runtime closure and notices

The packager recursively reads x64 PE imports for the app, CLI, runtime diagnostic
and image-loader DLLs. Each dependency must resolve to the UCRT64 prefix or to
Windows System32/API-set libraries. Missing imports, non-x64 images, MSYS/Cygwin
runtime dependencies, missing assets, or incomplete loader queries fail the build.
It bundles GTK's compiled schemas, Adwaita/hicolor icons, image loaders, optional
fontconfig data, and dependency license texts. The pixbuf cache uses relative
module paths so relocation does not refer back to the build machine. No optional
GIO network/TLS modules are needed for this local-file application.

`manifest.json` records payload hashes and DLL package ownership;
`runtime-packages.txt` records runtime package metadata; `build-packages.txt`
records the complete build inventory. `share/licenses` includes dependency
license texts installed by MSYS2. See `THIRD-PARTY.txt`: a public binary
release still needs the matching corresponding-source archive collection and
license review. This engineering artifact is not a release declaration.

## What the automated check proves

The verifier extracts into a new Unicode/space-containing location, uses an
unrelated working directory and a Windows-only PATH, and removes inherited GTK,
GDK, GIO and XDG overrides. It verifies every payload hash and required asset,
runs the packaged CLI/version commands, and checks real schema, symbolic-icon
and SVG loading with `music-runtime-check.exe`.

It then runs the packaged `music.exe --smoke`: native widget edits, playback,
null-device recording, save, export and reopen. While it runs, the verifier checks
the child's loaded module paths and rejects DLLs outside the extracted package
or Windows directory. The smoke log and ZIP are retained by the Windows desktop
CI job for 14 days. A green job is not a physical audio, latency, Windows file
picker, hardware-GPU, installer-signing, or clean-VM acceptance result.

Portable Python tests also check missing transitive dependencies, dependency
cycles/case folding, forbidden POSIX runtimes, invalid PE imports, missing assets,
loader-cache relocation, inventory tampering and development-environment removal.
They run in CTest when build-time Python is available. Python is not a dependency
of the C99 core or the packaged runtime.
