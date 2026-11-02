# Validation status

This is a source preview. Executed checks and unverified areas are kept separate.

## Executed locally

- Strict C99 core compilation with GCC 13.3, `-Wall -Wextra -Wpedantic -Werror`.
- Nine core test groups passed with AddressSanitizer and UndefinedBehaviorSanitizer. LeakSanitizer could not run in this workspace because `/proc`/ptrace access is restricted; leak detection is enabled in the normal Linux CI sanitizer job.
- Deterministic demo WAV SHA-256: `79e190e03151525c844b882cc2c3174fe6be7651d9fe29d7dc2584b7b72c301a`.
- Pinned miniaudio adapter compiled as C99 against version 0.11.26, commit `877ea4a7c2601c7b0f2a44e83dfac42e6470fea6`.

## CI checks

The workflow runs core tests on Windows, Linux and macOS, compares their rendered WAV digests, builds the GTK desktop on Linux and macOS, and exercises actual GTK widgets plus the null audio device on Linux. The GUI check covers playback, capture, project save/reopen, and WAV export. These CI results are pending until the first run completes.

## Not yet verified

- Physical microphone/speaker devices, latency, drift, dropouts and recording permission prompts.
- Windows GTK compilation/packaging and a signed/notarized self-contained Mac application.
- Long recording stress tests, professional recording quality, and actual hardware resource measurements.
- Adversarial concurrent replacement of local filesystem inputs or crash recovery.

The null-device test never accesses a real microphone. A successful desktop build is not a physical-device acceptance test.
