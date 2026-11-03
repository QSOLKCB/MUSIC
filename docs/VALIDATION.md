# Validation status

This is a source preview. Executed checks and unverified areas are kept separate.

## Executed locally

- Strict C99 core compilation with GCC 13.3, `-Wall -Wextra -Wpedantic -Werror`.
- Nine core test groups passed with AddressSanitizer and UndefinedBehaviorSanitizer. LeakSanitizer could not run in this workspace because `/proc`/ptrace access is restricted; leak detection is enabled in the normal Linux CI sanitizer job.
- Deterministic demo WAV SHA-256: `79e190e03151525c844b882cc2c3174fe6be7651d9fe29d7dc2584b7b72c301a`.
- Pinned miniaudio adapter compiled as C99 against version 0.11.26, commit `877ea4a7c2601c7b0f2a44e83dfac42e6470fea6`.

## Recording-capacity regression (3 November 2026)

The hardware-free `audio-capacity` CTest compiles the production `audio.c` against a device seam and an allocation ledger. It checks 16 one-second takes retain 3,072,000 requested PCM bytes rather than 1,843,200,000; checks join-before-reallocation, sample preservation, empty/full takes, invalid 64-bit cursors and repeated stop; and injects shrink failure to verify ownership and full-capacity budget enforcement. The core suite additionally checks spare-capacity save/load writes only captured frames. This is not a physical-device/thread-scheduling or whole-process RSS test.

Strict GCC C99 compilation and both test executables passed locally, including ASan/UBSan with leak detection disabled for the same workspace restriction above. Updated cross-platform results must be read at the exact PR head; the historical CI result below is not evidence for this fix.

## Executed in CI

All five jobs passed in [run 41547037825](https://github.com/QSOLKCB/MUSIC/actions/runs/41547037825) at commit `846578ef83afdd96629722ae4f61f32bffe34137` on 2 November 2026 UTC:

| Platform | Executed result |
| --- | --- |
| Ubuntu 24.04 | Core tests with address/undefined-behaviour sanitizers, including normal leak detection; matching demo WAV digest |
| Windows | Core tests and matching demo WAV digest |
| macOS | Core tests and matching demo WAV digest; GTK desktop and `.app` bundle compiled successfully |
| Linux GTK desktop | Built successfully; native widget edits, playback, null-device capture, project save/reopen, and WAV export passed under Xvfb |

The GUI test verifies that a Unicode track name, clip placement, trims and gain survive saving and reopening. It decodes the exported WAV and checks its arrangement duration. CI retains a native window screenshot and smoke log as `gtk-evidence` artifacts for 14 days. The [PR checks](https://github.com/QSOLKCB/MUSIC/pull/1/checks) track subsequent commits.

## Not yet verified

- Physical microphone/speaker devices, latency, drift, dropouts and recording permission prompts.
- Physical Windows desktop acceptance and a signed/notarized self-contained Mac application.
- Long recording stress tests, professional recording quality, and actual hardware resource measurements.
- Adversarial concurrent replacement of local filesystem inputs or crash recovery.

The null-device test never accesses a real microphone. A successful desktop build is not a physical-device acceptance test.

## Windows desktop packaging milestone (3 November 2026)

Added a native MSYS2 UCRT64 GTK build and unsigned portable ZIP, including
recursive DLL closure, relocatable image loaders, schemas, icons, dependency
notices and a file-hash/source manifest. The Windows CI job runs the extracted
ZIP from a Unicode/space path with no development PATH and checks loaded DLLs.
The separate runtime diagnostic exercises schemas, symbolic icons and SVGs;
the existing GUI smoke exercises null-device audio and project persistence.

Locally, all four Release CTest targets pass, including 19 portable packaging
negative tests; the deterministic demo WAV digest remains unchanged. Native
Windows build/package/smoke results must be read at the exact PR head. They have
not been executed on this Linux workspace, which has no Windows desktop or GTK.
No physical device, permission prompt, latency or dropout result is claimed.
The [physical-device checklist](DEVICE-ACCEPTANCE.md) remains explicitly unexecuted.
Historical validation and exact-span mixer evidence are preserved unchanged.

The initial Windows workflow could not start because the repository permits only
GitHub-owned or enterprise-owned Actions (runs `41558625804` and `41558635807`).
The workflow now uses the Windows runner's preinstalled MSYS2 and normal package
manager commands. The repository restriction is unchanged; the disallowed action
is neither vendored nor executed indirectly. Those startup failures remain part
of the validation history.

The Windows job uploads only logs, source/file manifests, package inventories and
archive hashes. Dependency-bearing ZIP/DLL upload is gated off until matching
corresponding sources and redistribution review are complete. The locally built
ZIP is still extracted and tested inside CI; no binary release is implied.
