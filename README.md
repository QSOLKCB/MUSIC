# MUSIC

**The Ultimate Music Software.** A local music-making workstation with a portable C99 audio core and a native GTK desktop. Windows, Linux, and macOS remain target platforms. The original Mac pricing line was a joke about Apple's pricing; it is preserved in the [founding note](docs/FOUNDING-NOTE.txt).

Version **0.1.0-preview** is the first audio-arrangement milestone. It records and arranges audio; MIDI instruments and plugin hosting are later stages.

## Make a sketch

1. Add a WAV, drop one onto the window, or choose **Demo** for a built-in three-track sketch.
2. Drag clips along the timeline. **Snap** follows the BPM grid. Use Start, Trim in, and Trim out for precise placement without changing the source audio.
3. Adjust each track's gain, mute, and solo. Click the timeline ruler to move the playhead; Play/Pause and Stop control local playback.
4. Press **Record** to capture the default microphone/input while existing tracks play. Finish the take to add it as a new track. Microphone monitoring is off.
5. **Save** writes a single `.music` project containing its audio and arrangement. **Export WAV** renders the mix to a stereo WAV.

Nothing records or plays automatically. New, Open, Demo, and Close guard unsaved work. Project and export writes use a separate temporary file, publishing the destination only after the full write succeeds. File operations run off the GTK thread and can be cancelled.

## Build and run

### Linux desktop

Ubuntu 24.04 or newer:

```sh
sudo apt install build-essential cmake pkg-config libgtk-4-dev
./build.sh
./music
```

### macOS desktop

With Homebrew installed:

```sh
brew install cmake gtk4 pkgconf
./build.sh
./music
```

The build produces `build/music.app` with a microphone usage description. macOS may ask for recording permission when you press Record. This preview is a source build using installed GTK libraries; it is not a signed, notarized, self-contained Mac distribution.

### Windows core and CLI

The C99 core and project-inspection CLI build with CMake and a C compiler:

```sh
cmake -S . -B build -DMUSIC_BUILD_GUI=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The GTK source and audio adapter are intended for Windows too. A supported Windows GTK distribution and packaging path remain a follow-up; passing the Windows core tests is not a Windows desktop acceptance test.

### Dependencies

The core uses only the C standard library. The desktop adds system GTK 4.10+ and [miniaudio 0.11.26](https://github.com/mackron/miniaudio/tree/877ea4a7c2601c7b0f2a44e83dfac42e6470fea6) for local audio devices. The GUI build downloads that one pinned header and checks its SHA-256. It can instead be placed at `build/dependencies/miniaudio.h` before configuring for an offline build. Runtime needs no download, account, cloud inference, HTTP server, Node, or Docker.

The app does not install system packages or drivers. Linux and macOS desktop builds, Linux native UI tests, and portable core tests on all three operating systems have passed in [CI](.github/workflows/checks.yml). See [validation status](docs/VALIDATION.md) for executed results and limits.

## Preview scope

| Area | Current behaviour |
| --- | --- |
| Tracks | Up to 16, one audio clip per track; position, trim, gain, mute and solo |
| Input | 16-bit PCM WAV, mono/stereo, 44.1 or 48 kHz |
| Internal audio | 48 kHz stereo, signed 16-bit PCM; 44.1 kHz imports use integer linear interpolation |
| Mixing | Fixed-point gain, wide integer accumulation, saturation at 16-bit output limits; no automatic normalization |
| Recording | Default local input; nominal alignment with playback; no hardware latency compensation or live input monitoring |
| Limits | 10 minutes per source/take, 30-minute arrangement, 256 MiB of project audio; recording stops when its take/memory limit is reached |
| Projects | Versioned `.music` container embeds all source PCM and settings; failed loads preserve the current project |
| Export | Stereo 48 kHz / 16-bit PCM WAV; deterministic for the same project PCM/settings |
| Timeline | Fits the arrangement; beat snapping and precise seconds fields; BPM changes the grid, not audio speed |

This is a working foundation, not yet a production DAW. Higher-resolution and floating-point audio, high-quality resampling, clip splitting/copying, undo/redo, device selection, latency calibration, MIDI/piano roll, effects, automation, plugin hosting, and signed installers are not implemented. Keep important originals and save recorded takes before closing. A crash can lose an unsaved take; this preview has no autosave recovery.

## Validation and development

```sh
cmake -S . -B build-core -DMUSIC_BUILD_GUI=OFF -DMUSIC_SANITIZE=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

Core tests exercise mixing and negative clipping, mute/solo, trimming/placement, deterministic project round trips, malformed/truncated WAV and project data, resampling, write failures, memory/track settings, and deterministic export. CI compares the same rendered WAV digest across Windows, Linux, and macOS. The Linux GTK smoke test uses miniaudio's null device to exercise playback, recording, project save/reopen, and WAV export without accessing a real microphone.

The CLI can inspect a project without GTK:

```sh
./build/music-cli --inspect MySong.music
```

See [architecture and project format](docs/ARCHITECTURE.md). Our source retains the repository's [Apache 2.0 licence](LICENSE); miniaudio's separate [licence text](docs/MINIAUDIO-LICENSE.txt) is included.
